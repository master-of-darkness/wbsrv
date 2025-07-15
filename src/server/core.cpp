#include "core.h"

#include <folly/logging/xlog.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <folly/executors/GlobalExecutor.h>

#include "utils/defines.h"
#include "utils/utils.h"

using namespace proxygen;


void ServerHandler::onRequest(std::unique_ptr<HTTPMessage> message) noexcept {
    ctx_.request = std::move(message);
    event_base_ = folly::EventBaseManager::get()->getEventBase();

    const folly::StringPiece host_header = ctx_.request->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST);
    const folly::StringPiece path_piece = ctx_.request->getPathAsStringPiece();

    const XXH64_hash_t host_hash = Utils::computeXXH64Hash(host_header);
    const auto vhost_it = host_config_cache_->find(host_hash);
    if (vhost_it == host_config_cache_->end()) {
        ResponseBuilder(downstream_)
                .status(STATUS_404)
                .body(Utils::getErrorPage(404))
                .sendWithEOM();
        return;
    }

    const folly::fbstring &doc_root = vhost_it->second.web_root_directory;
    ctx_.document_root = doc_root;

    folly::fbstring full_path;
    full_path.reserve(doc_root.size() + path_piece.size());
    full_path.append(doc_root);
    full_path.append(path_piece.begin(), path_piece.end());

    if (!path_piece.empty() && path_piece.back() == '/') {
        const XXH64_hash_t redirect_hash = Utils::computeXXH64Hash(doc_root, path_piece);
        auto redirect_it = directory_redirect_cache_->find(redirect_hash);
        if (redirect_it == directory_redirect_cache_->end()) {
            ResponseBuilder(downstream_)
                    .status(STATUS_404)
                    .body(Utils::getErrorPage(404))
                    .sendWithEOM();
            return;
        }
        ctx_.file_path = redirect_it->second;
    } else {
        ctx_.file_path = std::move(full_path);
    }

    g_moduleSystem.execute_hooks(ModuleManage::HookStage::PRE_REQUEST, ctx_);

    if (ctx_.request->getMethod() == HTTPMethod::GET) {
        const XXH64_hash_t file_path_hash = Utils::computeXXH64Hash(ctx_.file_path);
        auto cached_it = cache_->find(file_path_hash);
        if (cached_it != cache_->end()) {
            ctx_.response = std::make_unique<ResponseBuilder>(downstream_);

            g_moduleSystem.execute_hooks(ModuleManage::HookStage::PRE_RESPONSE, ctx_);

            ctx_.response->status(STATUS_200)
                    .header(HTTP_HEADER_CONTENT_TYPE, cached_it->second.content_type)
                    .body(cached_it->second.data->clone())
                    .sendWithEOM();

            g_moduleSystem.execute_hooks(ModuleManage::HookStage::POST_RESPONSE, ctx_);
            handled_from_cache_ = true;
            return;
        }
    }

    ctx_.response = std::make_unique<ResponseBuilder>(downstream_);
    error_ = false;
    cached_content_type_ = Utils::getContentType(ctx_.file_path);
}


void ServerHandler::handleStaticFile() {
    try {
        file_ = std::make_unique<folly::File>(ctx_.file_path);
    } catch (const std::system_error &) {
        event_base_->runInEventBaseThread([this]() {
            if (error_ || finished_) return;

            ctx_.response->status(STATUS_404)
                    .body(Utils::getErrorPage(404))
                    .sendWithEOM();
        });
        return;
    }

    event_base_->runInEventBaseThread([this]() {
        if (error_ || finished_) return;

        ctx_.response->status(STATUS_200)
                .header("Content-Type", cached_content_type_)
                .send();

        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add([this]() {
            folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
            std::vector<std::unique_ptr<folly::IOBuf> > chunks;

            while (file_ && !paused_ && !error_ && !finished_) {
                auto data = buf.preallocate(4000, 4000);
                auto rc = folly::readNoInt(file_->fd(), data.first, data.second);

                if (rc < 0) {
                    break;
                } else if (rc == 0) {
                    if (!chunks.empty()) {
                        auto complete_buf = folly::IOBuf::create(0);
                        for (auto &chunk: chunks) {
                            complete_buf->prependChain(std::move(chunk));
                        }

                        Cache::ResponseData row;
                        row.content_type = cached_content_type_;
                        row.data = std::move(complete_buf);

                        // Move cache operation to event base thread
                        event_base_->runInEventBaseThread([this, row = std::move(row)]() mutable {
                            if (!error_ && !finished_) {
                                cache_->set(Utils::computeXXH64Hash(ctx_.file_path), std::move(row));
                                ctx_.response->sendWithEOM();
                            }
                        });
                    } else {
                        // No chunks to cache, just send EOM
                        event_base_->runInEventBaseThread([this]() {
                            if (!error_ && !finished_) {
                                ctx_.response->sendWithEOM();
                            }
                        });
                    }
                    break;
                } else {
                    buf.postallocate(rc);
                    auto chunk = buf.move();

                    // Store clone for caching, send original
                    chunks.push_back(chunk->clone());

                    event_base_->runInEventBaseThread([this, chunk = std::move(chunk)]() mutable {
                        if (!error_ && !finished_) {
                            ctx_.response->body(std::move(chunk)).send();
                        }
                    });
                }
            }
        });
    });
}


void ServerHandler::onEgressPaused() noexcept {
    paused_ = true;
}

void ServerHandler::onEgressResumed() noexcept {
    paused_ = false;

    if (handled_from_cache_) {
        finished_ = true;
        checkForCompletion();
    }
}

void ServerHandler::onEOM() noexcept {
    if (!handled_from_cache_) {
        ctx_.request_body = body_;

        auto result = g_moduleSystem.execute_hooks(ModuleManage::HookStage::PRE_RESPONSE, ctx_);

        if (result != ModuleManage::ModuleResult::BREAK)
            handleStaticFile();

        g_moduleSystem.execute_hooks(ModuleManage::HookStage::POST_RESPONSE, ctx_);
    }
}

void ServerHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
    if (body_) {
        body_->prependChain(std::move(body));
    } else {
        body_ = std::move(body);
    }
}

void ServerHandler::onUpgrade(UpgradeProtocol protocol) noexcept {
}

void ServerHandler::requestComplete() noexcept {
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

void ServerHandler::onError(ProxygenError /*err*/) noexcept {
    error_ = true;
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

bool ServerHandler::checkForCompletion() {
    if (finished_) {
        delete this;
        return true;
    }
    return false;
}
