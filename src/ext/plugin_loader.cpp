#include "plugin_loader.h"

#include <folly/json.h>
#include <glog/logging.h>
#include <dlfcn.h>
#include <algorithm>

namespace PluginManager {
    HttpMethod ProxygenBridge::convertMethod(proxygen::HTTPMethod method) {
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

    HttpRequest ProxygenBridge::convertRequest(const proxygen::HTTPMessage &msg, const std::string &body) {
        HttpRequest req;
        req.method = convertMethod(msg.getMethod().value());
        req.path = msg.getPath();
        req.query = msg.getQueryString();
        req.body = body;
        req.clientIP = msg.getClientIP().c_str();
        msg.getHeaders().forEach([&req](const std::string &name, const std::string &value) {
            req.headers[name] = value;
        });
        return req;
    }

    void ProxygenBridge::applyResponse(const HttpResponse &response, proxygen::ResponseBuilder &builder) {
        builder.status(response.statusCode, response.statusMessage);
        for (const auto &[name, value]: response.headers) {
            builder.header(name, value);
        }
        if (!response.body.empty()) {
            builder.body(response.body);
        }
    }

    void GlogLogger::info(const std::string &message) {
        LOG(INFO) << message;
    }

    void GlogLogger::warning(const std::string &message) {
        LOG(WARNING) << message;
    }

    void GlogLogger::error(const std::string &message) {
        LOG(ERROR) << message;
    }

    void GlogLogger::debug(const std::string &message) {
        VLOG(1) << message;
    }

    void HookManagerImpl::registerHook(HookType type, const std::string &pluginName, HookFunction func, int priority) {
        std::unique_lock lock(hooksMutex_);
        HookInfo info{pluginName, std::move(func), priority};
        hooks_[type].push_back(std::move(info));
        std::sort(hooks_[type].begin(), hooks_[type].end(), [](const HookInfo &a, const HookInfo &b) {
            return a.priority < b.priority;
        });
    }

    void HookManagerImpl::unregisterHooks(const std::string &pluginName) {
        std::unique_lock lock(hooksMutex_);
        for (auto &[type, hookList]: hooks_) {
            hookList.erase(std::remove_if(hookList.begin(), hookList.end(), [&pluginName](const HookInfo &info) {
                return info.pluginName == pluginName;
            }), hookList.end());
        }
    }

    bool HookManagerImpl::executeHooks(HookType type, RequestContext &context) {
        std::shared_lock lock(hooksMutex_);
        auto it = hooks_.find(type);
        if (it == hooks_.end()) return true;
        for (const auto &hookInfo: it->second) {
            if (!hookInfo.enabled) continue;
            try {
                if (!hookInfo.function(context)) return false;
            } catch (const std::exception &e) {
                VLOG(1) << "Hook execution failed for plugin " << hookInfo.pluginName << ": " << e.what();
            }
        }
        return true;
    }

    void HookManagerImpl::enableHook(const std::string &pluginName, bool enabled) {
        std::unique_lock lock(hooksMutex_);
        for (auto &[type, hookList]: hooks_) {
            for (auto &hookInfo: hookList) {
                if (hookInfo.pluginName == pluginName) {
                    hookInfo.enabled = enabled;
                }
            }
        }
    }

    std::vector<std::string> HookManagerImpl::getHookPlugins(HookType type) const {
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

    size_t HookManagerImpl::getHookCount(HookType type) const {
        std::shared_lock lock(hooksMutex_);
        auto it = hooks_.find(type);
        return it != hooks_.end() ? it->second.size() : 0;
    }

    PluginManager::PluginManager(HookManagerImpl &hookManager) : hookManager_(hookManager) {
    }

    PluginManager::~PluginManager() { unloadAllPlugins(); }

    bool PluginManager::loadPlugin(const std::string &path, const std::unordered_map<std::string, ConfigValue> &config) {
        std::unique_lock lock(pluginsMutex_);
        void *handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
            LOG(ERROR) << "Cannot load plugin " << path << ": " << dlerror();
            return false;
        }
        typedef IPlugin *(*create_plugin_t)();
        create_plugin_t createPlugin = (create_plugin_t) dlsym(handle, "createPlugin");
        if (!createPlugin) {
            LOG(ERROR) << "Cannot find createPlugin function in " << path;
            dlclose(handle);
            return false;
        }
        std::unique_ptr<IPlugin> plugin(createPlugin());
        if (!plugin) {
            LOG(ERROR) << "Failed to create plugin instance from " << path;
            dlclose(handle);
            return false;
        }
        std::string pluginName = plugin->getName();
        if (loadedPlugins_.count(pluginName)) {
            LOG(WARNING) << "Plugin " << pluginName << " is already loaded";
            dlclose(handle);
            return false;
        }

        if (!plugin->validateConfig(config)) {
            LOG(ERROR) << "Invalid configuration for plugin " << pluginName;
            dlclose(handle);
            return false;
        }
        PluginContext context{&hookManager_, &logger_, config};
        if (!plugin->initialize(config)) {
            LOG(ERROR) << "Failed to initialize plugin " << pluginName;
            dlclose(handle);
            return false;
        }
        plugin->registerHooks(hookManager_);
        loadedPlugins_[pluginName] = LoadedPlugin{std::move(plugin), handle, path, config, context};
        LOG(INFO) << "Successfully loaded plugin: " << pluginName
                << " v" << loadedPlugins_[pluginName].plugin->getVersion();
        return true;
    }

    bool PluginManager::unloadPlugin(const std::string &name) {
        std::unique_lock lock(pluginsMutex_);
        auto it = loadedPlugins_.find(name);
        if (it == loadedPlugins_.end()) return false;
        hookManager_.unregisterHooks(name);
        it->second.plugin->shutdown();
        if (it->second.handle) dlclose(it->second.handle);
        loadedPlugins_.erase(it);
        LOG(INFO) << "Unloaded plugin: " << name;
        return true;
    }

    void PluginManager::unloadAllPlugins() {
        std::unique_lock lock(pluginsMutex_);
        for (auto &[name, loadedPlugin]: loadedPlugins_) {
            hookManager_.unregisterHooks(name);
            loadedPlugin.plugin->shutdown();
            if (loadedPlugin.handle) dlclose(loadedPlugin.handle);
        }
        loadedPlugins_.clear();
    }

    IPlugin *PluginManager::getPlugin(const std::string &name) const {
        std::shared_lock lock(pluginsMutex_);
        auto it = loadedPlugins_.find(name);
        return (it != loadedPlugins_.end()) ? it->second.plugin.get() : nullptr;
    }
} // namespace PluginManager
