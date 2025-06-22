#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>

namespace PluginManager {
    class HookManager;

    enum class HookType {
        PRE_REQUEST, // Before request processing
        POST_REQUEST, // After request received, before response
        PRE_RESPONSE, // Before response is sent
        POST_RESPONSE, // After response is sent
        ON_ERROR, // When an error occurs
        ON_CONNECT, // When client connects
        ON_DISCONNECT // When client disconnects
    };

    enum class HttpMethod {
        GET,
        POST,
        PUT,
        DELETE,
        HEAD,
        OPTIONS,
        PATCH,
        CONNECT,
        TRACE,
        UNKNOWN
    };

    struct HttpRequest {
        HttpMethod method;
        std::string path;
        std::string query;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        std::string clientIP;

        // Helper methods
        std::string getHeader(const std::string &name) const {
            auto it = headers.find(name);
            return (it != headers.end()) ? it->second : "";
        }

        bool hasHeader(const std::string &name) const {
            return headers.find(name) != headers.end();
        }
    };

    struct HttpResponse {
        int statusCode{200};
        std::string statusMessage{"OK"};
        std::unordered_map<std::string, std::string> headers;
        std::string body;

        // Helper methods
        void setHeader(const std::string &name, const std::string &value) {
            headers[name] = value;
        }

        void setStatus(int code, const std::string &message = "") {
            statusCode = code;
            if (!message.empty()) {
                statusMessage = message;
            }
        }

        void setJsonContent() {
            setHeader("Content-Type", "application/json");
        }

        void setTextContent() {
            setHeader("Content-Type", "text/plain");
        }
    };

    class ConfigValue {
    private:
        enum Type { STRING, INTEGER, DOUBLE, BOOLEAN, OBJECT, ARRAY, NULLVAL };

        Type type_;
        std::string stringVal_;
        int64_t intVal_;
        double doubleVal_;
        bool boolVal_;
        std::unordered_map<std::string, ConfigValue> objectVal_;
        std::vector<ConfigValue> arrayVal_;

    public:
        ConfigValue() : type_(NULLVAL) {
        }

        ConfigValue(const std::string &val) : type_(STRING), stringVal_(val) {
        }

        ConfigValue(const char *val) : type_(STRING), stringVal_(val) {
        }

        ConfigValue(int val) : type_(INTEGER), intVal_(val) {
        }

        ConfigValue(int64_t val) : type_(INTEGER), intVal_(val) {
        }

        ConfigValue(double val) : type_(DOUBLE), doubleVal_(val) {
        }

        ConfigValue(bool val) : type_(BOOLEAN), boolVal_(val) {
        }

        bool isString() const { return type_ == STRING; }
        bool isInt() const { return type_ == INTEGER; }
        bool isDouble() const { return type_ == DOUBLE; }
        bool isBool() const { return type_ == BOOLEAN; }
        bool isObject() const { return type_ == OBJECT; }
        bool isArray() const { return type_ == ARRAY; }
        bool isNull() const { return type_ == NULLVAL; }

        std::string asString() const { return isString() ? stringVal_ : ""; }
        int64_t asInt() const { return isInt() ? intVal_ : 0; }
        double asDouble() const { return isDouble() ? doubleVal_ : 0.0; }
        bool asBool() const { return isBool() ? boolVal_ : false; }

        ConfigValue &operator[](const std::string &key) {
            if (type_ != OBJECT) {
                type_ = OBJECT;
                objectVal_.clear();
            }
            return objectVal_[key];
        }

        const ConfigValue &operator[](const std::string &key) const {
            static ConfigValue nullValue;
            if (type_ != OBJECT) return nullValue;
            auto it = objectVal_.find(key);
            return (it != objectVal_.end()) ? it->second : nullValue;
        }

        bool hasKey(const std::string &key) const {
            return type_ == OBJECT && objectVal_.find(key) != objectVal_.end();
        }
    };

    struct RequestContext {
        HttpRequest *request{nullptr};
        HttpResponse *response{nullptr};
        std::unordered_map<std::string, ConfigValue> metadata;
        uint64_t requestId;
        std::chrono::steady_clock::time_point startTime;

        // Helper methods for plugins
        void setMetadata(const std::string &key, const ConfigValue &value) {
            metadata[key] = value;
        }

        ConfigValue getMetadata(const std::string &key) const {
            auto it = metadata.find(key);
            return (it != metadata.end()) ? it->second : ConfigValue();
        }

        bool hasMetadata(const std::string &key) const {
            return metadata.find(key) != metadata.end();
        }

        // Convenience methods
        std::chrono::milliseconds getElapsedTime() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);
        }
    };

    using HookFunction = std::function<bool(RequestContext &)>;

    class IPlugin {
    public:
        virtual ~IPlugin() = default;

        virtual bool initialize(const ConfigValue &config) = 0;

        virtual void shutdown() = 0;

        virtual std::string getName() const = 0;

        virtual std::string getVersion() const = 0;

        virtual std::string getDescription() const { return ""; }

        virtual void registerHooks(HookManager &hookManager) = 0;

        virtual bool validateConfig(const ConfigValue &config) const {
            return true; // Default: accept any config
        }

        virtual std::vector<std::string> getDependencies() const {
            return {}; // Default: no dependencies
        }

        virtual std::vector<std::string> getProvides() const {
            return {}; // Default: no services provided
        }
    };

    class HookManager {
    public:
        virtual ~HookManager() = default;

        virtual void registerHook(HookType type, const std::string &pluginName,
                                  HookFunction func, int priority = 100) = 0;

        virtual void enableHook(const std::string &pluginName, bool enabled) = 0;
    };

    class ILogger {
    public:
        virtual ~ILogger() = default;

        virtual void info(const std::string &message) = 0;

        virtual void warning(const std::string &message) = 0;

        virtual void error(const std::string &message) = 0;

        virtual void debug(const std::string &message) = 0;
    };

    struct PluginContext {
        HookManager *hookManager{nullptr};
        ILogger *logger{nullptr};
        ConfigValue config;

        // Plugin can store its own data here
        std::unordered_map<std::string, std::shared_ptr<void> > userData;

        template<typename T>
        void setUserData(const std::string &key, std::shared_ptr<T> data) {
            userData[key] = std::static_pointer_cast<void>(data);
        }

        template<typename T>
        std::shared_ptr<T> getUserData(const std::string &key) const {
            auto it = userData.find(key);
            if (it != userData.end()) {
                return std::static_pointer_cast<T>(it->second);
            }
            return nullptr;
        }
    };
}

extern "C" {
    PluginManager::IPlugin *createPlugin();

    void destroyPlugin(PluginManager::IPlugin *plugin);

    const char *getPluginName();

    const char *getPluginVersion();

    const char *getPluginDescription();

    int getPluginAPIVersion();
}
