#include "server.h"

#include <proxygen/httpserver/ResponseBuilder.h>

#include "utils/defines.h"
#include "utils/utils.h"
#include "ext/plugin_loader.h"

using namespace proxygen;

void StaticHandler::onRequest(std::unique_ptr<HTTPMessage> message) noexcept {
    error_ = false;
    headers_ = std::move(message);

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

    event_base_ = folly::EventBaseManager::get()->getEventBase();

    folly::getUnsafeMutableGlobalCPUExecutor()->add([this]() {
        HttpRequest constructed_request(
            (HttpMethod) headers_->getMethod().value(),
            headers_->getPath(),
            headers_->getQueryString(),
            std::make_unique<ProxygenHeadersAdapter>(headers_->getHeaders()),
            "",
            web_root_
        );
        auto response = plugin_loader->routeRequest(constructed_request);
        if (response.handled) {
            event_base_->runInEventBaseThread([this, response]() {
                if (!error_ && !finished_) {
                    ResponseBuilder(downstream_)
                            .status(response.statusCode, "Extension return")
                            .header("Content-Type", response.headers.at("Content-Type"))
                            .body(response.body)
                            .sendWithEOM();
                }
            });
        } else {
            try {
                file_ = std::make_unique<folly::File>(path_);
            } catch (const std::system_error &ex) {
                event_base_->runInEventBaseThread([this]() {
                    if (!error_ && !finished_) {
                        ResponseBuilder(downstream_)
                                .status(STATUS_404)
                                .body(utils::getErrorPage(404))
                                .sendWithEOM();
                    }
                });
                return;
            }

            _temp_content_type = utils::getContentType(path_);

            event_base_->runInEventBaseThread([this]() {
                if (!error_ && !finished_) {
                    ResponseBuilder(downstream_)
                            .status(STATUS_200)
                            .header("Content-Type", _temp_content_type)
                            .send();

                    readFileScheduled_ = true;
                    folly::getUnsafeMutableGlobalCPUExecutor()->add([this]() {
                        readFile();
                    });
                }
            });
        }
    });
}

void StaticHandler::readFile() {
    folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
    std::string accumulated_text;

    while (file_ && !paused_ && !error_ && !finished_) {
        // read 4k-ish chunks and forward each one to the client
        auto data = buf.preallocate(4000, 4000);
        auto rc = folly::readNoInt(file_->fd(), data.first, data.second);

        if (rc < 0) {
            // error
            file_.reset();
            event_base_->runInEventBaseThread([this] {
                if (!finished_) {
                    LOG(ERROR) << "Error reading file";
                    error_ = true;
                    downstream_->sendAbort();
                }
            });
            break;
        } else if (rc == 0) {
            // done - end of file
            file_.reset();
            event_base_->runInEventBaseThread([this, accumulated_text = std::move(accumulated_text)]() {
                if (!error_ && !finished_) {
                    // Cache the complete content
                    CacheRow row;
                    row.content_type = _temp_content_type;
                    row.text = accumulated_text;
                    row.time_to_die = std::chrono::steady_clock::now() + std::chrono::seconds(CACHE_TTL);
                    utils::cache.put(path_, std::make_shared<CacheRow>(row));

                    ResponseBuilder(downstream_).sendWithEOM();
                }
            });
            break;
        } else {
            buf.postallocate(rc);
            auto chunk = buf.move();
            chunk->appendTo(accumulated_text);

            // Send chunk to client
            event_base_->runInEventBaseThread([this, chunk = std::move(chunk)]() mutable {
                if (!error_ && !finished_) {
                    ResponseBuilder(downstream_).body(std::move(chunk)).send();
                }
            });
        }
    }

    // Notify that we're done with file reading
    event_base_->runInEventBaseThread([this] {
        readFileScheduled_ = false;
        if (!checkForCompletion() && !paused_ && !error_ && !finished_) {
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
    if (!readFileScheduled_ && file_ && !error_ && !finished_) {
        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add([this]() {
            readFile();
        });
    }
}

void StaticHandler::onEOM() noexcept {
    // Request body fully received
}

void StaticHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
    // Handle request body chunks
}

void StaticHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
    // TODO: Implement HTTP/3 upgrade
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