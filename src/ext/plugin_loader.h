#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <future>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include "../include/interface.h"
#include <dlfcn.h>
#include "proxygen/httpserver/HTTPServer.h"
#define LIBRARY_HANDLE void*
#define LOAD_LIBRARY(path) dlopen(path, RTLD_LAZY)
#define GET_FUNCTION(handle, name) dlsym(handle, name)
#define CLOSE_LIBRARY(handle) dlclose(handle)
#define LIBRARY_EXTENSION ".so"


// Plugin information structure
struct PluginInfo {
    std::string name;
    std::string version;
    std::string filePath;
    LIBRARY_HANDLE handle;
    std::unique_ptr<IPlugin> instance;
    CreatePluginFunc createFunc;
    DestroyPluginFunc destroyFunc;
    bool isLoaded;
    bool isInitialized;
};

// Plugin loader class
class PluginLoader {
public:
    PluginLoader();
    ~PluginLoader();

    // Core functionality
    bool loadPlugin(const std::string& filePath);
    bool unloadPlugin(const std::string& pluginName);
    void unloadAllPlugins();
    
    // Directory operations
    bool loadPluginsFromDirectory(const std::string& directory);
    void scanDirectory(const std::string& directory, std::vector<std::string>& pluginFiles);
    
    // Plugin management
    bool initializePlugin(const std::string& pluginName);
    bool shutdownPlugin(const std::string& pluginName);
    void initializeAllPlugins();
    void shutdownAllPlugins();
    
    // Query operations
    std::vector<std::string> getLoadedPluginNames() const;
    std::vector<std::string> getInitializedPluginNames() const;
    PluginInfo* getPluginInfo(const std::string& pluginName);
    const PluginInfo* getPluginInfo(const std::string& pluginName) const;
    IPlugin* getPlugin(const std::string& pluginName);
    
    // Request handling
    HttpResponse routeRequest(const HttpRequest& request);

    // Utility
    bool isPluginLoaded(const std::string& pluginName) const;
    bool isPluginInitialized(const std::string& pluginName) const;
    size_t getLoadedPluginCount() const;

    // Error handling
    std::string getLastError() const;
    void clearLastError();

private:
    mutable std::mutex pluginsMutex_;
    std::unordered_map<std::string, std::unique_ptr<PluginInfo>> plugins_;
    std::string lastError_;
    
    // Helper methods
    bool loadLibrary(const std::string& filePath, LIBRARY_HANDLE& handle);
    bool getFunctionPointers(LIBRARY_HANDLE handle, CreatePluginFunc& createFunc, DestroyPluginFunc& destroyFunc);
    void setLastError(const std::string& error);
    std::string getSystemError() const;
    bool isValidPluginFile(const std::string& filePath) const;
};

inline std::unique_ptr<PluginLoader> plugin_loader = std::make_unique<PluginLoader>();

class ProxygenHeadersAdapter : public IHeaders {
private:
    const proxygen::HTTPHeaders& headers_;

public:
    ProxygenHeadersAdapter(const proxygen::HTTPHeaders& headers) : headers_(headers) {}

    std::string get(const std::string& name) const override {
        return headers_.getSingleOrEmpty(name);
    }

    bool exists(const std::string& name) const override {
        return headers_.exists(name);
    }

    std::vector<std::string> getAll(const std::string& name) const override {
        std::vector<std::string> values;
        headers_.forEachValueOfHeader(name, [&](const std::string& value) {
            values.push_back(value);
            return false;
        });
        return values;
    }

    void forEach(std::function<void(const std::string&, const std::string&)> callback) const override {
        headers_.forEach(callback);
    }

    size_t size() const override {
        return headers_.size();
    }
};