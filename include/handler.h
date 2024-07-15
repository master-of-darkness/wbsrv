#pragma once

#include <atomic>
#include <utility>
#include <folly/File.h>
#include <folly/Memory.h>
#include <proxygen/httpserver/RequestHandler.h>

#include "cache.hpp"
#include "lru_cache_policy.hpp"

namespace proxygen {
    class ResponseHandler;
}

struct VHost {
    VHost(std::string v1, std::string v2) : host(std::move(v1)), web_dir(std::move(v2)) {};

    std::string host;
    std::string web_dir;
};

typedef VHost VHost;

extern caches::fixed_sized_cache<std::string, std::string> virtual_hosts;
extern caches::fixed_sized_cache<int, std::vector<VHost>> same_port;

class StaticHandler : public proxygen::RequestHandler {
public:
    void onRequest(
            std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override;

    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

    void onEOM() noexcept override;

    void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

    void requestComplete() noexcept override;

    void onError(proxygen::ProxygenError err) noexcept override;

    void onEgressPaused() noexcept override;

    void onEgressResumed() noexcept override;

private:
    void readFile(folly::EventBase *evb);

    bool checkForCompletion();

    std::string path_;
    std::unique_ptr<folly::File> file_;
    bool readFileScheduled_{false};
    std::atomic<bool> paused_{false};
    bool finished_{false};
    std::atomic<bool> error_{false};
};
