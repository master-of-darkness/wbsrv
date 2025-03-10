#pragma once

#include <proxygen/httpserver/RequestHandler.h>
#include "common.h"

class ErrorHandler : public proxygen::RequestHandler {
public:
    // Constructor that accepts an HTTP status code
    explicit ErrorHandler(int errorCode): errorCode_(errorCode) {
    }

    // Override methods from RequestHandler
    void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override;

    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

    void onEOM() noexcept override;

    void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override;

    void requestComplete() noexcept override;

    void onError(proxygen::ProxygenError err) noexcept override;

private:
    int errorCode_; // Stores the HTTP error code
};
