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
#include "config.h"
#include "ext/plugin_loader.h"
#include "utils/cache.h"
#include "utils/vhost.h"

using namespace proxygen;

PluginManager::HookManagerImpl *plugin_loader = nullptr;
PluginManager::PluginManager *plugin_manager = nullptr;
thread_local cache::arc_cache<std::string, CacheRow *> tl_cache(100);

class HandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase * /*evb*/) noexcept override {
    }

    void onServerStop() noexcept override {
    }

    RequestHandler *onRequest(RequestHandler *requestHandler, HTTPMessage *message) noexcept override {
        const std::string hostname = message->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST);
        auto vhostAccessor = vhost::list.get(hostname);

        auto path = vhostAccessor->web_dir + message->getPath();
        auto file_metadata = vhost::file_metadata.get(path);
        if (file_metadata.has_value() && file_metadata->is_directory) {
            for (const auto &index_page: vhostAccessor->index_pages) {
                std::string index_path = path + index_page;
                if (vhost::file_metadata.get(index_path).has_value()) {
                    path = index_path; // Use the found index page
                    break;
                }
            }
        }

        return new StaticHandler(path, vhostAccessor->web_dir, &tl_cache);
    }
};


int main(int argc, char *argv[]) {
    auto _ = folly::Init(&argc, &argv, false);

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

    // if ((chdir("/")) < 0) {
    //     exit(EXIT_FAILURE);
    // }

#endif

    Config::GeneralConfig general_config(CONFIG_DIR);
    if (!general_config.load())
        return -1;

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
    if (!vhost::load(IPs))
        return -1;

    static PluginManager::HookManagerImpl hookManager;
    static PluginManager::PluginManager pluginMgr(hookManager);

    plugin_loader = &hookManager;
    plugin_manager = &pluginMgr;

    plugin_manager->loadPlugin("../plugins/php_plugin.so"); // TODO: add field in server.yaml for plugin path

    HTTPServerOptions options;
    options.threads = static_cast<size_t>(general_config.threads);
    options.idleTimeout = std::chrono::milliseconds(60000);
    options.shutdownOn = {SIGINT, SIGTERM, SIGSEGV};
    options.enableContentCompression = false;
    options.handlerFactories =
            RequestHandlerChain().addThen<HandlerFactory>().build();
    options.h2cEnabled = true;

    auto diskIOThreadPool = std::make_shared<folly::CPUThreadPoolExecutor>(
        general_config.threads,
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
