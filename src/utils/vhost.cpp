#include <filesystem>

#include "utils/vhost.h"
#include "utils/defines.h"
#include "config.h"

cache::arc_cache<std::string, vhost::vinfo> vhost::list(100);
cache::arc_cache<std::string, vhost::FileMetadata> vhost::file_metadata(1000);

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
                FileMetadata rootMeta{};
                rootMeta.is_directory = std::filesystem::is_directory(host.www_dir);
                file_metadata.put(host.www_dir + "/", rootMeta);

                if (rootMeta.is_directory) {
                    for (const auto &entry: std::filesystem::recursive_directory_iterator(host.www_dir)) {
                        FileMetadata meta{};
                        meta.is_directory = entry.is_directory();
                        file_metadata.put(entry.path().string(), meta);
                    }
                }

                if (!host.www_dir.empty() && host.www_dir.back() == '/') {
                    host.www_dir.pop_back();
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
