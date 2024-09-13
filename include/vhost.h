#pragma once

#include <proxygen/httpserver/HTTPServer.h>

#include <utility>

#include "utils.h"

namespace vhost {
    typedef struct vinfo_t {
        std::string web_dir;
        std::vector<std::string> index_pages;
        bool fastcgi_enable = false;
        std::vector<std::string> cgi_extensions;
        std::string cgi_host;
        int cgi_port = -1;

        vinfo_t();

        vinfo_t(std::string web_dir, const std::vector<std::string> &index_pages, const bool fcgi): // fcgi: false
        web_dir(std::move(web_dir)), index_pages(index_pages), fastcgi_enable(fcgi) {}

        vinfo_t(
            std::string web_dir,
            const std::vector<std::string> &index_pages,
            const bool fcgi,
            std::vector<std::string> cgi_ext,
            std::string cgi_host,
            int cgi_port
            ):  // fcgi: true
        web_dir(std::move(web_dir)),
        index_pages(index_pages),
        fastcgi_enable(fcgi),
        cgi_extensions(std::move(cgi_ext)),
        cgi_host(std::move(cgi_host)),
        cgi_port(cgi_port) {}
    } vinfo;

    extern utils::ConcurrentLRUCache<std::string, vinfo> list;
    typedef utils::ConcurrentLRUCache<std::string, vinfo>::ConstAccessor const_accessor;
    bool load(std::vector<proxygen::HTTPServer::IPConfig> &config);
}