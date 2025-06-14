#include <filesystem>

#include "utils/vhost.h"
#include "utils/defines.h"
#include "config.h"


utils::ConcurrentLRUCache<std::string, vhost::vinfo> vhost::list(256);

bool vhost::load(std::vector<proxygen::HTTPServer::IPConfig> &config) {
    for (const auto &i: std::filesystem::directory_iterator(std::string(CONFIG_DIR) + "/hosts")) {
        if (i.path().extension() == ".yaml") {
            Config::VirtualHost host(i.path().string());
            if (host.load()) {
                proxygen::HTTPServer::IPConfig vhost(folly::SocketAddress("0.0.0.0", host.port, false),
                                                     proxygen::HTTPServer::Protocol::HTTP);

                if (host.ssl) {
                    wangle::SSLContextConfig cert;
                    cert.setCertificate(host.cert, host.private_key, host.password);
                    cert.clientVerification = folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST;
                    vhost.sslConfigs.push_back(cert);
                    vhost.sslConfigs[0].isDefault = true;
                }


                list.put(host.hostname + ':' + std::to_string(host.port),
                         vinfo(
                             host.www_dir,
                             host.index_page
                         ));

                if (std::ranges::find_if(config, [vhost](const proxygen::HTTPServer::IPConfig &item) {
                    return item.address == vhost.address;
                }) == config.end())
                    config.push_back(vhost);
            }
        }
    }
    return list.size() > 0 && !config.empty();
}
