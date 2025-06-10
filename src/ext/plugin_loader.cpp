#include "ext/plugin_loader.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include "glog/logging.h"
#include "proxygen/httpserver/HTTPServer.h"

PluginLoader::PluginLoader() = default;

PluginLoader::~PluginLoader() {
    unloadAllPlugins();
}

bool PluginLoader::loadPlugin(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    if (!isValidPluginFile(filePath)) {
        setLastError("Invalid plugin file: " + filePath);
        return false;
    }
    
    if (!std::filesystem::exists(filePath)) {
        setLastError("Plugin file does not exist: " + filePath);
        return false;
    }
    
    LIBRARY_HANDLE handle;
    if (!loadLibrary(filePath, handle)) {
        return false;
    }
    CreatePluginFunc createFunc;
    DestroyPluginFunc destroyFunc;
    if (!getFunctionPointers(handle, createFunc, destroyFunc)) {
        CLOSE_LIBRARY(handle);
        return false;
    }
    std::unique_ptr<IPlugin> plugin;
    try {
        plugin.reset(createFunc());
        if (!plugin) {
            setLastError("Failed to create plugin instance");
            CLOSE_LIBRARY(handle);
            return false;
        }
    } catch (const std::exception& e) {
        setLastError("Exception during plugin creation: " + std::string(e.what()));
        CLOSE_LIBRARY(handle);
        return false;
    }
    std::string pluginName = plugin->getName();
    if (plugins_.find(pluginName) != plugins_.end()) {
        setLastError("Plugin already loaded: " + pluginName);
        destroyFunc(plugin.release());
        CLOSE_LIBRARY(handle);
        return false;
    }
    auto info = std::make_unique<PluginInfo>();
    info->name = pluginName;
    info->version = plugin->getVersion();
    info->filePath = filePath;
    info->handle = handle;
    info->instance = std::move(plugin);
    info->createFunc = createFunc;
    info->destroyFunc = destroyFunc;
    info->isLoaded = true;
    info->isInitialized = false;
    plugins_[pluginName] = std::move(info);
    LOG(INFO) << "Plugin loaded: " << pluginName << " v" << plugins_[pluginName]->version;
    return true;
}

bool PluginLoader::unloadPlugin(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        setLastError("Plugin not found: " + pluginName);
        return false;
    }
    auto& info = it->second;
    if (info->isInitialized) {
        try {
            info->instance->shutdown();
        } catch (const std::exception& e) {
            LOG(ERROR) << "Exception during plugin shutdown: " << e.what();
        }
        info->isInitialized = false;
    }
    try {
        info->destroyFunc(info->instance.release());
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception during plugin destruction: " << e.what();
    }
    if (info->handle) {
        CLOSE_LIBRARY(info->handle);
    }
    LOG(INFO) << "Plugin unloaded: " << pluginName;
    plugins_.erase(it);
    return true;
}

void PluginLoader::unloadAllPlugins() {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    for (auto& [name, info] : plugins_) {
        if (info->isInitialized) {
            try {
                info->instance->shutdown();
            } catch (const std::exception& e) {
                LOG(ERROR) << "Exception during plugin shutdown: " << e.what();
            }
        }
        
        try {
            info->destroyFunc(info->instance.release());
        } catch (const std::exception& e) {
            LOG(ERROR) << "Exception during plugin destruction: " << e.what();
        }
        
        if (info->handle) {
            CLOSE_LIBRARY(info->handle);
        }
    }
    
    plugins_.clear();
    LOG(INFO) << "All plugins unloaded";
}

bool PluginLoader::loadPluginsFromDirectory(const std::string& directory) {
    std::vector<std::string> pluginFiles;
    scanDirectory(directory, pluginFiles);
    
    bool allLoaded = true;
    for (const auto& file : pluginFiles) {
        if (!loadPlugin(file)) {
            LOG(ERROR) << "Failed to load plugin: " << file << " - " << getLastError();
            allLoaded = false;
        }
    }
    
    return allLoaded;
}

void PluginLoader::scanDirectory(const std::string& directory, std::vector<std::string>& pluginFiles) {
    try {
        if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
            return;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string filePath = entry.path().string();
                if (isValidPluginFile(filePath)) {
                    pluginFiles.push_back(filePath);
                }
            }
        }
    } catch (const std::exception& e) {
        setLastError("Error scanning directory: " + std::string(e.what()));
    }
}

bool PluginLoader::initializePlugin(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        setLastError("Plugin not found: " + pluginName);
        return false;
    }
    
    auto& info = it->second;
    if (info->isInitialized) {
        return true; // Already initialized
    }
    
    try {
        if (info->instance->initialize()) {
            info->isInitialized = true;
            LOG(INFO) << "Plugin initialized: " << pluginName;
            return true;
        } else {
            setLastError("Plugin initialization failed: " + pluginName);
            return false;
        }
    } catch (const std::exception& e) {
        setLastError("Exception during plugin initialization: " + std::string(e.what()));
        return false;
    }
}

bool PluginLoader::shutdownPlugin(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        setLastError("Plugin not found: " + pluginName);
        return false;
    }
    
    auto& info = it->second;
    if (!info->isInitialized) {
        return true; // Already shut down
    }
    
    try {
        info->instance->shutdown();
        info->isInitialized = false;
        LOG(INFO) << "Plugin shut down: " << pluginName;
        return true;
    } catch (const std::exception& e) {
        setLastError("Exception during plugin shutdown: " + std::string(e.what()));
        return false;
    }
}

void PluginLoader::initializeAllPlugins() {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    for (auto& [name, info] : plugins_) {
        if (!info->isInitialized) {
            try {
                if (info->instance->initialize()) {
                    info->isInitialized = true;
                    LOG(INFO) << "Plugin initialized: " << name;
                } else {
                    LOG(ERROR) << "Plugin initialization failed: " << name;
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Exception during plugin initialization: " << e.what();
            }
        }
    }
}

void PluginLoader::shutdownAllPlugins() {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    for (auto& [name, info] : plugins_) {
        if (info->isInitialized) {
            try {
                info->instance->shutdown();
                info->isInitialized = false;
                LOG(INFO) << "Plugin shut down: " << name;
            } catch (const std::exception& e) {
                LOG(ERROR) << "Exception during plugin shutdown: " << e.what();
            }
        }
    }
}

std::vector<std::string> PluginLoader::getLoadedPluginNames() const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    std::vector<std::string> names;
    names.reserve(plugins_.size());
    
    for (const auto& [name, info] : plugins_) {
        names.push_back(name);
    }
    
    return names;
}

std::vector<std::string> PluginLoader::getInitializedPluginNames() const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    std::vector<std::string> names;
    
    for (const auto& [name, info] : plugins_) {
        if (info->isInitialized) {
            names.push_back(name);
        }
    }
    
    return names;
}

PluginInfo* PluginLoader::getPluginInfo(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    auto it = plugins_.find(pluginName);
    return (it != plugins_.end()) ? it->second.get() : nullptr;
}

const PluginInfo* PluginLoader::getPluginInfo(const std::string& pluginName) const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    auto it = plugins_.find(pluginName);
    return (it != plugins_.end()) ? it->second.get() : nullptr;
}

IPlugin* PluginLoader::getPlugin(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    auto it = plugins_.find(pluginName);
    return (it != plugins_.end() && it->second->isInitialized) ? it->second->instance.get() : nullptr;
}

HttpResponse PluginLoader::routeRequest(const HttpRequest& request) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    for (const auto& [name, info] : plugins_) {
        if (info->isInitialized) {
            try {
                HttpResponse response = info->instance->handleRequest(request);
                if (response.handled)
                    return response;
                LOG(INFO) << "Plugin " << name << " did not handle request for " << request.path;

            } catch (const std::exception& e) {
                LOG(ERROR) << "Exception in plugin " << name << ": " << e.what();
            } catch (...) {
                LOG(ERROR) << "Unknown exception in plugin " << name;
            }
        }
    }

    // No plugin could handle the request
    HttpResponse response;
    response.statusCode = 404;
    response.body = "No plugin found to handle request: " + request.path;
    response.headers["Content-Type"] = "text/plain";
    response.handled = false; // Explicitly set to false for consistency
    return response;
}

bool PluginLoader::isPluginLoaded(const std::string& pluginName) const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    return plugins_.find(pluginName) != plugins_.end();
}

bool PluginLoader::isPluginInitialized(const std::string& pluginName) const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    auto it = plugins_.find(pluginName);
    return (it != plugins_.end()) && it->second->isInitialized;
}

size_t PluginLoader::getLoadedPluginCount() const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    return plugins_.size();
}

std::string PluginLoader::getLastError() const {
    return lastError_;
}

void PluginLoader::clearLastError() {
    lastError_.clear();
}

// Private helper methods
bool PluginLoader::loadLibrary(const std::string& filePath, LIBRARY_HANDLE& handle) {
    handle = LOAD_LIBRARY(filePath.c_str());
    if (!handle) {
        setLastError("Failed to load library: " + filePath + " - " + getSystemError());
        return false;
    }
    return true;
}

bool PluginLoader::getFunctionPointers(LIBRARY_HANDLE handle, CreatePluginFunc& createFunc, DestroyPluginFunc& destroyFunc) {
    createFunc = reinterpret_cast<CreatePluginFunc>(GET_FUNCTION(handle, "createPlugin"));
    if (!createFunc) {
        setLastError("Failed to find createPlugin function - " + getSystemError());
        return false;
    }
    
    destroyFunc = reinterpret_cast<DestroyPluginFunc>(GET_FUNCTION(handle, "destroyPlugin"));
    if (!destroyFunc) {
        setLastError("Failed to find destroyPlugin function - " + getSystemError());
        return false;
    }
    
    return true;
}

void PluginLoader::setLastError(const std::string& error) {
    lastError_ = error;
}

std::string PluginLoader::getSystemError() const {
    const char* error = dlerror();
    return error ? std::string(error) : "No error";
}

bool PluginLoader::isValidPluginFile(const std::string& filePath) const {
    std::string extension = std::filesystem::path(filePath).extension().string();
    return extension == LIBRARY_EXTENSION;
}