#include <filesystem>

#include "vhost.h"
#include "defines.h"
#include "config.h"


utils::ConcurrentLRUCache<std::string, vhost::vinfo> vhost::list(256);

bool vhost::load(std::vector<proxygen::HTTPServer::IPConfig>& config)
{
    for (const auto& i : std::filesystem::directory_iterator(std::string(CONFIG_DIR) + "/hosts"))
    {
        if (i.path().extension() == ".yaml")
        {
            config::vhost host(i.path().string());
            if (host.load())
            {
                proxygen::HTTPServer::IPConfig vhost(folly::SocketAddress("0.0.0.0", host.port, false),
                                                     proxygen::HTTPServer::Protocol::HTTP);

                if (host.ssl)
                {
                    wangle::SSLContextConfig cert;
                    cert.setCertificate(host.cert, host.private_key, host.password);
                    cert.clientVerification = folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST;
                    vhost.sslConfigs.push_back(cert);
                    vhost.sslConfigs[0].isDefault = true;
                }


                list.insert(host.hostname + ':' + std::to_string(host.port),
                            vinfo(
                                host.web_dir,
                                host.index_pages
                            ));


                config.push_back(vhost);
            }
        }
    }
    return list.size() > 0 && !config.empty();
}
