#include "config/config.h"
#include "defines.h"

#include <yaml-cpp/yaml.h>

using namespace Config;

bool General::Load() {
    YAML::Node config = YAML::LoadFile(this->path_ + "/server.yaml");
    if(!config.IsNull()) {
        this->host = config["host"].as<std::string>();
        this->http2 = config["http2"].as<bool>();
        this->threads = config["threads"].as<int>();
        return true;
    }
    return false;
}
