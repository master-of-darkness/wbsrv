#pragma once

#include <proxygen/httpserver/HTTPServer.h>

#include <utility>

#include "utils/utils.h"

#include "cache.h"

namespace vhost {
    typedef struct vinfo_t {
        std::string web_dir;
        std::vector<std::string> index_pages;

        vinfo_t();

        vinfo_t(std::string web_dir, const std::vector<std::string> &index_pages): web_dir(std::move(web_dir)),
            index_pages(index_pages) {
        }
    } vinfo;

    struct FileMetadata {
        bool is_directory;
        // TODO: Add more metadata fields as needed
    };

    extern cache::arc_cache<std::string, vinfo> list;
    extern cache::arc_cache<std::string, FileMetadata> file_metadata;

    bool load(std::vector<proxygen::HTTPServer::IPConfig> &config);
}
