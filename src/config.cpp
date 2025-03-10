#include "config.h"

#include <glog/logging.h>
using namespace config;

bool general::load() {
    try {
        YAML::Node config = YAML::LoadFile(this->path_ + "/server.yaml");
        if (!config.IsNull()) {
            this->threads = config["threads"].as<int>();
            return true;
        }
        return false;
    } catch (const YAML::BadFile &) {
        LOG(ERROR) << "Can't open " << this->path_ + "/server.yaml" << ". Ensure that file exists.";
        return false;
    }
}

/**
 * @brief Loads the virtual host configuration.
 *
 * This function is responsible for loading the virtual host
 * configuration from the specified files and initializing
 * the necessary components.
 *
 * @param configPath The path to the configuration file.
 * @return true if the configuration is successfully loaded, false otherwise.
 */
bool vhost::load() {
    YAML::Node config = YAML::LoadFile(this->path_);
    if (!config.IsNull()) {
        this->www_dir = config["www_dir"].as<std::string>();
        this->hostname = config["hostname"].as<std::string>();
        this->ssl = config["ssl"].as<bool>();
        this->port = config["port"].as<int>();
        if (ssl) {
            this->cert = config["certificate"].as<std::string>();
            this->private_key = config["private_key"].as<std::string>();
            this->password = config["password"].as<std::string>();
        }
        this->index_page = config["index_page"].as<std::vector<std::string> >();
        return true;
    }
    return false;
}
