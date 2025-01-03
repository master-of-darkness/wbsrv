#pragma once

#include <utility>

#include "common.h"
#include "vhost.h"

class EngineHandler : public proxygen::RequestHandler
{
public:
    explicit EngineHandler(std::string path, utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>* cache, std::string web_root):
        cache_(cache), path_(std::move(path)), web_root_(web_root)
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
    bool checkForCompletion();

    utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>* cache_ = nullptr;

    std::unique_ptr<proxygen::HTTPMessage> headers_;

    std::string hostname;

    std::string _temp_text;

    const char* _temp_content_type;

    std::string web_root_;

    std::string messageBody;

    std::string path_;

    std::unique_ptr<folly::File> file_;

    bool readFileScheduled_{false};

    std::atomic<bool> paused_{false};

    bool finished_{false};

    std::atomic<bool> error_{false};
};
