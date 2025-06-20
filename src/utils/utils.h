#pragma once

#include <folly/Range.h>
#include "utils/concurrent_cache.h"

struct CacheRow {
    bool operator!=(const CacheRow &rhs) const {
        return (content_type != rhs.content_type) || (text != rhs.text);
    }

    std::string content_type;
    std::string text;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::steady_clock::time_point time_to_die;
};

namespace utils {
    extern ConcurrentLRUCache<std::string, CacheRow> cache;

    const char *getContentType(const std::string &path);

    const char *getErrorPage(const int error);
}
