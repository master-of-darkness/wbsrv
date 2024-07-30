#include "handler.h"
#include "defines.h"
#include "utils/utils.h"

#include <folly/FileUtil.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <filesystem>
#include <utility>

using namespace proxygen;

struct CacheRow {
    CacheRow(std::string content_type, std::string text)
            : content_type(std::move(content_type)), text(std::move(text)) {}

    std::string content_type;
    std::string text;
};

LRUCache<std::string, CacheRow> cache(256);
LRUCache<std::string, std::string> virtual_hosts(256);

void StaticHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
    error_ = false;

    auto h = headers->getHeaders().rawGet("Host");
    if (auto vhost = virtual_hosts.get(h); vhost) {
        path_ = vhost.value() + '/' + headers->getPathAsStringPiece().subpiece(1).str();

        if (auto text = cache.get(path_); text) {
            auto row = text.value();
            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", row.content_type)
                    .body(row.text)
                    .sendWithEOM();
        } else {
            try {
                file_ = std::make_unique<folly::File>(path_);
            } catch (const std::system_error& ex) {
                ResponseBuilder(downstream_)
                        .status(STATUS_404)
                        .body(folly::to<std::string>("Could not find ",
                                                     headers->getPathAsStringPiece(),
                                                     " ex=",
                                                     folly::exceptionStr(ex)))
                        .sendWithEOM();
                return;
            }

            std::string cnt_type = utils::getContentType(path_);
            cache.put(path_, CacheRow(cnt_type, ""));
            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", cnt_type).send();

            // use a CPU executor since read(2) of a file can block
            readFileScheduled_ = true;
            folly::getUnsafeMutableGlobalCPUExecutor()->add(
                    std::bind(&StaticHandler::readFile,
                              this,
                              folly::EventBaseManager::get()->getEventBase()));
        }
    } else {
        ResponseBuilder(downstream_)
                .status(STATUS_400)
                .body("Bad request")
                .sendWithEOM();
        return;
    }
}

void StaticHandler::readFile(folly::EventBase* evb) {
    folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());

    while (file_ && !paused_) {
        // read 4k-ish chunks and forward each one to the client
        auto data = buf.preallocate(4000, 4000);
        auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
        if (rc < 0) {
            // error
#ifdef DEBUG
            VLOG(4) << "Read error=" << rc;
#endif
            file_.reset();
            evb->runInEventBaseThread([this] {
                LOG(ERROR) << "Error reading file";
                downstream_->sendAbort();
            });
            break;
        } else if (rc == 0) {
            // done
            file_.reset();
#ifdef DEBUG
            VLOG(4) << "Read EOF";
#endif
            evb->runInEventBaseThread([this] {
                if (!error_) {
                    ResponseBuilder(downstream_).sendWithEOM();
                }
            });
            break;
        } else {
            buf.postallocate(rc);
            auto cache_ptr = cache.get(path_);
            evb->runInEventBaseThread([this, body = buf.move(), cache_ptr]() mutable {
                if (!error_) {
                    cache_ptr->text.append(body->moveToFbString().c_str());
                    ResponseBuilder(downstream_).body(std::move(body)).send();
                }
            });
        }
    }

    // Notify the request thread that we terminated the readFile loop
    evb->runInEventBaseThread([this] {
        readFileScheduled_ = false;
        if (!checkForCompletion() && !paused_) {
#ifdef DEBUG
            VLOG(4) << "Resuming deferred readFile";
#endif
            onEgressResumed();
        }
    });
}

void StaticHandler::onEgressPaused() noexcept {
    // This will terminate readFile soon
#ifdef DEBUG
    VLOG(4) << "StaticHandler paused";
#endif
    paused_ = true;
}

void StaticHandler::onEgressResumed() noexcept {
#ifdef DEBUG
    VLOG(4) << "StaticHandler resumed";
#endif
    paused_ = false;
    // If readFileScheduled_, it will reschedule itself
    if (!readFileScheduled_ && file_) {
        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add(
                std::bind(&StaticHandler::readFile,
                          this,
                          folly::EventBaseManager::get()->getEventBase()));
    } else {
#ifdef DEBUG
        VLOG(4) << "Deferred scheduling readFile";
#endif
    }
}

void StaticHandler::onBody(std::unique_ptr<folly::IOBuf> /*body*/) noexcept {
    // ignore, only support GET
}

void StaticHandler::onEOM() noexcept {
}

void StaticHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
    // handler doesn't support upgrades
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
#ifdef DEBUG
        VLOG(4) << "deleting StaticHandler";
#endif
        delete this;
        return true;
    }
    return false;
}
