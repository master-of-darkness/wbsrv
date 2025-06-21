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
    if (cache_acc && headers_->getMethod() == HTTPMethod::GET) {
        const auto &cache_row = *cache_acc;
        if (cache_row.time_to_die <= std::chrono::steady_clock::now()) {
            utils::cache.remove(path_);
        } else {
            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", cache_row.content_type)
                    .body(cache_row.text)
                    .sendWithEOM();
            handled_from_cache_ = true;
            return;
        }
    }
    cached_content_type_ = utils::getContentType(path_);
    event_base_ = folly::EventBaseManager::get()->getEventBase();
}

void StaticHandler::processRequest() {
    if (!headers_) return;

    auto method_opt = headers_->getMethod();
    if (!method_opt.has_value()) return;

    HttpMethod method = static_cast<HttpMethod>(method_opt.value());
    std::string query = headers_->getQueryString();

    const size_t len = path_.size();
    bool is_php = len >= 4 && path_.compare(len - 4, 4, ".php") == 0;

    folly::getUnsafeMutableGlobalCPUExecutor()->add([this, method, query = std::move(query), is_php]() {
        if (is_php) {
            handlePHPRequest(method, query);
        } else {
            handleStaticFile();
        }
    });
}

void StaticHandler::handlePHPRequest(HttpMethod method, const std::string& query) const {
    HttpRequest req(
        method, headers_->getPath(), query,
        std::make_unique<ProxygenHeadersAdapter>(headers_->getHeaders()),
        std::make_unique<FollyBodyImpl>(body_ ? body_->clone() : folly::IOBuf::create(0)),
        web_root_
    );

    auto response = plugin_loader->routeRequest(&req);
    if (!response.handled) return;

    event_base_->runInEventBaseThread([this, response = std::move(response)]() {
        if (error_ || finished_) return;

        ResponseBuilder(downstream_)
            .status(response.statusCode, "Extension return")
            .header("Content-Type", response.headers.at("Content-Type"))
            .body(response.body)
            .sendWithEOM();
    });
}

void StaticHandler::handleStaticFile() {
    try {
        file_ = std::make_unique<folly::File>(path_);
    } catch (const std::system_error&) {
        event_base_->runInEventBaseThread([this]() {
            if (error_ || finished_) return;

            ResponseBuilder(downstream_)
                .status(STATUS_404)
                .body(utils::getErrorPage(404))
                .sendWithEOM();
        });
        return;
    }

    event_base_->runInEventBaseThread([this]() {
        if (error_ || finished_) return;

        ResponseBuilder(downstream_)
            .status(STATUS_200)
            .header("Content-Type", cached_content_type_)
            .send();

        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add([this]() {
            readFile();
        });
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
                    row.content_type = cached_content_type_;
                    row.text = accumulated_text;
                    row.time_to_die = std::chrono::steady_clock::now() + std::chrono::seconds(CACHE_TTL);
                    utils::cache.put(path_, row);

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
    if (!handled_from_cache_)
        processRequest();
}

void StaticHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
    if (body_) {
        body_->prependChain(std::move(body));
    } else {
        body_ = std::move(body);
    }
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
