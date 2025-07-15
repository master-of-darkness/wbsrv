#pragma once

#include <yaml-cpp/yaml.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <utility>

#include "cache.h"
#include "utils/utils.h"


namespace Config {
    class ServerConfig {
    public:
        explicit ServerConfig(std::string path) {
            path_ = std::move(path);
        }

        bool initialize();

        int threads = 0;

    private:
        std::string path_;
    };

    class VirtualHost {
    public:
        explicit VirtualHost(std::string path) {
            path_ = std::move(path);
        }

        bool initialize();

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

    inline std::unordered_map<std::string, Cache::VirtualHostConfig> virtual_hosts;
    inline std::unordered_map<std::string, Cache::FileSystemMetadata> files_metadata;

    bool load_virtual_host_configurations(std::vector<proxygen::HTTPServer::IPConfig> &ip_configs);
}
