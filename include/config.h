#pragma once

#include <string>
#include <utility>
#include <vector>

namespace config {

    class general {
    public:
        explicit general(std::string path) {
            path_ = std::move(path);
        }

        bool load();

        int threads = 0;
    private:
        std::string path_;
    };

    class vhost {
    public:
        explicit vhost(std::string path) {
            path_ = std::move(path);
        }

        bool load();

        std::string private_key;
        std::string cert;
        std::string password;
        std::string hostname;
        std::string web_dir;
        std::vector<std::string> index_pages; //name, cgi (true : false)
        bool ssl = false;

        int port = 80;
    private:
        std::string path_;
    };
}
