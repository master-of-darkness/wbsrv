#pragma once

#include <proxygen/httpserver/HTTPServer.h>

#include "utils/utils.h"

namespace vhost {
    extern utils::ConcurrentLRUCache<std::string, std::string> list;
    typedef utils::ConcurrentLRUCache<std::string, std::string>::ConstAccessor const_accessor;
    bool load(std::vector<proxygen::HTTPServer::IPConfig> &config);
}