#include "server.h"

#include <proxygen/httpserver/ResponseBuilder.h>

#include "utils/defines.h"
#include "utils/utils.h"
#include "ext/plugin_loader.h"
#include "../include/interface.h"

using namespace proxygen;

void convertHeaders(const proxygen::HTTPHeaders &headers,
                    std::unordered_map<std::string, std::string> &result) {
    result.clear();
    result.reserve(headers.size());

    headers.forEach([&result](const std::string &name, const std::string &value) {
        result.emplace(name, value);
    });
}

std::string convertBodyToString(const std::unique_ptr<folly::IOBuf> &body) {
    if (!body) return "";

    std::string result;
    result.reserve(body->computeChainDataLength());

    for (const auto &buf: *body) {
        result.append(reinterpret_cast<const char *>(buf.data()), buf.size());
    }

    return result;
}

std::string getClientIP(const std::unique_ptr<HTTPMessage> &headers) {
    auto clientIP = headers->getHeaders().getSingleOrEmpty("X-Forwarded-For");
    if (!clientIP.empty()) {
        auto commaPos = clientIP.find(',');
        if (commaPos != std::string::npos) {
            clientIP = clientIP.substr(0, commaPos);
        }
        return clientIP;
    }

    clientIP = headers->getHeaders().getSingleOrEmpty("X-Real-IP");
    if (!clientIP.empty()) {
        return clientIP;
    }

    return "127.0.0.1"; // Default for now
}

void StaticHandler::onRequest(std::unique_ptr<HTTPMessage> message) noexcept {
    headers_ = std::move(message);
    auto cache_acc = cache_->get(path_);

    // Check if the path is already cached
    if (cache_acc.has_value() && headers_->getMethod() == HTTPMethod::GET) {
        const auto &cache_row = cache_acc.value();
        ResponseBuilder(downstream_)
                .status(STATUS_200)
                .header(HTTP_HEADER_CONTENT_TYPE, cache_row->content_type)
                .body(cache_row->data->clone())
                .sendWithEOM();
        handled_from_cache_ = true;
        return;
    }
    error_ = false;
    cached_content_type_ = utils::getContentType(path_);
    event_base_ = folly::EventBaseManager::get()->getEventBase();
}

void StaticHandler::processRequest() {
    if (!headers_) return;

    auto method_opt = headers_->getMethod();
    if (!method_opt.has_value()) return;

    PluginManager::HttpMethod method = PluginManager::ProxygenBridge::convertMethod(method_opt.value());
    std::string query = headers_->getQueryString();

    folly::getUnsafeMutableGlobalCPUExecutor()->add([this, method, query = std::move(query)]() {
        if (!handleHooks(method, query)) handleStaticFile();
    });
}

bool StaticHandler::handleHooks(PluginManager::HttpMethod method, const std::string &query) const {
    PluginManager::HttpRequest request;
    request.method = method;
    request.path = headers_->getPath();
    request.query = query;
    convertHeaders(headers_->getHeaders(), request.headers);
    request.body = convertBodyToString(body_);
    request.clientIP = getClientIP(headers_);

    PluginManager::HttpResponse response;

    PluginManager::RequestContext context;
    context.request = &request;
    context.response = &response;
    context.requestId = reinterpret_cast<uint64_t>(this); // Use handler address as unique ID
    context.startTime = std::chrono::steady_clock::now();

    context.setMetadata("document_root", web_root_);

    bool handled = false;
    if (plugin_loader->executeHooks(PluginManager::HookType::PRE_REQUEST, context)) {
        if (plugin_loader->executeHooks(PluginManager::HookType::POST_REQUEST, context)) {
            plugin_loader->executeHooks(PluginManager::HookType::PRE_RESPONSE, context);
            handled = true;
        }
    }

    if (!handled || response.statusCode == 0)
        return false;


    event_base_->runInEventBaseThread([this, response = std::move(response)]() {
        if (error_ || finished_) return;

        auto rb = ResponseBuilder(downstream_);
        rb.status(response.statusCode, response.statusMessage);

        for (const auto &header: response.headers) {
            rb.header(header.first, header.second);
        }

        rb.body(response.body);
        rb.sendWithEOM();
    });
    plugin_loader->executeHooks(PluginManager::HookType::POST_RESPONSE, context);
    return true;
}

void StaticHandler::handleStaticFile() {
    try {
        file_ = std::make_unique<folly::File>(path_);
    } catch (const std::system_error &) {
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

void StaticHandler::readFile() const {
    folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
    std::vector<std::unique_ptr<folly::IOBuf> > chunks;

    while (file_ && !paused_ && !error_ && !finished_) {
        auto data = buf.preallocate(4000, 4000);
        auto rc = folly::readNoInt(file_->fd(), data.first, data.second);

        if (rc < 0) {
            // handle error
            break;
        } else if (rc == 0) {
            // EOF - cache the complete chain
            if (!chunks.empty()) {
                // Build complete chain
                auto complete_buf = folly::IOBuf::create(0);
                for (auto &chunk: chunks) {
                    complete_buf->prependChain(std::move(chunk));
                }

                CacheRow row(cached_content_type_.data(), std::move(complete_buf));

                CacheRow *row_ptr = new CacheRow(std::move(row));
                cache_->put(path_, row_ptr);
            }

            event_base_->runInEventBaseThread([this]() {
                ResponseBuilder(downstream_).sendWithEOM();
            });
            break;
        } else {
            buf.postallocate(rc);
            auto chunk = buf.move();

            // Store clone for caching, send original
            chunks.push_back(chunk->clone());

            event_base_->runInEventBaseThread([this, chunk = std::move(chunk)]() mutable {
                if (!error_ && !finished_) {
                    ResponseBuilder(downstream_).body(std::move(chunk)).send();
                }
            });
        }
    }
}

void StaticHandler::onEgressPaused() noexcept {
    paused_ = true;
}

void StaticHandler::onEgressResumed() noexcept {
    paused_ = false;
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
