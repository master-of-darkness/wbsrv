#pragma once

#include <atomic>
#include <utility>
#include <folly/File.h>
#include <proxygen/httpserver/RequestHandler.h>
#include "vhost.h"

namespace proxygen
{
    class ResponseHandler;
}

class Handler : public proxygen::RequestHandler
{
public:
    explicit Handler(std::string hostname): hostname(std::move(hostname))
    {
    }

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
    void readFile(folly::EventBase* evb);

    void handleFileRead();

    void requestPHP(folly::EventBase* evb, bool checkBody = false);

    void handleGetRequest();

    void handlePostRequest();

    bool checkForCompletion();

    std::unique_ptr<proxygen::HTTPMessage> headers_;


    std::string messageBody;

    std::string hostname;

    std::string _temp_text;
    const char* _temp_content_type;

    std::string path_;
    std::string php_output;
    std::unique_ptr<folly::File> file_;
    bool readFileScheduled_{false};
    std::atomic<bool> paused_{false};
    bool finished_{false};
    std::atomic<bool> error_{false};

    vhost::const_accessor vhost_accessor;

};
