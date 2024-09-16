#include <folly/init/Init.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <syslog.h>

#include "handler.h"
#include "defines.h"
#include "config.h"
#include "vhost.h"

using namespace proxygen;

using folly::SocketAddress;

class StaticHandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase * /*evb*/) noexcept override {
    }

    void onServerStop() noexcept override {
    }

    RequestHandler *onRequest(RequestHandler *, HTTPMessage *b) noexcept override {
        return new StaticHandler(b->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST));
    }
};


int main(int argc, char *argv[]) {
    auto _ = folly::Init(&argc, &argv, true);

#ifndef DEBUG
    //thx for alexdlaird
    // Define variables
    pid_t pid, sid;

    // Fork the current process
    pid = fork();
    // The parent process continues with a process ID greater than 0
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
        // A process ID lower than 0 indicates a failure in either process
    else if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    // The parent process has now terminated, and the forked child process will continue
    // (the pid of the child process was 0)

    // Since the child process is a daemon, the umask needs to be set so files and logs can be written
    umask(0);

    // Open system logs for the child process
    openlog(DAEMON_NAME, LOG_NOWAIT | LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Successfully started" DAEMON_NAME);

    // Generate a session ID for the child process
    sid = setsid();
    // Ensure a valid SID for the child process
    if (sid < 0) {
        // Log failure and exit
        syslog(LOG_ERR, "Could not generate session ID for child process");

        // If a new session ID could not be generated, we must terminate the child process
        // or it will be orphaned
        exit(EXIT_FAILURE);
    }

    // Change the current working directory to a directory guaranteed to exist
    if ((chdir("/")) < 0) {
        // Log failure and exit
        syslog(LOG_ERR, "Could not change working directory to /");

        // If our guaranteed directory does not exist, terminate the child process to ensure
        // the daemon has not been hijacked
        exit(EXIT_FAILURE);
    }

    // A daemon cannot use the terminal, so close standard file descriptors for security reasons
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
            RequestHandlerChain().addThen<StaticHandlerFactory>().build();
    options.h2cEnabled = true;

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
    // Close system logs for the child process
    syslog(LOG_NOTICE, "Stopping" DAEMON_NAME);
    closelog();

    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
#endif

    return 0;
}
