#include "config.h"
#include "defines.h"

#include <yaml-cpp/yaml.h>

using namespace config;

bool general::load() {
    YAML::Node config = YAML::LoadFile(this->path_ + "/server.yaml");
    if (!config.IsNull()) {
        this->threads = config["threads"].as<int>();
        return true;
    }
    return false;
}

bool vhost::load() {
    YAML::Node config = YAML::LoadFile(this->path_);
    if (!config.IsNull()) {
        this->web_dir = config["www_dir"].as<std::string>();
        this->hostname = config["hostname"].as<std::string>();
        this->ssl = config["ssl"].as<bool>();
        this->port = config["port"].as<int>();
        this->cert = config["certificate"].as<std::string>();
        this->private_key = config["private_key"].as<std::string>();
        this->password = config["password"].as<std::string>();
        this->index_pages = config["index_page"].as<std::vector<std::string> >();
        return true;
    }
    return false;
}
