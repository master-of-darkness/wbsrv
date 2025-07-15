#pragma once

#include "utils/config.h"
#include <folly/io/IOBufQueue.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include "module.h"
#include "utils/cache.h"

class ServerHandler : public proxygen::RequestHandler {
public:
    explicit ServerHandler(
        folly::EvictingCacheMap<XXH64_hash_t, Cache::ResponseData> *cache,
        folly::EvictingCacheMap<XXH64_hash_t, Cache::VirtualHostConfig> *host_config_cache,
        folly::EvictingCacheMap<XXH64_hash_t, folly::fbstring> *directory_redirect_cache) : cache_(cache),
        host_config_cache_(host_config_cache),
        directory_redirect_cache_(directory_redirect_cache) {
    }

    void onRequest(std::unique_ptr<proxygen::HTTPMessage> message) noexcept override;

    void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

    void requestComplete() noexcept override;

    void onError(proxygen::ProxygenError err) noexcept override;

    void onEgressPaused() noexcept override;

    void onEgressResumed() noexcept override;

    void onEOM() noexcept override;

    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

private:
    bool checkForCompletion();

    void handleStaticFile();

    const char *cached_content_type_;
    ModuleManage::ModuleContext ctx_;

    std::unique_ptr<folly::File> file_;
    std::shared_ptr<folly::IOBuf> body_;
    folly::EvictingCacheMap<XXH64_hash_t, Cache::ResponseData> *cache_;
    folly::EvictingCacheMap<XXH64_hash_t, Cache::VirtualHostConfig> *host_config_cache_;
    folly::EvictingCacheMap<XXH64_hash_t, folly::fbstring> *directory_redirect_cache_;
    bool readFileScheduled_ = false;
    bool paused_ = false;
    bool finished_ = false;
    bool handled_from_cache_ = false;
    bool error_ = false;
    folly::EventBase *event_base_;
};
