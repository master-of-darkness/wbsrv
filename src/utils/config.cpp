#include "config.h"

#include <glog/logging.h>
#include <filesystem>
#include <folly/logging/xlog.h>
#include <sys/syslog.h>
#include <folly/logging/xlog.h>

#include "server/core.h"
#include "utils/defines.h"

using namespace folly;
using namespace Config;

bool ServerConfig::initialize() {
    try {
        YAML::Node config = YAML::LoadFile(path_ + "/server.yaml");
        if (!config.IsNull()) {
            threads = config["threads"].as<int>();
            return true;
        }
        return false;
    } catch (const YAML::BadFile &) {
        XLOG(ERR) << "Can't open " << path_ + "/server.yaml" << ". Ensure that file exists.";
        return false;
    }
}


bool VirtualHost::initialize() {
    YAML::Node config = YAML::LoadFile(path_);
    if (!config.IsNull()) {
        www_dir = config["www_dir"].as<std::string>();
        hostname = config["hostname"].as<std::string>();
        ssl = config["ssl"].as<bool>();
        port = config["port"].as<int>();
        if (ssl) {
            cert = config["certificate"].as<std::string>();
            private_key = config["private_key"].as<std::string>();
            password = config["password"].as<std::string>();
        }
        index_page = config["index_page"].as<std::vector<std::string> >();
        return true;
    }
    return false;
}

bool Config::load_virtual_host_configurations(std::vector<proxygen::HTTPServer::IPConfig> &config) {
    for (const auto &i: std::filesystem::directory_iterator(std::string(CONFIG_DIR) + "/hosts")) {
        if (i.path().extension() == ".yaml") {
            Config::VirtualHost host(i.path().string());
            if (host.initialize()) {
                proxygen::HTTPServer::IPConfig vhost(folly::SocketAddress("0.0.0.0", host.port, false),
                                                     proxygen::HTTPServer::Protocol::HTTP);


                if (host.ssl) {
                    wangle::SSLContextConfig cert;
                    cert.setCertificate(host.cert, host.private_key, host.password);
                    cert.clientVerification = folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST;
                    vhost.sslConfigs.push_back(cert);
                    vhost.sslConfigs[0].isDefault = true;
                }
                Cache::FileSystemMetadata rootMeta{};
                rootMeta.is_directory = std::filesystem::is_directory(host.www_dir);
                files_metadata[host.www_dir + "/"] = rootMeta;

                if (rootMeta.is_directory) {
                    for (const auto &entry: std::filesystem::recursive_directory_iterator(host.www_dir)) {
                        Cache::FileSystemMetadata meta{};
                        meta.is_directory = entry.is_directory();
                        files_metadata[entry.path().string()] = meta;
                    }
                }

                if (!host.www_dir.empty() && host.www_dir.back() == '/') {
                    host.www_dir.pop_back();
                }

                virtual_hosts[host.hostname + ':' + std::to_string(host.port)] = Cache::VirtualHostConfig(
                    host.www_dir,
                    host.index_page
                );

                if (std::ranges::find_if(config, [vhost](const proxygen::HTTPServer::IPConfig &item) {
                    return item.address == vhost.address;
                }) == config.end())
                    config.push_back(vhost);
            }
        }
    }
    return !virtual_hosts.empty() && !config.empty();
}
