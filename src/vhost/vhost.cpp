#include <filesystem>

#include "vhost/vhost.h"
#include "defines.h"
#include "config/config.h"

utils::ConcurrentLRUCache<std::string, std::string> vhost::list(256);

bool vhost::load(std::vector<proxygen::HTTPServer::IPConfig> &config) {
    for (const auto &i: std::filesystem::directory_iterator(std::string(CONFIG_DIR) + "/hosts")) {
        if (i.path().extension() == ".yaml") {
            config::vhost host(i.path().string());
            if (host.load()) {
                list.insert(host.hostname + ':' + std::to_string(host.port), host.web_dir);
                proxygen::HTTPServer::IPConfig vhost(folly::SocketAddress(host.hostname, host.port, true), proxygen::HTTPServer::Protocol::HTTP);

                if (host.ssl) {
                    wangle::SSLContextConfig cert;
                    cert.setCertificate(host.cert, host.private_key, host.password);
                    cert.clientVerification = folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST;
                    vhost.sslConfigs.push_back(cert);
                    vhost.sslConfigs[0].isDefault = true;
                }

                config.push_back(vhost);
            }
        }
    }
    return list.size() > 0 && !config.empty();
}
