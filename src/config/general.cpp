#include "config/config.h"
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
