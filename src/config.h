#pragma once

#include <yaml-cpp/yaml.h>

namespace Config {
    class GeneralConfig {
    public:
        explicit GeneralConfig(std::string path) {
            path_ = std::move(path);
        }

        bool load();

        int threads = 0;

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
}
