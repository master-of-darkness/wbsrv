#pragma once
#include <filesystem>
#include "../include/interface.h"
#include "utils/concurrent_cache.h"
#include <dlfcn.h>
#include "proxygen/httpserver/HTTPServer.h"
#define LIBRARY_HANDLE void*
#define LOAD_LIBRARY(path) dlopen(path, RTLD_LAZY)
#define GET_FUNCTION(handle, name) dlsym(handle, name)
#define CLOSE_LIBRARY(handle) dlclose(handle)
#define LIBRARY_EXTENSION ".so"


struct PluginInfo {
    std::string name;
    std::string version;
    std::string filePath;
    LIBRARY_HANDLE handle;
    IPlugin *instance;
    CreatePluginFunc createFunc;
    DestroyPluginFunc destroyFunc;
    std::atomic<bool> isLoaded{false};
    std::atomic<bool> isInitialized{false};

    PluginInfo() = default;

    PluginInfo(const PluginInfo &) = delete;

    PluginInfo &operator=(const PluginInfo &) = delete;

    PluginInfo(PluginInfo &&other) noexcept
        : name(std::move(other.name))
          , version(std::move(other.version))
          , filePath(std::move(other.filePath))
          , handle(other.handle)
          , instance(other.instance)
          , createFunc(other.createFunc)
          , destroyFunc(other.destroyFunc)
          , isLoaded(other.isLoaded.load())
          , isInitialized(other.isInitialized.load()) {
        other.handle = nullptr;
        other.instance = nullptr;
        other.createFunc = nullptr;
        other.destroyFunc = nullptr;
    }

    PluginInfo &operator=(PluginInfo &&other) noexcept {
        if (this != &other) {
            name = std::move(other.name);
            version = std::move(other.version);
            filePath = std::move(other.filePath);
            handle = other.handle;
            instance = other.instance;
            createFunc = other.createFunc;
            destroyFunc = other.destroyFunc;
            isLoaded.store(other.isLoaded.load());
            isInitialized.store(other.isInitialized.load());

            other.handle = nullptr;
            other.instance = nullptr;
            other.createFunc = nullptr;
            other.destroyFunc = nullptr;
        }
        return *this;
    }
};

class PluginLoader {
public:
    PluginLoader();

    ~PluginLoader();

    bool loadPlugin(const std::string &filePath);

    bool unloadPlugin(const std::string &pluginName);

    void unloadAllPlugins();

    bool loadPluginsFromDirectory(const std::string &directory);

    bool initializePlugin(const std::string &pluginName);

    bool shutdownPlugin(const std::string &pluginName);

    void initializeAllPlugins();

    void shutdownAllPlugins();

    IPlugin *getPlugin(const std::string &pluginName);

    HttpResponse routeRequest(HttpRequest *request);

    static std::string getLastError();

    static void clearLastError();

private:
    utils::ConcurrentLRUCache<std::string, PluginInfo> plugins_{1000};

    utils::ConcurrentLRUCache<std::string, IPlugin *> pluginInstanceCache_{1000};
    utils::ConcurrentLRUCache<std::string, std::string> routeToPluginCache_{5000};

    std::atomic<size_t> loadedPluginCount_{0};
    std::atomic<size_t> initializedPluginCount_{0};

    thread_local static std::string lastError_;

    void scanDirectory(const std::string &directory, std::vector<std::string> &pluginFiles) const;

    static bool loadLibrary(const std::string &filePath, LIBRARY_HANDLE&handle);

    static bool getFunctionPointers(LIBRARY_HANDLE handle, CreatePluginFunc &createFunc,
                                    DestroyPluginFunc &destroyFunc);

    static bool isValidPluginFile(const std::string &filePath);

    static std::string getSystemError();

    static void setLastError(const std::string &error);

    void updatePluginCache(const std::string &pluginName, IPlugin *plugin);

    void invalidatePluginCache(const std::string &pluginName);

    void invalidateRouteCache();

    PluginInfo createPluginInfo(const std::string &filePath,
                                LIBRARY_HANDLE handle,
                                CreatePluginFunc createFunc,
                                DestroyPluginFunc destroyFunc);
};

inline std::unique_ptr<PluginLoader> plugin_loader = std::make_unique<PluginLoader>();

class ProxygenHeadersAdapter : public IHeaders {
private:
    const proxygen::HTTPHeaders &headers_;

public:
    ProxygenHeadersAdapter(const proxygen::HTTPHeaders &headers) : headers_(headers) {
    }

    std::string get(const std::string &name) const override {
        return headers_.getSingleOrEmpty(name);
    }

    bool exists(const std::string &name) const override {
        return headers_.exists(name);
    }

    std::vector<std::string> getAll(const std::string &name) const override {
        std::vector<std::string> values;
        headers_.forEachValueOfHeader(name, [&](const std::string &value) {
            values.push_back(value);
            return false;
        });
        return values;
    }

    void forEach(std::function<void(const std::string &, const std::string &)> callback) const override {
        headers_.forEach(callback);
    }

    size_t size() const override {
        return headers_.size();
    }
};

class FollyBodyImpl : public IBody {
private:
    std::unique_ptr<folly::IOBuf> iobuf_;

public:
    explicit FollyBodyImpl(std::unique_ptr<folly::IOBuf> iobuf)
        : iobuf_(std::move(iobuf)) {}

    const char* data() const override {
        return reinterpret_cast<const char*>(iobuf_->data());
    }

    size_t size() const override {
        return iobuf_->length();
    }

    bool empty() const override {
        return !iobuf_ || iobuf_->empty();
    }

    std::string toString() const override {
        return iobuf_->toString();
    }

    std::string_view view() const override {
        return std::string_view(data(), size());
    }
};
