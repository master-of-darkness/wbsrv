#include "handler/static.h"

#include <proxygen/httpserver/ResponseBuilder.h>

#include "defines.h"
#include "utils.h"

using namespace proxygen;

void StaticHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
    error_ = false;
    headers_ = std::move(headers);

    auto cache_acc = utils::cache.get(path_);

    // Check if the path is already cached
    if (cache_acc.has_value()) {
        const auto &cache_row = *cache_acc;
        if (cache_row->time_to_die <= std::chrono::steady_clock::now()) {
            utils::cache.remove(path_);
        } else {
            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", cache_row->content_type)
                    .body(cache_row->text)
                    .sendWithEOM();
            return;
        }
    }

    try {
        file_ = std::make_unique<folly::File>(path_);
    } catch (const std::system_error &ex) {
        ResponseBuilder(downstream_)
                .status(STATUS_404)
                .body(utils::getErrorPage(404))
                .sendWithEOM();
        return;
    }

    _temp_content_type = utils::getContentType(path_);
    ResponseBuilder(downstream_).status(STATUS_200).header("Content-Type", _temp_content_type).send();

    // Use a CPU executor since read(2) of a file can block
    readFileScheduled_ = true;
    folly::getUnsafeMutableGlobalCPUExecutor()->add(
        std::bind(&StaticHandler::readFile,
                  this,
                  folly::EventBaseManager::get()->getEventBase())
    );
}

void StaticHandler::readFile(folly::EventBase *evb) {
    folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());

    while (file_ && !paused_) {
        // read 4k-ish chunks and forward each one to the client
        auto data = buf.preallocate(4000, 4000);
        auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
        if (rc < 0) {
            // error
            file_.reset();
            evb->runInEventBaseThread([this] {
                LOG(ERROR) << "Error reading file";
                downstream_->sendAbort();
            });
            break;
        } else if (rc == 0) {
            // done
            file_.reset();
            evb->runInEventBaseThread([this] {
                if (!error_) {
                    CacheRow row;
                    row.content_type = _temp_content_type;
                    row.text = _temp_text;
                    row.time_to_die = std::chrono::steady_clock::now() + std::chrono::seconds(CACHE_TTL);
                    utils::cache.put(path_, std::make_shared<CacheRow>(row));
                    ResponseBuilder(downstream_).sendWithEOM();
                }
            });
            break;
        } else {
            buf.postallocate(rc);
            evb->runInEventBaseThread([this, body = buf.move()]() mutable {
                if (!error_) {
                    body->appendTo(_temp_text);
                    ResponseBuilder(downstream_).body(std::move(body)).send();
                }
            });
        }
    }

    // Notify the request thread that we terminated the readFile loop
    evb->runInEventBaseThread([this] {
        readFileScheduled_ = false;
        if (!checkForCompletion() && !paused_) {
            onEgressResumed();
        }
    });
}

void StaticHandler::onEgressPaused() noexcept {
    // This will terminate readFile soon
    paused_ = true;
}

void StaticHandler::onEgressResumed() noexcept {
    paused_ = false;
    // If readFileScheduled_, it will reschedule itself
    if (!readFileScheduled_ && file_) {
        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add(
            std::bind(&StaticHandler::readFile,
                      this,
                      folly::EventBaseManager::get()->getEventBase()));
    }
}

void StaticHandler::onEOM() noexcept {
}

void StaticHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
}

void StaticHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
    // todo: http3
}

void StaticHandler::requestComplete() noexcept {
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

void StaticHandler::onError(ProxygenError /*err*/) noexcept {
    error_ = true;
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

bool StaticHandler::checkForCompletion() {
    if (finished_ && !readFileScheduled_) {
        delete this;
        return true;
    }
    return false;
}
