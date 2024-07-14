#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Unistd.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <filesystem>
#include <fizz/server/CertManager.h>


#include "handler.h"
#include "defines.h"
#include "config/config.h"

using namespace proxygen;

using folly::SocketAddress;

using Protocol = HTTPServer::Protocol;

class StaticHandlerFactory : public RequestHandlerFactory {
public:
    void onServerStart(folly::EventBase* /*evb*/) noexcept override {
    }

    void onServerStop() noexcept override {
    }

    RequestHandler* onRequest(RequestHandler*, HTTPMessage*) noexcept override {
        return new StaticHandler;
    }
};

int main(int argc, char* argv[]) {
    auto _ = folly::Init(&argc, &argv, true);

    Config::General general_config(CONFIG_DIR);

    if(!general_config.Load())
        return -1;

    std::vector<HTTPServer::IPConfig> IPs;

    for(const auto& i: std::filesystem::directory_iterator(std::string(CONFIG_DIR) + "/hosts")) {
        if(i.path().extension() == ".yaml") {
            Config::VHost host(i.path().string());
            if(host.Load()) {
                virtual_hosts.Put(host.hostname+':'+std::to_string(host.port), host.web_dir);
                HTTPServer::IPConfig vhost(SocketAddress(host.hostname, host.port, true), Protocol::HTTP);
                wangle::SSLContextConfig cert;
                cert.setCertificate(host.cert, host.private_key, host.password);
                cert.clientVerification = folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST;
                vhost.sslConfigs.push_back(cert);
                vhost.sslConfigs[0].isDefault = true;
                IPs.push_back(vhost);
            }
        }
    }

    HTTPServerOptions options;
    options.threads = static_cast<size_t>(general_config.threads);
    options.idleTimeout = std::chrono::milliseconds(60000);
    options.shutdownOn = {SIGINT, SIGTERM};
    options.enableContentCompression = false;
    options.handlerFactories =
            RequestHandlerChain().addThen<StaticHandlerFactory>().build();
    options.h2cEnabled = true;

    auto diskIOThreadPool = std::make_shared<folly::CPUThreadPoolExecutor>(
            general_config.threads,
            std::make_shared<folly::NamedThreadFactory>("StaticDiskIOThread"));
    folly::setUnsafeMutableGlobalCPUExecutor(diskIOThreadPool);

    HTTPServer server(std::move(options));
    server.bind(IPs);
    // Start HTTPServer mainloop in a separate thread
    std::thread t([&]() { server.start(); });

    t.join();
    return 0;
}