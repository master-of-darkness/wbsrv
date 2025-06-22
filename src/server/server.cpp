#include "server.h"

#include <proxygen/httpserver/ResponseBuilder.h>

#include "utils/defines.h"
#include "utils/utils.h"
#include "ext/plugin_loader.h"
#include "../include/interface.h"

using namespace proxygen;

std::unordered_map<std::string, std::string> convertHeaders(const proxygen::HTTPHeaders& headers) {
    std::unordered_map<std::string, std::string> result;
    headers.forEach([&result](const std::string& name, const std::string& value) {
        result[name] = value;
    });
    return result;
}

std::string convertBodyToString(const std::unique_ptr<folly::IOBuf>& body) {
    if (!body) return "";

    std::string result;
    result.reserve(body->computeChainDataLength());

    for (const auto& buf : *body) {
        result.append(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    return result;
}

std::string getClientIP(const std::unique_ptr<HTTPMessage>& headers) {
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
    error_ = false;
    headers_ = std::move(message);

    auto cache_acc = utils::cache.get(path_);

    const size_t len = path_.size();
    bool is_php = len >= 4 && path_.compare(len - 4, 4, ".php") == 0;

    // Check if the path is already cached
    if (cache_acc && headers_->getMethod() == HTTPMethod::GET && !is_php) {
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

    PluginManager::HttpMethod method = PluginManager::ProxygenBridge::convertMethod(method_opt.value());
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

void StaticHandler::handlePHPRequest(PluginManager::HttpMethod method, const std::string& query) const {
    // Create the HttpRequest according to the interface
    PluginManager::HttpRequest request;
    request.method = method;
    request.path = headers_->getPath();
    request.query = query;
    request.headers = convertHeaders(headers_->getHeaders());
    request.body = convertBodyToString(body_);
    request.clientIP = getClientIP(headers_);

    // Create HttpResponse
    PluginManager::HttpResponse response;

    // Create RequestContext
    PluginManager::RequestContext context;
    context.request = &request;
    context.response = &response;
    context.requestId = reinterpret_cast<uint64_t>(this); // Use handler address as unique ID
    context.startTime = std::chrono::steady_clock::now();

    context.setMetadata("document_root", web_root_);

    bool handled = false;
    if (plugin_loader) {
        if (plugin_loader->executeHooks(PluginManager::HookType::PRE_REQUEST, context)) {
            if (plugin_loader->executeHooks(PluginManager::HookType::POST_REQUEST, context)) {
                plugin_loader->executeHooks(PluginManager::HookType::PRE_RESPONSE, context);
                handled = true;
            }
        }
    }

    if (!handled) {
        response.setStatus(404, "Not Found");
        response.setTextContent();
        response.body = utils::getErrorPage(404);
    }

    event_base_->runInEventBaseThread([this, response = std::move(response), context]() {
        if (error_ || finished_) return;

        auto rb = ResponseBuilder(downstream_);
        rb.status(response.statusCode, response.statusMessage);

        for (const auto& header : response.headers) {
            rb.header(header.first, header.second);
        }

        rb.body(response.body);
        rb.sendWithEOM();

        if (plugin_loader) {
            auto asyncContext = context;
            folly::getUnsafeMutableGlobalCPUExecutor()->add([this, asyncContext]() mutable {
                plugin_loader->executeHooks(PluginManager::HookType::POST_RESPONSE, asyncContext);
            });
        }
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