#pragma once

#include <proxygen/httpserver/HTTPServer.h>

#include "utils.h"

namespace vhost {
    typedef struct vinfo_t {
        std::string web_dir;
        std::vector<std::string> index_pages;
    } vinfo;

    extern utils::ConcurrentLRUCache<std::string, vinfo> list;
    typedef utils::ConcurrentLRUCache<std::string, vinfo>::ConstAccessor const_accessor;
    bool load(std::vector<proxygen::HTTPServer::IPConfig> &config);
}