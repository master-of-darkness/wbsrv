#include "config/config.h"
#include "defines.h"

#include <yaml-cpp/yaml.h>

using namespace Config;

bool VHost::Load() {
    YAML::Node config = YAML::LoadFile(this->path_);
    if(!config.IsNull()){
        this->web_dir = config["www_dir"].as<std::string>();
        this->hostname = config["hostname"].as<std::string>();
        this->port = config["port"].as<int>();
        this->cert = config["certificate"].as<std::string>();
        this->private_key = config["private_key"].as<std::string>();
        this->password = config["password"].as<std::string>();

        return true;
    }
    return false;
}
