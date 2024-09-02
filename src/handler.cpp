#include <folly/FileUtil.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <filesystem>
#include <utility>

#include "handler.h"
#include "defines.h"
#include "utils/utils.h"
#include "vhost/vhost.h"

using namespace proxygen;

typedef struct CacheRow {
    bool operator!=(const CacheRow& rhs) const{
        return (this->content_type == rhs.content_type) && (this->text == rhs.text);
    }

    const char* content_type;
    std::string text;
} CacheRow_t;

typedef utils::ConcurrentLRUCache<std::string, CacheRow_t>::ConstAccessor c_acc_cache;


utils::ConcurrentLRUCache<std::string, CacheRow_t> cache(256);

void StaticHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
    error_ = false;
    vhost::const_accessor c_acc;

    if (auto v = vhost::list.find(c_acc, hostname); v) {
        c_acc_cache const_acc_2;
        path_ = *c_acc + '/' + headers->getPathAsStringPiece().subpiece(1).str();

        if (auto g = cache.find(const_acc_2, path_); g) {
            auto text = *const_acc_2;
            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", text.content_type)
                    .body(text.text)
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

            _temp_content_type = utils::getContentType(path_);

            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", _temp_content_type)
                    .send();

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
                    cache.insert(path_, CacheRow_t{_temp_content_type, _temp_text});
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

