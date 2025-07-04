#include <memory>
#include <string>
#include <filesystem>

#include <folly/init/Init.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <proxygen/httpserver/HTTPServer.h>
#ifndef DEBUG
#include <syslog.h>
#include <sys/resource.h>
#endif

#include <ranges>

#include "server/server.h"

#include "utils/defines.h"
#include "utils/config.h"
#include "ext/plugin_loader.h"
#include "utils/cache.h"
#include "utils/config.h"

using namespace proxygen;

PluginManager::HookManagerImpl *plugin_loader = nullptr;
PluginManager::PluginManager *plugin_manager = nullptr;
thread_local Cache::ARC<std::string, CacheRow *> tl_cache(100);

class HandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase * /*evb*/) noexcept override {
    }

    void onServerStop() noexcept override {
    }

    RequestHandler *onRequest(RequestHandler *requestHandler, HTTPMessage *message) noexcept override {
        const std::string hostname = message->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST);
        auto vhostAccessor = Cache::host_config_cache.get(hostname);

        auto path = vhostAccessor->web_root_directory + message->getPath();
        auto file_metadata = Cache::file_metadata_cache.get(path);
        if (file_metadata.has_value() && file_metadata->is_directory) {
            for (const auto &index_page: vhostAccessor->index_page_files) {
                std::string index_path = path + index_page;
                if (Cache::file_metadata_cache.get(index_path).has_value()) {
                    path = index_path; // Use the found index page
                    break;
                }
            }
        }

        return new StaticHandler(path, vhostAccessor->web_root_directory, &tl_cache);
    }
};


int main(int argc, char *argv[]) {
    folly::InitOptions folly_options;
    folly_options.install_fatal_signal_callbacks = false;
    folly_options.use_gflags = true;
    folly_options.remove_flags = false;
    auto _ = folly::Init(&argc, &argv);

    LOG(INFO) << "Starting " NAME_N_VERSION " " VERSION_NUM;

#ifndef DEBUG

    pid_t pid, sid;

    pid = fork();
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    } else if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    umask(0);

    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) {
        exit(EXIT_FAILURE);
    }

#endif
    static PluginManager::HookManagerImpl hookManager;
    static PluginManager::PluginManager pluginMgr(hookManager);

    plugin_loader = &hookManager;
    plugin_manager = &pluginMgr;

    Config::ServerConfig server_config(CONFIG_DIR);
    if (!server_config.load())
        return -1;

    // The plugin list is previously loaded in ServerConfig
    for (const auto &plugin: server_config.plugins) {
        if (plugin.enabled) {
            if (!plugin_manager->loadPlugin(plugin.path, plugin.parameters)) {
                LOG(ERROR) << "Failed to load plugin: " << plugin.path;
            } else {
                LOG(INFO) << "Loaded plugin: " << plugin.path;
            }
        }
    }
#ifndef DEBUG
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    const int num_cores = 6;
    for (int i = 0; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpuset), &cpuset);

    // Set high priority (only root or permissions required)
    setpriority(PRIO_PROCESS, 0, -5);
#endif

    std::vector<HTTPServer::IPConfig> IPs;
    if (!Config::load_virtual_host_configurations(IPs))
        return -1;

    HTTPServerOptions options;
    options.threads = static_cast<size_t>(server_config.threads);
    options.idleTimeout = std::chrono::milliseconds(60000);
    options.shutdownOn = {SIGINT, SIGTERM, SIGSEGV};
    options.enableContentCompression = false;
    options.handlerFactories =
            RequestHandlerChain().addThen<HandlerFactory>().build();
    options.h2cEnabled = true;
    options.supportsConnect = true;
    auto diskIOThreadPool = std::make_shared<folly::CPUThreadPoolExecutor>(
        server_config.threads,
        std::make_shared<folly::NamedThreadFactory>("StaticDiskIOThread"));
    setUnsafeMutableGlobalCPUExecutor(diskIOThreadPool);

    HTTPServer server(std::move(options));
    server.bind(IPs);

    std::thread t([&]() { server.start(); });

    t.join();

#ifndef DEBUG
    exit(EXIT_SUCCESS);
#endif

    return 0;
}
