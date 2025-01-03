#pragma once

#include <string>
#include "utils.h"
#include <atomic>
#include <utility>
#include <folly/File.h>
#include <proxygen/httpserver/RequestHandler.h>

struct CacheRow
{
    bool operator!=(const CacheRow& rhs) const
    {
        return (content_type != rhs.content_type) || (text != rhs.text);
    }

    std::string content_type;
    std::string text;
    std::unordered_map<std::string, std::string> headers;
};

typedef utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>::ConstAccessor CacheAccessor;

namespace proxygen
{
    class ResponseHandler;
}
