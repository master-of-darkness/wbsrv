#pragma once

// C++ Standard Library Includes
#include <string>
#include <memory>
#include <mutex>

// Proxygen Includes
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/lib/http/HTTPMessage.h>

// PHP Embedding Includes
extern "C" {
#include <TSRM/TSRM.h>
#include <sapi/embed/php_embed.h>
}

namespace EmbedPHP {
    void Initialize(int threads_expected);
    void executeScript(const std::string &path,
                       const std::unique_ptr<proxygen::HTTPMessage> &headers, std::string *message_body = nullptr, proxygen::ResponseHandler* downstream_ = nullptr);
    void Shutdown();
}
