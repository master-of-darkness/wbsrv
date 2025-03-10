#include "handler/error.h"
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>

using namespace proxygen;

void ErrorHandler::onRequest(std::unique_ptr<HTTPMessage>) noexcept {
}

void ErrorHandler::onBody(std::unique_ptr<folly::IOBuf>) noexcept {
}

void ErrorHandler::onEOM() noexcept {
    ResponseBuilder(downstream_)
            .status(errorCode_, "fail")
            .sendWithEOM();
}

void ErrorHandler::onUpgrade(UpgradeProtocol) noexcept {
}

void ErrorHandler::requestComplete() noexcept {
    delete this;
}

void ErrorHandler::onError(ProxygenError) noexcept {
    delete this;
}
