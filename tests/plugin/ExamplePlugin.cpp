#include "interface.h"
#include <iostream>
#include <sstream>

using namespace PluginManager;

class HelloWorldPlugin : public IPlugin {
private:
    std::string greeting_;
    bool logRequests_;

public:
    HelloWorldPlugin() : greeting_("Hello, World!"), logRequests_(true) {}

    bool initialize(const std::unordered_map<std::string, ConfigValue> &config) override {
        // Read configuration
        auto greetingIt = config.find("greeting");
        if (greetingIt != config.end() && greetingIt->second.isString()) {
            greeting_ = greetingIt->second.asString();
        }

        auto logRequestsIt = config.find("logRequests");
        if (logRequestsIt != config.end() && logRequestsIt->second.isBool()) {
            logRequests_ = logRequestsIt->second.asBool();
        }

        return true;
    }

    void shutdown() override {
        // Clean up resources if needed
    }

    std::string getName() const override {
        return "HelloWorldPlugin";
    }

    std::string getVersion() const override {
        return "1.0.0";
    }

    std::string getDescription() const override {
        return "Simple hello world plugin";
    }

    void registerHooks(HookManager &hookManager) override {
        hookManager.registerHook(HookType::PRE_REQUEST, getName(),
            [this](RequestContext &ctx) -> bool {
                return this->handlePreRequest(ctx);
            }, 50);

        if (logRequests_) {
            hookManager.registerHook(HookType::POST_REQUEST, getName(),
                [this](RequestContext &ctx) -> bool {
                    return this->logRequest(ctx);
                }, 200); // Lower priority
        }
    }

    bool validateConfig(const std::unordered_map<std::string, ConfigValue> &config) const override {
        // Check if greeting exists and validate its type
        auto greetingIt = config.find("greeting");
        if (greetingIt != config.end() && !greetingIt->second.isString()) {
            return false;
        }

        // Check if logRequests exists and validate its type
        auto logRequestsIt = config.find("logRequests");
        if (logRequestsIt != config.end() && !logRequestsIt->second.isBool()) {
            return false;
        }

        return true;
    }

private:
    bool handlePreRequest(RequestContext &ctx) {
        if (ctx.request->path != "/hello") {
            return true; // Continue processing
        }

        ctx.response->setJsonContent();
        ctx.response->setStatus(200, "OK");

        std::ostringstream json;
        json << "{\n";
        json << "  \"message\": \"" << greeting_ << "\",\n";
        json << "  \"plugin\": \"" << getName() << "\",\n";
        json << "  \"version\": \"" << getVersion() << "\",\n";
        json << "  \"method\": \"" << methodToString(ctx.request->method) << "\",\n";
        json << "  \"clientIP\": \"" << ctx.request->clientIP << "\",\n";
        json << "  \"requestId\": " << ctx.requestId << ",\n";
        json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
        json << "}";

        ctx.response->body = json.str();

        return true;
    }

    bool logRequest(RequestContext &ctx) {
        // Log the request (in a real implementation, you'd use the logger interface)
        std::cout << "[HelloWorldPlugin] Request: "
                  << methodToString(ctx.request->method)
                  << " " << ctx.request->path
                  << " from " << ctx.request->clientIP
                  << " (ID: " << ctx.requestId << ")"
                  << std::endl;

        return true; // Continue processing
    }

    std::string methodToString(HttpMethod method) const {
        switch (method) {
            case HttpMethod::GET: return "GET";
            case HttpMethod::POST: return "POST";
            case HttpMethod::PUT: return "PUT";
            case HttpMethod::DELETE: return "DELETE";
            case HttpMethod::HEAD: return "HEAD";
            case HttpMethod::OPTIONS: return "OPTIONS";
            case HttpMethod::PATCH: return "PATCH";
            case HttpMethod::CONNECT: return "CONNECT";
            case HttpMethod::TRACE: return "TRACE";
            default: return "UNKNOWN";
        }
    }
};

// Plugin factory functions (C interface)
extern "C" {
    PluginManager::IPlugin* createPlugin() {
        return new HelloWorldPlugin();
    }

    void destroyPlugin(PluginManager::IPlugin* plugin) {
        delete plugin;
    }

    const char* getPluginName() {
        return "HelloWorldPlugin";
    }

    const char* getPluginVersion() {
        return "1.0.0";
    }

    const char* getPluginDescription() {
        return "A simple hello world plugin that demonstrates the plugin API";
    }

    int getPluginAPIVersion() {
        return 1; // API version this plugin was built against, currently no need
    }
}
