#include <memory>
#include <string>
#include <filesystem>
#include <dlfcn.h>

#include <folly/init/Init.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <proxygen/httpserver/HTTPServer.h>
#ifndef DEBUG
#include <syslog.h>
#endif

#include "handler/static.h"
#include "handler/sapi.h"
#include "handler/error.h"

#include "defines.h"
#include "config.h"
#include "vhost.h"
#include "php_sapi.h"
#include "plugin_manager.h"

using namespace proxygen;

utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow> > cache(256);
PluginManager manager;
bool useExt = false;
std::once_flag flag;

class HandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase * /*evb*/) noexcept override {
        std::call_once(flag, []() {
            // Plugin load
            for (const auto &i: std::filesystem::directory_iterator(std::string(CONFIG_DIR) + "/ext")) {
                if (i.path().extension() == ".so") {
                    if (manager.loadPlugin(i.path())) {
                        LOG(INFO) << "Loaded plugin " << i.path();
                        useExt = true;
                    } else
                        LOG(ERROR) << "Failed to load plugin " << i.path();
                }
            }
            // Plugin init
            for (size_t i = 0; i < manager.count(); ++i) {
                Plugin *p = manager.getPlugin(i);
                if (p) {
                    p->initialize();
                }
            }
        });
    }

    void onServerStop() noexcept override {
        std::call_once(flag, []() {
            // Plugin shutdown
            for (size_t i = 0; i < manager.count(); ++i) {
                Plugin *p = manager.getPlugin(i);
                if (p) {
                    p->shutdown();
                }
            }
            manager.unloadAllPlugins();
            EmbedPHP::Shutdown();
        });
        LOG(INFO) << "Shutdown requested";
    }

    RequestHandler *onRequest(RequestHandler *, HTTPMessage *message) noexcept override {
        const std::string hostname = message->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST);
        vhost::const_accessor vhostAccessor;
        if (!vhost::list.find(vhostAccessor, hostname)) {
            if (useExt) {
                for (size_t i = 0; i < manager.count(); ++i) {
                    Plugin *p = manager.getPlugin(i);
                    if (p) {
                        p->onEvent("Error", "-");
                    }
                }
            }
            return new ErrorHandler(400);
        }
        auto path = vhostAccessor->web_dir + message->getPath();

        if (std::filesystem::is_directory(path)) {
            for (const auto &index_page: vhostAccessor->index_pages) {
                std::string index_path = path + index_page;
                if (std::filesystem::exists(index_path)) {
                    path = index_path; // Use the found index page
                    break;
                }
            }
        }

        if (const size_t len = path.size(); path[len - 1] != 'p' && path[len - 2] != 'h' && path[len - 3] != 'p') {
            if (useExt) {
                for (size_t i = 0; i < manager.count(); ++i) {
                    Plugin *p = manager.getPlugin(i);
                    if (p) {
                        p->onEvent("Statica", "-");
                    }
                }
            }
            return new StaticHandler(path, &cache, &vhostAccessor);
        }

        if (useExt) {
            for (size_t i = 0; i < manager.count(); ++i) {
                Plugin *p = manager.getPlugin(i);
                if (p) {
                    p->onEvent("Engine", "-");
                }
            }
        }
        return new EngineHandler(path, &cache, vhostAccessor->web_dir);
    }
};


int main(int argc, char *argv[]) {
    auto _ = folly::Init(&argc, &argv, true);

#ifndef DEBUG
    pid_t pid, sid;

    pid = fork();
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }
    else if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    umask(0);

    openlog(DAEMON_NAME, LOG_NOWAIT | LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Successfully started" DAEMON_NAME);

    sid = setsid();
    if (sid < 0)
    {
        syslog(LOG_ERR, "Could not generate session ID for child process");
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0)
    {
        syslog(LOG_ERR, "Could not change working directory to /");

        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
#endif

    config::general general_config(CONFIG_DIR);
    if (!general_config.load())
        return -1;

    std::vector<HTTPServer::IPConfig> IPs;
    if (!vhost::load(IPs))
        return -1;


    HTTPServerOptions options;
    options.threads = static_cast<size_t>(general_config.threads);
    options.idleTimeout = std::chrono::milliseconds(60000);
    options.shutdownOn = {SIGINT, SIGTERM, SIGSEGV};
    options.enableContentCompression = false;
    options.handlerFactories =
            RequestHandlerChain().addThen<HandlerFactory>().build();
    options.h2cEnabled = true;

    EmbedPHP::Initialize(general_config.threads);

    auto diskIOThreadPool = std::make_shared<folly::CPUThreadPoolExecutor>(
        general_config.threads,
        std::make_shared<folly::NamedThreadFactory>("StaticDiskIOThread"));
    setUnsafeMutableGlobalCPUExecutor(diskIOThreadPool);

    HTTPServer server(std::move(options));
    server.bind(IPs);


    // Start HTTPServer mainloop in a separate thread
    std::thread t([&]() { server.start(); });

    t.join();

#ifndef DEBUG
    syslog(LOG_NOTICE, "Stopping" DAEMON_NAME);
    closelog();
    exit(EXIT_SUCCESS);
#endif

    return 0;
}
