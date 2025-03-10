#include "handler/sapi.h"

#include <proxygen/httpserver/ResponseBuilder.h>
#include <filesystem>

#include "defines.h"
#include "utils.h"
#include "php_sapi.h"

using namespace proxygen;

void EngineHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
    error_ = false;
    headers_ = std::move(headers);
}

void EngineHandler::onEgressPaused() noexcept {
    // This will terminate readFile soon
    paused_ = true;
}

void EngineHandler::onEgressResumed() noexcept {
    paused_ = false;
}

void EngineHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
    // todo: http3
}

void EngineHandler::requestComplete() noexcept {
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

void EngineHandler::onError(ProxygenError /*err*/) noexcept {
    error_ = true;
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

void EngineHandler::onEOM() noexcept {
    auto cache_acc = utils::cache.get(path_);

    if (!cache_acc.has_value()) {
        if (!messageBody.empty())
            EmbedPHP::executeScript(path_, headers_, &messageBody, downstream_, web_root_);
        else
            EmbedPHP::executeScript(path_, headers_, nullptr, downstream_, web_root_);
    } else {
        const auto &cache_row = *cache_acc;
        ResponseBuilder builder(downstream_);
        builder.status(200, "OK");
        for (const auto &header: cache_row->headers)
            builder.header(header.first, header.second);

        builder.body(cache_row->text).sendWithEOM();
    }
}

void EngineHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
    messageBody.append(body->toString());
}

bool EngineHandler::checkForCompletion() {
    if (finished_ && !readFileScheduled_) {
        delete this;
        return true;
    }
    return false;
}
