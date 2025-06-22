#pragma once
#include <proxygen/httpserver/HTTPServer.h>
#include <folly/Memory.h>
#include <folly/json.h>
#include <folly/dynamic.h>
#include <glog/logging.h>

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <dlfcn.h>
#include <filesystem>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <algorithm>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "../include/interface.h"

namespace PluginManager {
    class ProxygenBridge {
    public:
        static HttpMethod convertMethod(proxygen::HTTPMethod method) {
            switch (method) {
                case proxygen::HTTPMethod::GET: return HttpMethod::GET;
                case proxygen::HTTPMethod::POST: return HttpMethod::POST;
                case proxygen::HTTPMethod::PUT: return HttpMethod::PUT;
                case proxygen::HTTPMethod::DELETE: return HttpMethod::DELETE;
                case proxygen::HTTPMethod::HEAD: return HttpMethod::HEAD;
                case proxygen::HTTPMethod::OPTIONS: return HttpMethod::OPTIONS;
                case proxygen::HTTPMethod::PATCH: return HttpMethod::PATCH;
                case proxygen::HTTPMethod::CONNECT: return HttpMethod::CONNECT;
                case proxygen::HTTPMethod::TRACE: return HttpMethod::TRACE;
                default: return HttpMethod::UNKNOWN;
            }
        }

        static HttpRequest convertRequest(const proxygen::HTTPMessage &msg, const std::string &body = "") {
            HttpRequest req;
            req.method = convertMethod(msg.getMethod().value());
            req.path = msg.getPath();
            req.query = msg.getQueryString();
            req.body = body;
            req.clientIP = msg.getClientIP().c_str();

            // Convert headers
            msg.getHeaders().forEach([&req](const std::string &name, const std::string &value) {
                req.headers[name] = value;
            });

            return req;
        }

        static void applyResponse(const HttpResponse &response, proxygen::ResponseBuilder &builder) {
            builder.status(response.statusCode, response.statusMessage);

            for (const auto &[name, value]: response.headers) {
                builder.header(name, value);
            }

            if (!response.body.empty()) {
                builder.body(response.body);
            }
        }

        static ConfigValue convertFromFolly(const folly::dynamic &d) {
            if (d.isString()) {
                return ConfigValue(d.asString());
            } else if (d.isInt()) {
                return ConfigValue(d.asInt());
            } else if (d.isDouble()) {
                return ConfigValue(d.asDouble());
            } else if (d.isBool()) {
                return ConfigValue(d.asBool());
            } else if (d.isObject()) {
                ConfigValue obj;
                for (const auto &[key, value]: d.items()) {
                    obj[key.asString()] = convertFromFolly(value);
                }
                return obj;
            }
            return ConfigValue(); // null
        }

        static folly::dynamic convertToFolly(const ConfigValue &c) {
            if (c.isString()) {
                return folly::dynamic(c.asString());
            } else if (c.isInt()) {
                return folly::dynamic(c.asInt());
            } else if (c.isDouble()) {
                return folly::dynamic(c.asDouble());
            } else if (c.isBool()) {
                return folly::dynamic(c.asBool());
            }
            return folly::dynamic(); // null
        }
    };

    // Logger implementation using glog
    class GlogLogger : public ILogger {
    public:
        void info(const std::string &message) override {
            LOG(INFO) << message;
        }

        void warning(const std::string &message) override {
            LOG(WARNING) << message;
        }

        void error(const std::string &message) override {
            LOG(ERROR) << message;
        }

        void debug(const std::string &message) override {
            VLOG(1) << message;
        }
    };

    // Hook registration info
    struct HookInfo {
        std::string pluginName;
        HookFunction function;
        int priority{100}; // Lower numbers = higher priority
        bool enabled{true};
    };

    // Concrete Hook Manager implementation
    class HookManagerImpl : public HookManager {
    private:
        std::unordered_map<HookType, std::vector<HookInfo> > hooks_;
        mutable std::shared_mutex hooksMutex_;

    public:
        void registerHook(HookType type, const std::string &pluginName,
                          HookFunction func, int priority = 100) override {
            std::unique_lock lock(hooksMutex_);

            HookInfo info;
            info.pluginName = pluginName;
            info.function = std::move(func);
            info.priority = priority;

            hooks_[type].push_back(std::move(info));

            // Sort by priority (lower number = higher priority)
            std::sort(hooks_[type].begin(), hooks_[type].end(),
                      [](const HookInfo &a, const HookInfo &b) {
                          return a.priority < b.priority;
                      });
        }

        void unregisterHooks(const std::string &pluginName) {
            std::unique_lock lock(hooksMutex_);

            for (auto &[type, hookList]: hooks_) {
                hookList.erase(
                    std::remove_if(hookList.begin(), hookList.end(),
                                   [&pluginName](const HookInfo &info) {
                                       return info.pluginName == pluginName;
                                   }),
                    hookList.end());
            }
        }

        bool executeHooks(HookType type, RequestContext &context) {
            std::shared_lock lock(hooksMutex_);

            auto it = hooks_.find(type);
            if (it == hooks_.end()) {
                return true; // No hooks registered, continue
            }

            for (const auto &hookInfo: it->second) {
                if (!hookInfo.enabled) continue;

                try {
                    if (!hookInfo.function(context)) {
                        // Hook returned false, stop processing
                        return false;
                    }
                } catch (const std::exception &e) {
                    VLOG(1) << "Hook execution failed for plugin "
                        << hookInfo.pluginName << ": " << e.what();
                    // Continue with other hooks on error
                }
            }

            return true;
        }

        void enableHook(const std::string &pluginName, bool enabled) override {
            std::unique_lock lock(hooksMutex_);

            for (auto &[type, hookList]: hooks_) {
                for (auto &hookInfo: hookList) {
                    if (hookInfo.pluginName == pluginName) {
                        hookInfo.enabled = enabled;
                    }
                }
            }
        }

        // Additional methods for server management
        std::vector<std::string> getHookPlugins(HookType type) const {
            std::shared_lock lock(hooksMutex_);
            std::vector<std::string> plugins;

            auto it = hooks_.find(type);
            if (it != hooks_.end()) {
                for (const auto &hookInfo: it->second) {
                    plugins.push_back(hookInfo.pluginName);
                }
            }

            return plugins;
        }

        size_t getHookCount(HookType type) const {
            std::shared_lock lock(hooksMutex_);
            auto it = hooks_.find(type);
            return (it != hooks_.end()) ? it->second.size() : 0;
        }
    };

    // Plugin Manager - handles loading/unloading plugins
    class PluginManager {
    private:
        struct LoadedPlugin {
            std::unique_ptr<IPlugin> plugin;
            void *handle{nullptr};
            std::string path;
            ConfigValue config;
            std::vector<std::string> dependencies;
            PluginContext context;
        };

        std::unordered_map<std::string, LoadedPlugin> loadedPlugins_;
        HookManagerImpl &hookManager_;
        GlogLogger logger_;
        mutable std::shared_mutex pluginsMutex_;

    public:
        explicit PluginManager(HookManagerImpl &hookManager)
            : hookManager_(hookManager) {
        }

        ~PluginManager() {
            unloadAllPlugins();
        }

        bool loadPlugin(const std::string &path, const folly::dynamic &config = folly::dynamic::object()) {
            std::unique_lock lock(pluginsMutex_);

            void *handle = dlopen(path.c_str(), RTLD_LAZY);
            if (!handle) {
                LOG(ERROR) << "Cannot load plugin " << path << ": " << dlerror();
                return false;
            }

            // Get the create function
            typedef IPlugin * (*create_plugin_t)();
            create_plugin_t createPlugin = (create_plugin_t) dlsym(handle, "createPlugin");

            if (!createPlugin) {
                LOG(ERROR) << "Cannot find createPlugin function in " << path;
                dlclose(handle);
                return false;
            }

            // Create plugin instance
            std::unique_ptr<IPlugin> plugin(createPlugin());
            if (!plugin) {
                LOG(ERROR) << "Failed to create plugin instance from " << path;
                dlclose(handle);
                return false;
            }

            std::string pluginName = plugin->getName();

            // Check if plugin is already loaded
            if (loadedPlugins_.find(pluginName) != loadedPlugins_.end()) {
                LOG(WARNING) << "Plugin " << pluginName << " is already loaded";
                dlclose(handle);
                return false;
            }

            // Convert config
            ConfigValue pluginConfig = ProxygenBridge::convertFromFolly(config);

            // Validate configuration
            if (!plugin->validateConfig(pluginConfig)) {
                LOG(ERROR) << "Invalid configuration for plugin " << pluginName;
                dlclose(handle);
                return false;
            }

            // Check dependencies
            auto dependencies = plugin->getDependencies();
            for (const auto &dep: dependencies) {
                if (loadedPlugins_.find(dep) == loadedPlugins_.end()) {
                    LOG(ERROR) << "Plugin " << pluginName << " depends on " << dep
                            << " which is not loaded";
                    dlclose(handle);
                    return false;
                }
            }

            // Setup plugin context
            PluginContext context;
            context.hookManager = &hookManager_;
            context.logger = &logger_;
            context.config = pluginConfig;

            // Initialize plugin
            if (!plugin->initialize(pluginConfig)) {
                LOG(ERROR) << "Failed to initialize plugin " << pluginName;
                dlclose(handle);
                return false;
            }

            // Register hooks
            plugin->registerHooks(hookManager_);

            // Store loaded plugin
            LoadedPlugin loadedPlugin;
            loadedPlugin.plugin = std::move(plugin);
            loadedPlugin.handle = handle;
            loadedPlugin.path = path;
            loadedPlugin.config = pluginConfig;
            loadedPlugin.dependencies = dependencies;
            loadedPlugin.context = std::move(context);

            loadedPlugins_[pluginName] = std::move(loadedPlugin);

            LOG(INFO) << "Successfully loaded plugin: " << pluginName
                    << " v" << loadedPlugins_[pluginName].plugin->getVersion();
            return true;
        }

        bool unloadPlugin(const std::string &name) {
            std::unique_lock lock(pluginsMutex_);

            auto it = loadedPlugins_.find(name);
            if (it == loadedPlugins_.end()) {
                return false;
            }

            // Check if other plugins depend on this one
            for (const auto &[pluginName, loadedPlugin]: loadedPlugins_) {
                if (pluginName != name) {
                    auto &deps = loadedPlugin.dependencies;
                    if (std::find(deps.begin(), deps.end(), name) != deps.end()) {
                        LOG(ERROR) << "Cannot unload plugin " << name
                                << " because " << pluginName << " depends on it";
                        return false;
                    }
                }
            }

            // Unregister hooks
            hookManager_.unregisterHooks(name);

            // Shutdown plugin
            it->second.plugin->shutdown();

            // Close library
            if (it->second.handle) {
                dlclose(it->second.handle);
            }

            loadedPlugins_.erase(it);
            LOG(INFO) << "Unloaded plugin: " << name;
            return true;
        }

        void unloadAllPlugins() {
            std::unique_lock lock(pluginsMutex_);

            // Unload in reverse dependency order
            std::vector<std::string> toUnload;
            for (const auto &[name, _]: loadedPlugins_) {
                toUnload.push_back(name);
            }

            // Simple approach: keep trying to unload until all are gone
            while (!toUnload.empty()) {
                size_t initialSize = toUnload.size();

                for (auto it = toUnload.begin(); it != toUnload.end();) {
                    const std::string &name = *it;
                    auto pluginIt = loadedPlugins_.find(name);

                    if (pluginIt != loadedPlugins_.end()) {
                        // Check dependencies
                        bool canUnload = true;
                        for (const auto &[otherName, otherPlugin]: loadedPlugins_) {
                            if (otherName != name) {
                                auto &deps = otherPlugin.dependencies;
                                if (std::find(deps.begin(), deps.end(), name) != deps.end()) {
                                    canUnload = false;
                                    break;
                                }
                            }
                        }

                        if (canUnload) {
                            hookManager_.unregisterHooks(name);
                            pluginIt->second.plugin->shutdown();
                            if (pluginIt->second.handle) {
                                dlclose(pluginIt->second.handle);
                            }
                            loadedPlugins_.erase(pluginIt);
                            it = toUnload.erase(it);
                            continue;
                        }
                    }
                    ++it;
                }

                // If we couldn't unload anything, break to avoid infinite loop
                if (toUnload.size() == initialSize) {
                    LOG(WARNING) << "Could not unload all plugins due to circular dependencies";
                    break;
                }
            }

            loadedPlugins_.clear();
        }

        bool reloadPlugin(const std::string &name) {
            std::shared_lock lock(pluginsMutex_);

            auto it = loadedPlugins_.find(name);
            if (it == loadedPlugins_.end()) {
                return false;
            }

            std::string path = it->second.path;
            folly::dynamic config = ProxygenBridge::convertToFolly(it->second.config);

            lock.unlock();

            return unloadPlugin(name) && loadPlugin(path, config);
        }

        std::vector<std::string> getLoadedPlugins() const {
            std::shared_lock lock(pluginsMutex_);

            std::vector<std::string> plugins;
            plugins.reserve(loadedPlugins_.size());

            for (const auto &[name, _]: loadedPlugins_) {
                plugins.push_back(name);
            }

            return plugins;
        }

        IPlugin *getPlugin(const std::string &name) const {
            std::shared_lock lock(pluginsMutex_);

            auto it = loadedPlugins_.find(name);
            return (it != loadedPlugins_.end()) ? it->second.plugin.get() : nullptr;
        }

        folly::dynamic getPluginInfo(const std::string &name) const {
            std::shared_lock lock(pluginsMutex_);

            auto it = loadedPlugins_.find(name);
            if (it == loadedPlugins_.end()) {
                return folly::dynamic();
            }

            return folly::dynamic::object
                    ("name", it->second.plugin->getName())
                    ("version", it->second.plugin->getVersion())
                    ("description", it->second.plugin->getDescription())
                    ("path", it->second.path)
                    ("dependencies", folly::dynamic(it->second.dependencies.begin(),
                                                    it->second.dependencies.end()));
        }

        folly::dynamic getAllPluginsInfo() const {
            std::shared_lock lock(pluginsMutex_);

            folly::dynamic plugins = folly::dynamic::array();
            for (const auto &[name, loadedPlugin]: loadedPlugins_) {
                plugins.push_back(getPluginInfo(name));
            }

            return plugins;
        }
    };
}
