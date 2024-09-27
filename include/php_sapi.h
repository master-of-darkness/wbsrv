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
#include <sapi/embed/php_embed.h>
}

namespace EmbedPHP {
    void executeScript(const std::string &path, std::string &retval,
                       const std::unique_ptr<proxygen::HTTPMessage> &headers);
}
