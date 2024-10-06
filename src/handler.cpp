#include <proxygen/httpserver/ResponseBuilder.h>
#include <filesystem>

#include "defines.h"
#include "utils.h"
#include "vhost.h"
#include "handler.h"
#include "php_sapi.h"

using namespace proxygen;

struct CacheRow
{
    bool operator!=(const CacheRow& rhs) const
    {
        return (content_type != rhs.content_type) || (text != rhs.text);
    }

    std::string content_type;
    std::string text;
};

using CacheAccessor = utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>::ConstAccessor;

utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>> cache(256);

void Handler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept
{
    error_ = false;
    std::string requested_path = headers->getPath();
    vhost::const_accessor vhost_accessor;
    if (auto v = vhost::list.find(vhost_accessor, hostname); v)
    {
        CacheAccessor cache_acc;
        // Construct the full path using the web directory and requested path
        path_ = vhost_accessor->web_dir + '/' + requested_path;

        // If the requested path is a directory, attempt to find a default page
        if (std::filesystem::is_directory(path_))
        {
            for (const auto& index_page : vhost_accessor->index_pages)
            {
                std::string index_path = path_ + '/' + index_page;
                if (std::filesystem::exists(index_path))
                {
                    path_ = index_path; // Use the found index page
                    break;
                }
            }
        }

        // Check if the path is already cached
        if (cache.find(cache_acc, path_))
        {
            const auto& cache_row = *cache_acc;
            ResponseBuilder(downstream_)
                .status(STATUS_200)
                .header("Content-Type", cache_row->content_type)
                .body(cache_row->text)
                .sendWithEOM();
        }
        else
        {
            if (path_.ends_with(".php"))
            {
                requestPHP(folly::EventBaseManager::get()->getEventBase(), headers);
                return;
            }
            handleFileRead();
        }
    }
    else
    {
        ResponseBuilder(downstream_)
            .status(STATUS_400)
            .body(utils::getErrorPage(400))
            .sendWithEOM();
    }
}

void Handler::handleFileRead()
{
    try
    {
        file_ = std::make_unique<folly::File>(path_);
    }
    catch (const std::system_error& ex)
    {
        ResponseBuilder(downstream_)
            .status(STATUS_404)
            .body(utils::getErrorPage(404))
            .sendWithEOM();
        return;
    }

    _temp_content_type = utils::getContentType(path_);
    ResponseBuilder(downstream_).status(STATUS_200).header("Content-Type", _temp_content_type).send();

    // Use a CPU executor since read(2) of a file can block
    readFileScheduled_ = true;
    folly::getUnsafeMutableGlobalCPUExecutor()->add(
        std::bind(&Handler::readFile,
                  this,
                  folly::EventBaseManager::get()->getEventBase())
    );
}

void Handler::requestPHP(folly::EventBase* evb, const std::unique_ptr<HTTPMessage>& headers)
{
    std::string a;
    EmbedPHP::executeScript(path_, a, headers);
    evb->runInEventBaseThread([this, a]
    {
        ResponseBuilder(downstream_)
            .status(STATUS_200)
            .body(a)
            .sendWithEOM();
    });
}


void Handler::readFile(folly::EventBase* evb)
{
    folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());

    while (file_ && !paused_)
    {
        // read 4k-ish chunks and forward each one to the client
        auto data = buf.preallocate(4000, 4000);
        auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
        if (rc < 0)
        {
            // error
            file_.reset();
            evb->runInEventBaseThread([this]
            {
                LOG(ERROR) << "Error reading file";
                downstream_->sendAbort();
            });
            break;
        }
        else if (rc == 0)
        {
            // done
            file_.reset();
            evb->runInEventBaseThread([this]
            {
                if (!error_)
                {
                    cache.insert(path_, std::make_shared<CacheRow>(_temp_content_type, _temp_text));
                    ResponseBuilder(downstream_).sendWithEOM();
                }
            });
            break;
        }
        else
        {
            buf.postallocate(rc);
            evb->runInEventBaseThread([this, body = buf.move()]() mutable
            {
                if (!error_)
                {
                    body->appendTo(_temp_text);
                    ResponseBuilder(downstream_).body(std::move(body)).send();
                }
            });
        }
    }

    // Notify the request thread that we terminated the readFile loop
    evb->runInEventBaseThread([this]
    {
        readFileScheduled_ = false;
        if (!checkForCompletion() && !paused_)
        {
            onEgressResumed();
        }
    });
}

void Handler::onEgressPaused() noexcept
{
    // This will terminate readFile soon
    paused_ = true;
}

void Handler::onEgressResumed() noexcept
{
    paused_ = false;
    // If readFileScheduled_, it will reschedule itself
    if (!readFileScheduled_ && file_)
    {
        readFileScheduled_ = true;
        folly::getUnsafeMutableGlobalCPUExecutor()->add(
            std::bind(&Handler::readFile,
                      this,
                      folly::EventBaseManager::get()->getEventBase()));
    }
}

void Handler::onBody(std::unique_ptr<folly::IOBuf> /*body*/) noexcept
{
    // ignore, only support GET
}

void Handler::onEOM() noexcept
{
}

void Handler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept
{
    // handler doesn't support upgrades
}

void Handler::requestComplete() noexcept
{
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

void Handler::onError(ProxygenError /*err*/) noexcept
{
    error_ = true;
    finished_ = true;
    paused_ = true;
    checkForCompletion();
}

bool Handler::checkForCompletion()
{
    if (finished_ && !readFileScheduled_)
    {
        delete this;
        return true;
    }
    return false;
}
