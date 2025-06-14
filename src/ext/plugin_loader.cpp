#include "ext/plugin_loader.h"
#include <algorithm>
#include <sstream>
#include "glog/logging.h"
#include "proxygen/httpserver/HTTPServer.h"

// Thread-local error storage
thread_local std::string PluginLoader::lastError_;

PluginLoader::PluginLoader() = default;

PluginLoader::~PluginLoader() {
    unloadAllPlugins();
}

bool PluginLoader::loadPlugin(const std::string &filePath) {
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

    auto info = createPluginInfo(filePath, handle, createFunc, destroyFunc);
    if (info.name.empty()) {
        // createPluginInfo failed
        CLOSE_LIBRARY(handle);
        return false;
    }

    std::string pluginName = info.name;

    // Check if plugin already exists
    if (plugins_.get(pluginName)) {
        setLastError("Plugin already loaded: " + pluginName);
        destroyFunc(info.instance);
        CLOSE_LIBRARY(handle);
        return false;
    }

    plugins_.put(pluginName, std::move(info));
    loadedPluginCount_.fetch_add(1);

    LOG(INFO) << "Plugin loaded: " << pluginName;
    return true;
}

bool PluginLoader::unloadPlugin(const std::string &pluginName) {
    auto pluginInfo = plugins_.get(pluginName);
    if (!pluginInfo) {
        setLastError("Plugin not found: " + pluginName);
        return false;
    }

    const auto &info = *pluginInfo; // Fixed: use const reference

    if (info.isInitialized.load()) {
        try {
            info.instance->shutdown();
            initializedPluginCount_.fetch_sub(1);
        } catch (const std::exception &e) {
            LOG(ERROR) << "Exception during plugin shutdown: " << e.what();
        }
        // Note: We can't modify the atomic through const reference
        // This is a design issue - the cache should allow mutable access
    }

    try {
        info.destroyFunc(info.instance); // Fixed: use info instead of undefined variable
    } catch (const std::exception &e) {
        LOG(ERROR) << "Exception during plugin destruction: " << e.what();
    }

    if (info.handle) {
        CLOSE_LIBRARY(info.handle);
    }

    // Remove from caches
    plugins_.remove(pluginName);
    invalidatePluginCache(pluginName);
    loadedPluginCount_.fetch_sub(1);

    LOG(INFO) << "Plugin unloaded: " << pluginName;
    return true;
}

void PluginLoader::unloadAllPlugins() {
    // Collect plugin names first to avoid iterator invalidation
    std::vector<std::string> pluginNames;
    plugins_.forEach([&](const std::string &name, const PluginInfo & /*info*/) {
        pluginNames.push_back(name);
    });

    // Unload each plugin
    for (const auto &name: pluginNames) {
        unloadPlugin(name);
    }

    // Clear all caches
    plugins_ = utils::ConcurrentLRUCache<std::string, PluginInfo>{1000};
    pluginInstanceCache_ = utils::ConcurrentLRUCache<std::string, IPlugin *>{1000};
    invalidateRouteCache();

    loadedPluginCount_.store(0);
    initializedPluginCount_.store(0);

    LOG(INFO) << "All plugins unloaded";
}

bool PluginLoader::loadPluginsFromDirectory(const std::string &directory) {
    std::vector<std::string> pluginFiles;
    scanDirectory(directory, pluginFiles);

    bool allLoaded = true;
    for (const auto &file: pluginFiles) {
        if (!loadPlugin(file)) {
            LOG(ERROR) << "Failed to load plugin: " << file << " - " << getLastError();
            allLoaded = false;
        }
    }

    return allLoaded;
}

void PluginLoader::scanDirectory(const std::string &directory, std::vector<std::string> &pluginFiles) const {
    try {
        if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
            return;
        }

        for (const auto &entry: std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string filePath = entry.path().string();
                if (isValidPluginFile(filePath)) {
                    pluginFiles.push_back(filePath);
                }
            }
        }
    } catch (const std::exception &e) {
        setLastError("Error scanning directory: " + std::string(e.what()));
    }
}

bool PluginLoader::initializePlugin(const std::string &pluginName) {
    auto pluginInfo = plugins_.get(pluginName);
    if (!pluginInfo) {
        setLastError("Plugin not found: " + pluginName);
        return false;
    }

    const auto &info = *pluginInfo;

    if (info.isInitialized.load()) {
        return true; // Already initialized
    }

    try {
        if (info.instance->initialize()) {
            // Note: This is a problem - we can't modify atomic through const reference
            // The cache implementation needs to allow mutable access
            // For now, we'll work around this limitation
            const_cast<PluginInfo &>(info).isInitialized.store(true); // Workaround
            initializedPluginCount_.fetch_add(1);

            // Cache the initialized plugin instance
            updatePluginCache(pluginName, info.instance);

            LOG(INFO) << "Plugin initialized: " << pluginName;
            return true;
        } else {
            setLastError("Plugin initialization failed: " + pluginName);
            return false;
        }
    } catch (const std::exception &e) {
        setLastError("Exception during plugin initialization: " + std::string(e.what()));
        return false;
    }
}

bool PluginLoader::shutdownPlugin(const std::string &pluginName) {
    auto pluginInfo = plugins_.get(pluginName);
    if (!pluginInfo) {
        setLastError("Plugin not found: " + pluginName);
        return false;
    }

    const auto &info = *pluginInfo;

    if (!info.isInitialized.load()) {
        return true; // Already shut down
    }

    try {
        info.instance->shutdown();
        const_cast<PluginInfo &>(info).isInitialized.store(false); // Workaround
        initializedPluginCount_.fetch_sub(1);

        // Remove from instance cache
        invalidatePluginCache(pluginName);

        LOG(INFO) << "Plugin shut down: " << pluginName;
        return true;
    } catch (const std::exception &e) {
        setLastError("Exception during plugin shutdown: " + std::string(e.what()));
        return false;
    }
}

void PluginLoader::initializeAllPlugins() {
    plugins_.forEach([&](const std::string &name, const PluginInfo &info) {
        try {
            if (info.instance->initialize()) {
                const_cast<PluginInfo &>(info).isInitialized.store(true); // Workaround
                initializedPluginCount_.fetch_add(1);
                updatePluginCache(name, info.instance);
                LOG(INFO) << "Plugin initialized: " << name;
            } else {
                LOG(ERROR) << "Plugin initialization failed: " << name;
            }
        } catch (const std::exception &e) {
            LOG(ERROR) << "Exception during plugin initialization: " << e.what();
        }
    });
}

void PluginLoader::shutdownAllPlugins() {
    std::vector<std::string> pluginNames;
    plugins_.forEach([&](const std::string &name, const PluginInfo & /*info*/) {
        pluginNames.push_back(name);
    });

    for (const auto &name: pluginNames) {
        shutdownPlugin(name);
    }
}

IPlugin *PluginLoader::getPlugin(const std::string &pluginName) {
    // Try cache first for maximum performance
    auto cachedPlugin = pluginInstanceCache_.get(pluginName);
    if (cachedPlugin) {
        return *cachedPlugin;
    }

    // Fallback to plugin storage
    auto pluginInfo = plugins_.get(pluginName);
    if (pluginInfo && pluginInfo->isInitialized.load()) {
        IPlugin *plugin = pluginInfo->instance;
        updatePluginCache(pluginName, plugin);
        return plugin;
    }

    return nullptr;
}

HttpResponse PluginLoader::routeRequest(const HttpRequest &request) {
    // First, try cached route -> plugin mapping
    auto cachedPluginName = routeToPluginCache_.get(request.path);
    if (cachedPluginName) {
        auto cachedPlugin = pluginInstanceCache_.get(*cachedPluginName);
        if (cachedPlugin) {
            try {
                HttpResponse response = (*cachedPlugin)->handleRequest(request);
                if (response.handled) {
                    return response;
                }
                // Plugin no longer handles this route, remove from cache
                routeToPluginCache_.remove(request.path);
            } catch (const std::exception &e) {
                LOG(ERROR) << "Exception in cached plugin " << *cachedPluginName << ": " << e.what();
                invalidatePluginCache(*cachedPluginName);
                routeToPluginCache_.remove(request.path);
            }
        }
    }

    // Try all initialized plugins
    HttpResponse foundResponse;
    bool responseFound = false;

    plugins_.forEach([&](const std::string &name, const PluginInfo &info) {
        if (!responseFound && info.isInitialized.load()) {
            try {
                IPlugin *plugin = info.instance;
                HttpResponse response = plugin->handleRequest(request);
                if (response.handled) {
                    // Cache successful route mapping
                    routeToPluginCache_.put(request.path, name);
                    updatePluginCache(name, plugin);
                    foundResponse = response;
                    responseFound = true;
                }
            } catch (const std::exception &e) {
                LOG(ERROR) << "Exception in plugin " << name << ": " << e.what();
                invalidatePluginCache(name);
            } catch (...) {
                LOG(ERROR) << "Unknown exception in plugin " << name;
                invalidatePluginCache(name);
            }
        }
    });

    if (responseFound) {
        return foundResponse;
    }

    // No plugin could handle the request
    HttpResponse response;
    response.statusCode = 404;
    response.body = "No plugin found to handle request: " + request.path;
    response.headers["Content-Type"] = "text/plain";
    response.handled = false;
    return response;
}

std::string PluginLoader::getLastError() {
    return lastError_;
}

void PluginLoader::clearLastError() {
    lastError_.clear();
}

// Private helper methods
bool PluginLoader::loadLibrary(const std::string &filePath, LIBRARY_HANDLE&handle) {
    handle = LOAD_LIBRARY(filePath.c_str());
    if (!handle) {
        setLastError("Failed to load library: " + filePath + " - " + getSystemError());
        return false;
    }
    return true;
}

bool PluginLoader::getFunctionPointers(LIBRARY_HANDLE handle, CreatePluginFunc &createFunc,
                                       DestroyPluginFunc &destroyFunc) {
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

void PluginLoader::setLastError(const std::string &error) {
    lastError_ = error;
}

std::string PluginLoader::getSystemError() {
#ifdef _WIN32
    DWORD error = GetLastError();
    if (error == 0) return "No error";

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
#else
    const char *error = dlerror();
    return error ? std::string(error) : "No error";
#endif
}

bool PluginLoader::isValidPluginFile(const std::string &filePath) {
    std::string extension = std::filesystem::path(filePath).extension().string();
    return extension == LIBRARY_EXTENSION;
}

void PluginLoader::updatePluginCache(const std::string &pluginName, IPlugin *plugin) {
    if (plugin) {
        pluginInstanceCache_.put(pluginName, plugin);
    }
}

void PluginLoader::invalidatePluginCache(const std::string &pluginName) {
    pluginInstanceCache_.remove(pluginName);
}

void PluginLoader::invalidateRouteCache() {
    routeToPluginCache_ = utils::ConcurrentLRUCache<std::string, std::string>{5000};
}

PluginInfo PluginLoader::createPluginInfo(const std::string &filePath,
                                          LIBRARY_HANDLE handle,
                                          CreatePluginFunc createFunc,
                                          DestroyPluginFunc destroyFunc) {
    IPlugin *plugin;
    try {
        plugin = createFunc();
        if (!plugin) {
            setLastError("Failed to create plugin instance");
            return PluginInfo{}; // Return empty PluginInfo to indicate failure
        }
    } catch (const std::exception &e) {
        setLastError("Exception during plugin creation: " + std::string(e.what()));
        return PluginInfo{}; // Return empty PluginInfo to indicate failure
    }

    PluginInfo info;
    info.name = plugin->getName();
    info.version = plugin->getVersion();
    info.filePath = filePath;
    info.handle = handle;
    info.instance = plugin;
    info.createFunc = createFunc;
    info.destroyFunc = destroyFunc;
    info.isLoaded.store(true);
    info.isInitialized.store(false);

    return info;
}
