#include <memory>
#include <string>
#include <filesystem>

#include <folly/init/Init.h>
#include <folly/logging/xlog.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <proxygen/httpserver/HTTPServer.h>
#ifndef DEBUG
#include <syslog.h>
#include <sys/resource.h>
#endif

#include <ranges>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "server/core.h"

#include "utils/defines.h"
#include "utils/config.h"
#include "server/module.h"


using namespace proxygen;

std::shared_mutex config_mutex;

thread_local folly::EvictingCacheMap<XXH64_hash_t, Cache::ResponseData> tl_response_data_cache(1000);
thread_local folly::EvictingCacheMap<XXH64_hash_t, Cache::VirtualHostConfig> tl_host_config_cache(100);
thread_local folly::EvictingCacheMap<XXH64_hash_t, folly::fbstring> tl_directory_redirect_cache(1000);

extern "C" {
extern ModuleManage::Module *__start_my_module_section[] __attribute__((weak));
extern ModuleManage::Module *__stop_my_module_section[] __attribute__((weak));
}

template<typename T> // either uint8_t or StringPiece
const folly::fbstring &getSingleOrEmptyTest(const T &nameOrCode) {
    const folly::fbstring *res = nullptr;
    forEachValueOfHeader(nameOrCode, [&](const folly::fbstring &value) -> bool {
        if (res != nullptr) {
            // a second value is found
            res = nullptr;
            return true; // stop processing
        } else {
            // the first value is found
            res = &value;
            return false;
        }
    });
    if (res == nullptr) {
        return folly::fbstring();
    } else {
        return *res;
    }
}

class HandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase * /*evb*/) noexcept override {
        std::shared_lock lock(config_mutex);

        for (const auto &[hostname, config]: Config::virtual_hosts) {
            tl_host_config_cache.set(Utils::computeXXH64Hash(hostname), config);

            if (!std::filesystem::exists(config.web_root_directory.c_str())) continue;

            const auto web_root = config.web_root_directory;
            const auto normalized_root = web_root.back() == '/' ? web_root : web_root + "/";

            for (const auto &index_file: config.index_page_files) {
                const auto full_index_path = web_root + '/' + index_file.data();
                if (std::filesystem::exists(full_index_path.toStdString())) {
                    tl_directory_redirect_cache.set(Utils::computeXXH64Hash(normalized_root), full_index_path);
                    break;
                }
            }

            for (const auto &entry: std::filesystem::recursive_directory_iterator(config.web_root_directory.c_str())) {
                if (!entry.is_directory()) continue;

                const auto dir_path = entry.path().string();
                for (const auto &index_file: config.index_page_files) {
                    const auto full_index_path = dir_path + "/" + index_file;
                    if (std::filesystem::exists(full_index_path)) {
                        tl_directory_redirect_cache.set(Utils::computeXXH64Hash(dir_path), full_index_path);
                        break;
                    }
                }
            }
        }
    }

    void onServerStop() noexcept override {
    }

    RequestHandler *onRequest(RequestHandler *requestHandler, HTTPMessage *message) noexcept override {
        return new ServerHandler(&tl_response_data_cache, &tl_host_config_cache, &tl_directory_redirect_cache);
    }
};

void register_all_modules(ModuleManage::System<> &system) {
    if (&__start_my_module_section == nullptr || &__stop_my_module_section == nullptr) {
        return;
    }
    for (ModuleManage::Module **mod = __start_my_module_section; mod != __stop_my_module_section; ++mod) {
        system.register_module(**mod);
    }
}

int main(int argc, char *argv[]) {
    folly::InitOptions folly_options;
    folly_options.install_fatal_signal_callbacks = false;
    folly_options.remove_flags = false;
    auto _ = folly::Init(&argc, &argv);

    XLOG(INFO) << "Starting " NAME_N_VERSION " " VERSION_NUM;

#ifndef DEBUG
    XLOG(INFO) << "Running in production mode - daemonizing process";

    pid_t pid, sid;

    pid = fork();
    if (pid > 0) {
        XLOG(INFO) << "Parent process exiting, daemon PID: " << pid;
        exit(EXIT_SUCCESS);
    } else if (pid < 0) {
        XLOG(ERR) << "Failed to fork daemon process";
        exit(EXIT_FAILURE);
    }

    XLOG(INFO) << "Process forked successfully, continuing as daemon";
    umask(0);

    sid = setsid();
    if (sid < 0) {
        XLOG(ERR) << "Failed to create new session";
        exit(EXIT_FAILURE);
    }
    XLOG(INFO) << "New session created with SID: " << sid;

    if ((chdir("/")) < 0) {
        XLOG(ERR) << "Failed to change working directory to root";
        exit(EXIT_FAILURE);
    }
    XLOG(INFO) << "Working directory changed to root";

#else
    XLOG(INFO) << "Running in DEBUG mode - no daemonization";
#endif

    Config::ServerConfig server_config(CONFIG_DIR);
    if (!server_config.initialize()) {
        XLOG(ERR) << "Failed to initialize server configuration";
        return -1;
    }
    XLOG(INFO) << "Server configuration loaded successfully";

    register_all_modules(g_moduleSystem);
    // Initialize the module system
    if (!g_moduleSystem.initialize()) {
        XLOG(ERR) << "Failed to initialize module system";
        return -1;
    }
    XLOG(INFO) << "Module system initialized successfully";

#ifndef DEBUG
    XLOG(INFO) << "Setting CPU affinity and process priority";
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    const int num_cores = 6;
    for (int i = 0; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
    }
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
        XLOG(INFO) << "CPU affinity set to " << num_cores << " cores";
    } else {
        XLOG(WARN) << "Failed to set CPU affinity";
    }

    // Set high priority (only root or permissions required)
    if (setpriority(PRIO_PROCESS, 0, -5) == 0) {
        XLOG(INFO) << "Process priority set to -5";
    } else {
        XLOG(WARN) << "Failed to set process priority";
    }
#endif

    XLOG(INFO) << "Loading virtual host configurations";
    std::vector<HTTPServer::IPConfig> IPs;
    if (!Config::load_virtual_host_configurations(IPs)) {
        XLOG(ERR) << "Failed to load virtual host configurations";
        return -1;
    }
    XLOG(INFO) << "Virtual host configurations loaded, " << IPs.size() << " configurations";

    HTTPServerOptions options;
    options.threads = static_cast<size_t>(server_config.threads);
    options.idleTimeout = std::chrono::milliseconds(60000);
    options.shutdownOn = {SIGINT, SIGTERM, SIGSEGV};
    options.enableContentCompression = false;
    options.handlerFactories =
            RequestHandlerChain().addThen<HandlerFactory>().build();
    options.h2cEnabled = true;
    options.supportsConnect = true;

    auto unsafeThreadPool = std::make_shared<folly::CPUThreadPoolExecutor>(
        server_config.threads,
        std::make_shared<folly::NamedThreadFactory>("UnsafeThreadPool"));
    folly::setUnsafeMutableGlobalCPUExecutor(unsafeThreadPool);
    XLOG(INFO) << "Thread pool created with " << server_config.threads << " threads";

    HTTPServer server(std::move(options));

    server.bind(IPs);

    server.start();

    g_moduleSystem.cleanup();
#ifndef DEBUG
    exit(EXIT_SUCCESS);
#endif

    return 0;
}
