#pragma once

// C++ Standard Library Includes
#include <string>
#include <memory>
#include <mutex>

// Proxygen Includes
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/lib/http/HTTPMessage.h>

#include "handler/common.h"

// PHP Embedding Includes
extern "C" {
#include <TSRM/TSRM.h>
#include <sapi/embed/php_embed.h>
}

namespace EmbedPHP
{
    void Initialize(int threads_expected);
    void executeScript(const std::string& path,
                       const std::unique_ptr<proxygen::HTTPMessage>& headers, std::string* message_body = nullptr,
                       proxygen::ResponseHandler* downstream_ = nullptr,  utils::ConcurrentLRUCache<std::string,
                       std::shared_ptr<CacheRow>>* cache = nullptr,
                       std::string web_root = "");
    void Shutdown();
}
