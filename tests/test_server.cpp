#include <memory>
#include <string>
#include <filesystem>
#include <thread>
#include <sys/resource.h>
#include <sched.h>

#include <folly/init/Init.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <proxygen/httpserver/HTTPServer.h>
#ifndef DEBUG
#include <syslog.h>
#endif

using namespace proxygen;

#include <folly/portability/GFlags.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>

class EchoHandler : public RequestHandler {

public:
    explicit EchoHandler() = default;

    void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {
        ResponseBuilder(downstream_)
            .status(200, "OK")
            .header(HTTP_HEADER_CONTENT_TYPE, "text/plain")
            .body("fddfsdfs")
            .sendWithEOM();
    }

    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    }

    void onEOM() noexcept override {
    }

    void onUpgrade(proxygen::UpgradeProtocol /*proto*/) noexcept override {
    }

    void requestComplete() noexcept override {
        delete this;
    }

    void onError(proxygen::ProxygenError /*err*/) noexcept override {
        delete this;
    }
};

class HandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase * /*evb*/) noexcept override {
        // Could add handler pool here if needed
    }

    void onServerStop() noexcept override {
        // Cleanup
    }

    RequestHandler *onRequest(RequestHandler *, HTTPMessage *) noexcept override {
        return new EchoHandler();
    }
};

int main(int argc, char *argv[]) {
    // System optimizations
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    const int num_cores = 6;
    for (int i = 0; i < num_cores; ++i) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpuset), &cpuset);

    // Set high priority (requires appropriate permissions)
    setpriority(PRIO_PROCESS, 0, -5);

    auto _ = folly::Init(&argc, &argv, false);

    // Bind to all interfaces with optimized settings
    std::vector<HTTPServer::IPConfig> IPs = {
        {folly::SocketAddress("0.0.0.0", 8080, true), HTTPServer::Protocol::HTTP},
    };

    HTTPServerOptions options;

    // Thread optimization for i7-12700H (20 logical cores)
    options.threads = num_cores; // Use all available cores

    // Connection and timeout optimizations
    options.idleTimeout = std::chrono::milliseconds(30000);
    options.listenBacklog = 2048;

    // Protocol optimizations
    options.enableContentCompression = false; // Disable for simple responses
    options.h2cEnabled = true;
    options.supportsConnect = false;

    // Signal handling
    options.shutdownOn = {SIGINT, SIGTERM, SIGSEGV};

    // Handler setup
    options.handlerFactories = RequestHandlerChain()
        .addThen<HandlerFactory>()
        .build();

    HTTPServer server(std::move(options));
    server.bind(IPs);

    // Start server in separate thread
    std::thread server_thread([&]() {
        server.start();
    });

    // Keep main thread alive
    server_thread.join();

    return 0;
}