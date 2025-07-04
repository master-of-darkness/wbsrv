#include "config.h"

#include <glog/logging.h>
#include <filesystem>
#include <sys/syslog.h>

#include "server/server.h"
#include "utils/defines.h"

using namespace Config;

PluginManager::ConfigValue yamlNodeToConfigValue(const YAML::Node &node) {
    if (node.IsScalar()) {
        auto str = node.as<std::string>();

        // Check for boolean values first
        if (str == "true" || str == "false" || str == "True" || str == "False" ||
            str == "TRUE" || str == "FALSE" || str == "yes" || str == "no" ||
            str == "Yes" || str == "No" || str == "YES" || str == "NO") {
            try {
                return PluginManager::ConfigValue(node.as<bool>());
            } catch (...) {
                // If conversion fails, treat as string
                return PluginManager::ConfigValue(str);
            }
        }
        try {
            if (str.find('.') == std::string::npos && str.find('e') == std::string::npos &&
                str.find('E') == std::string::npos) {
                int64_t intVal = node.as<int64_t>();
                return PluginManager::ConfigValue(intVal);
                }
        } catch (...) {
            // If conversion fails, continue to check for double
        }

        try {
            double doubleVal = node.as<double>();
            if (str.find_first_not_of("0123456789.-+eE") == std::string::npos) {
                return PluginManager::ConfigValue(doubleVal);
            }
        } catch (...) {
        }

        // Default to string
        return PluginManager::ConfigValue(str);
    }

    return PluginManager::ConfigValue();
}


bool ServerConfig::load() {
    try {
        YAML::Node config = YAML::LoadFile(path_ + "/server.yaml");
        if (!config.IsNull()) {
            threads = config["threads"].as<int>();
            if (config["plugins"] && config["plugins"].IsSequence()) {
                for (const auto &plugin_node: config["plugins"]) {
                    PluginConfig plugin;
                    plugin.path = plugin_node["path"].as<std::string>();

                    // If not specified, default to true
                    plugin.enabled = plugin_node["enabled"] ? plugin_node["enabled"].as<bool>() : true;

                    if (plugin_node["config"] && plugin_node["config"].IsMap()) {
                        for (const auto &param: plugin_node["config"]) {
                            plugin.parameters[param.first.as<std::string>()] =
                                    yamlNodeToConfigValue(param.second);
                        }
                    }

                    plugins.push_back(plugin);
                }
            }
            return true;
        }
        return false;
    } catch (const YAML::BadFile &) {
        LOG(ERROR) << "Can't open " << path_ + "/server.yaml" << ". Ensure that file exists.";
        return false;
    }
}


bool VirtualHost::load() {
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
                Cache::FileSystemMetadata rootMeta{};
                rootMeta.is_directory = std::filesystem::is_directory(host.www_dir);
                Cache::file_metadata_cache.put(host.www_dir + "/", rootMeta);

                if (rootMeta.is_directory) {
                    for (const auto &entry: std::filesystem::recursive_directory_iterator(host.www_dir)) {
                        Cache::FileSystemMetadata meta{};
                        meta.is_directory = entry.is_directory();
                        Cache::file_metadata_cache.put(entry.path().string(), meta);
                    }
                }

                if (!host.www_dir.empty() && host.www_dir.back() == '/') {
                    host.www_dir.pop_back();
                }

                Cache::host_config_cache.put(host.hostname + ':' + std::to_string(host.port),
                                             Cache::VirtualHostConfig(
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
    return !Cache::host_config_cache.empty() && !config.empty();
}
