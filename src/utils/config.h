#pragma once

#include <yaml-cpp/yaml.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <utility>
#include "utils/utils.h"
#include "cache.h"
#include "interface.h"

namespace Config {
    class ServerConfig {
    public:
        explicit ServerConfig(std::string path) {
            path_ = std::move(path);
        }

        bool load();

        int threads = 0;

        struct PluginConfig {
            std::string path;
            bool enabled{true};
            std::unordered_map<std::string, PluginManager::ConfigValue> parameters;
        };

        std::vector<PluginConfig> plugins;

    private:
        std::string path_;
    };

    class VirtualHost {
    public:
        explicit VirtualHost(std::string path) {
            path_ = std::move(path);
        }

        bool load();

        std::string private_key;
        std::string cert;
        std::string password;
        std::string hostname;
        std::string www_dir;
        std::vector<std::string> index_page;


        bool ssl = false;
        int port = 80;

    private:
        std::string path_;
    };


    bool load_virtual_host_configurations(std::vector<proxygen::HTTPServer::IPConfig> &ip_configs);
}
