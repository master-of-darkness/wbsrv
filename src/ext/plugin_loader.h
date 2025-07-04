#pragma once

#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <folly/dynamic.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <shared_mutex>

#include "../include/interface.h"

namespace PluginManager {
    class ProxygenBridge {
    public:
        static HttpMethod convertMethod(proxygen::HTTPMethod method);

        static HttpRequest convertRequest(const proxygen::HTTPMessage &msg, const std::string &body = "");

        static void applyResponse(const HttpResponse &response, proxygen::ResponseBuilder &builder);
    };

    // Logger implementation using glog
    class GlogLogger : public ILogger {
    public:
        void info(const std::string &message) override;

        void warning(const std::string &message) override;

        void error(const std::string &message) override;

        void debug(const std::string &message) override;
    };

    // Internal structure for a registered hook
    struct HookInfo {
        std::string pluginName;
        HookFunction function;
        int priority{100}; // Lower numbers = higher priority
        bool enabled{true};
    };

    class HookManagerImpl : public HookManager {
    private:
        std::unordered_map<HookType, std::vector<HookInfo> > hooks_;
        mutable std::shared_mutex hooksMutex_;

    public:
        void registerHook(HookType type, const std::string &pluginName, HookFunction func, int priority = 100) override;

        void enableHook(const std::string &pluginName, bool enabled) override;

        void unregisterHooks(const std::string &pluginName);

        bool executeHooks(HookType type, RequestContext &context);

        std::vector<std::string> getHookPlugins(HookType type) const;

        size_t getHookCount(HookType type) const;
    };

    class PluginManager {
    private:
        struct LoadedPlugin {
            std::unique_ptr<IPlugin> plugin;
            void *handle{nullptr};
            std::string path;
            std::unordered_map<std::string, ConfigValue> config;
            PluginContext context;
        };

        std::unordered_map<std::string, LoadedPlugin> loadedPlugins_;
        HookManagerImpl &hookManager_;
        GlogLogger logger_;
        mutable std::shared_mutex pluginsMutex_;

    public:
        explicit PluginManager(HookManagerImpl &hookManager);

        ~PluginManager();

        bool loadPlugin(const std::string &path, const std::unordered_map<std::string, ConfigValue> &config);

        bool unloadPlugin(const std::string &name);

        void unloadAllPlugins();

        IPlugin *getPlugin(const std::string &name) const;
    };
} // namespace PluginManager
