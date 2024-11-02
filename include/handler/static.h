#pragma once

#include "common.h"
#include "vhost.h"

class StaticHandler : public proxygen::RequestHandler
{
public:
    explicit StaticHandler(std::string path, utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>* cache,
                           vhost::const_accessor* vhost):
        cache_(cache), path_(std::move(path)), vhost_accessor_(vhost)
    {
    }

    void onRequest(
        std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override;

    void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

    void requestComplete() noexcept override;

    void onError(proxygen::ProxygenError err) noexcept override;

    void onEgressPaused() noexcept override;

    void onEgressResumed() noexcept override;

    void onEOM() noexcept override;

    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

private:
    void readFile(folly::EventBase* evb);

    bool checkForCompletion();

    utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>* cache_ = nullptr;

    vhost::const_accessor* vhost_accessor_ = nullptr;

    std::unique_ptr<proxygen::HTTPMessage> headers_;

    std::string _temp_text;

    const char* _temp_content_type;

    std::string path_;

    std::unique_ptr<folly::File> file_;

    bool readFileScheduled_{false};

    std::atomic<bool> paused_{false};

    bool finished_{false};

    std::atomic<bool> error_{false};
};
