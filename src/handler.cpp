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

struct CacheRow{
    CacheRow(std::string &content_type, std::string text): content_type(content_type), text(std::move(text)) {}

    std::string content_type;
    std::string text;
};

caches::fixed_sized_cache<std::string, CacheRow, caches::LRUCachePolicy> cache(256);
caches::fixed_sized_cache<std::string, std::string> virtual_hosts(256);

void StaticHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
    error_ = false;
    if (headers->getMethod() != HTTPMethod::GET) {
        ResponseBuilder(downstream_)
                .status(400, "Bad method")
                .body("Only GET is supported")
                .sendWithEOM();
        return;
    }

    auto h = headers->getHeaders().rawGet("Host");
    auto f = virtual_hosts.TryGet(h);
    if (f.second) {
        path_ = *f.first + '/' + std::string(headers->getPathAsStringPiece().subpiece(1).str());

        if (cache.Cached(path_)) {
            LOG(INFO) << path_ << "exists";
            auto row = cache.Get(path_);
            ResponseBuilder(downstream_)
                    .status(STATUS_200)
                    .header("Content-Type", row->content_type)
                    .body(row->text)
                    .sendWithEOM();
        } else {
            LOG(INFO) << path_ << "not exists";

            try {
                file_ = std::make_unique<folly::File>(path_);
            } catch (const std::system_error &ex) {
                ResponseBuilder(downstream_)
                        .status(STATUS_404)
                        .body(folly::to<std::string>("Could not find ",
                                                     headers->getPathAsStringPiece(),
                                                     " ex=",
                                                     folly::exceptionStr(ex)))
                        .sendWithEOM();
                return;
            }

//            ResponseBuilder(downstream_).status(STATUS_200).send();

            std::string cnt_type = utils::getContentType(path_.c_str());
            cache.Put(path_, CacheRow(cnt_type, ""));
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
                .status(400, "Bad method")
                .body("Only GET is supported")
                .sendWithEOM();
        return;
    }
}

void StaticHandler::readFile(folly::EventBase *evb) {
    folly::IOBufQueue buf;


    while (file_ && !paused_) {
        // read 4k-ish chunks and foward each one to the client
        auto data = buf.preallocate(4000, 4000);
        auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
        if (rc < 0) {
            // error
            VLOG(4) << "Read error=" << rc;
            file_.reset();
            evb->runInEventBaseThread([this] {
                LOG(ERROR) << "Error reading file";
                downstream_->sendAbort();
            });
            break;
        } else if (rc == 0) {
            // done
            file_.reset();
            VLOG(4) << "Read EOF";
            evb->runInEventBaseThread([this] {
                if (!error_) {
                    ResponseBuilder(downstream_).sendWithEOM();
                }
            });
            break;
        } else {
            buf.postallocate(rc);
            auto cache_ptr = cache.Get(path_);
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
            VLOG(4) << "Resuming deferred readFile";
            onEgressResumed();
        }
    });
}

void StaticHandler::onEgressPaused() noexcept {
    // This will terminate readFile soon
    VLOG(4) << "StaticHandler paused";
    paused_ = true;
}

void StaticHandler::onEgressResumed() noexcept {
    VLOG(4) << "StaticHandler resumed";
    paused_ = false;
    // If readFileScheduled_, it will reschedule itself
    if (!readFileScheduled_ && file_) {
        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add(
                std::bind(&StaticHandler::readFile,
                          this,
                          folly::EventBaseManager::get()->getEventBase()));
    } else {
        VLOG(4) << "Deferred scheduling readFile";
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
        VLOG(4) << "deleting StaticHandler";
        delete this;
        return true;
    }
    return false;
}

