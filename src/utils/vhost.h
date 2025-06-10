#pragma once

#include <proxygen/httpserver/HTTPServer.h>

#include <utility>

#include "utils/utils.h"

namespace vhost {
    typedef struct vinfo_t {
        std::string web_dir;
        std::vector<std::string> index_pages;
        bool fastcgi_enable = false;

        vinfo_t();

        vinfo_t(std::string web_dir, const std::vector<std::string> &index_pages): // fcgi: false
            web_dir(std::move(web_dir)), index_pages(index_pages) {
        }
    } vinfo;

    extern utils::ConcurrentLRUCache<std::string, vinfo> list;

    bool load(std::vector<proxygen::HTTPServer::IPConfig> &config);
}
