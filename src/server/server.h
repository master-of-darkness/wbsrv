#pragma once

#include "utils/vhost.h"
#include <folly/io/IOBufQueue.h>

#include "interface.h"
#include "ext/plugin_loader.h"
#include "../include/interface.h"
#include "utils/cache.h"

class StaticHandler : public proxygen::RequestHandler {
public:
    explicit StaticHandler(std::string path, std::string web_root = "",
                           cache::arc_cache<std::string, CacheRow *> *cache = nullptr): path_(std::move(path)),
        web_root_(std::move(web_root)),
        cache_(cache) {
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
    void readFile() const;

    bool checkForCompletion();

    void processRequest();

    bool handleHooks(PluginManager::HttpMethod method, const std::string &query) const;

    void handleStaticFile();

    std::string cached_content_type_;
    std::unique_ptr<proxygen::HTTPMessage> headers_;
    std::string path_;
    std::string web_root_;

    std::unique_ptr<folly::File> file_;
    std::unique_ptr<folly::IOBuf> body_;
    cache::arc_cache<std::string, CacheRow *> *cache_;
    bool readFileScheduled_ = false;
    bool paused_ = false;
    bool finished_ = false;
    bool handled_from_cache_ = false;
    bool error_ = false;
    folly::EventBase *event_base_;
};

// Global declarations
extern PluginManager::HookManagerImpl *plugin_loader;
extern PluginManager::PluginManager *plugin_manager;
