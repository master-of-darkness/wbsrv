#include <memory>
#include <string>
#include <filesystem>

#include <folly/init/Init.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <proxygen/httpserver/HTTPServer.h>
#ifndef DEBUG
#include <syslog.h>
#endif

#include "server/server.h"

#include "utils/defines.h"
#include "config.h"
#include "ext/plugin_loader.h"
#include "utils/vhost.h"

using namespace proxygen;

PluginManager::HookManagerImpl* plugin_loader = nullptr;
PluginManager::PluginManager* plugin_manager = nullptr;

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
        std::unordered_map<std::string, std::string> headers;

        if (std::filesystem::is_directory(path)) {
            for (const auto &index_page: vhostAccessor->index_pages) {
                std::string index_path = path + index_page;
                if (std::filesystem::exists(index_path)) {
                    path = index_path; // Use the found index page
                    break;
                }
            }
        }

        return new StaticHandler(path, vhostAccessor->web_dir);
    }
};


int main(int argc, char *argv[]) {
    auto _ = folly::Init(&argc, &argv, true);

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
