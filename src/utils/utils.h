#pragma once

#include <chrono>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>

#include "utils/cache.h"

struct CacheRow {
    bool operator!=(const CacheRow &rhs) const {
        return size != rhs.size;
    }

    std::string content_type;
    std::shared_ptr<folly::IOBuf> data;
    size_t size;

    CacheRow(std::string &&type,
             std::unique_ptr<folly::IOBuf> &&data_ptr)
        : content_type(std::move(type)),
          data(std::move(data_ptr)),
          size(data->computeChainDataLength()) {
    }
};

namespace utils {
    const char *getContentType(const std::string &path);

    const char *getErrorPage(const int error);
}
