#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <future>
#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <dlfcn.h>
#include <atomic>

// Forward declarations
struct HttpRequest;
struct HttpResponse;

enum class HttpMethod {
    GET, POST, OPTIONS, DELETE, HEAD, CONNECT,
    CONNECT_UDP, PUT, TRACE, PATCH, SUB, PUB, UNSUB
};


class IHeaders {
public:
    virtual ~IHeaders() = default;

    virtual std::string get(const std::string &name) const = 0;

    virtual std::string get(const std::string &name, const std::string &default_value) const {
        auto value = get(name);
        return value.empty() ? default_value : value;
    }

    virtual bool exists(const std::string &name) const = 0;

    virtual std::vector<std::string> getAll(const std::string &name) const = 0;

    virtual void forEach(std::function<void(const std::string &, const std::string &)> callback) const = 0;

    virtual size_t size() const = 0;
};

class IBody {
public:
    virtual ~IBody() = default;

    virtual const char *data() const = 0;

    virtual size_t size() const = 0;

    virtual bool empty() const = 0;

    virtual std::string toString() const = 0;

    virtual std::string_view view() const = 0;
};

// HTTP structures
struct HttpRequest {
    HttpMethod method;
    std::string path;
    std::string query;
    std::unique_ptr<const IHeaders> headers; // Changed to interface
    std::unique_ptr<const IBody> body;
    std::string web_root;

    // Convenience methods
    std::string getHeader(const std::string &name) const {
        return headers ? headers->get(name) : "";
    }

    bool hasHeader(const std::string &name) const {
        return headers ? headers->exists(name) : false;
    }
};

struct HttpResponse {
    int statusCode = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool handled = false;
};

// Plugin interface
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual std::string getName() const = 0;

    virtual std::string getVersion() const = 0;

    virtual bool initialize() = 0;

    virtual void shutdown() = 0;

    virtual HttpResponse handleRequest(HttpRequest* request) = 0;
};

// Plugin factory function type
typedef IPlugin * (*CreatePluginFunc)();

typedef void (*DestroyPluginFunc)(IPlugin *);
