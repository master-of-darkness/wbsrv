#include "ExamplePlugin.h"
#include <iomanip>
#include <algorithm>

ExamplePlugin::ExamplePlugin() 
    : isInitialized_(false)
    , randomGenerator_(std::chrono::steady_clock::now().time_since_epoch().count())
    , requestCounter_(0) {
    std::cout << "ExamplePlugin constructor called" << std::endl;
}

ExamplePlugin::~ExamplePlugin() {
    if (isInitialized_) {
        shutdown();
    }
    std::cout << "ExamplePlugin destructor called" << std::endl;
}

std::string ExamplePlugin::getName() const {
    return "ExamplePlugin";
}

std::string ExamplePlugin::getVersion() const {
    return "1.0.0";
}

bool ExamplePlugin::initialize() {
    if (isInitialized_) {
        return true;
    }
    
    std::cout << "Initializing " << getName() << " v" << getVersion() << std::endl;
    
    // Initialize plugin configuration
    config_["max_requests"] = "1000";
    config_["timeout_ms"] = "5000";
    config_["debug_mode"] = "true";
    config_["api_key"] = "example-api-key-12345";
    
    // Simulate some initialization work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    isInitialized_ = true;
    requestCounter_ = 0;
    
    std::cout << "ExamplePlugin initialized successfully" << std::endl;
    return true;
}

void ExamplePlugin::shutdown() {
    if (!isInitialized_) {
        return;
    }
    
    std::cout << "Shutting down " << getName() << std::endl;
    std::cout << "Total requests processed: " << requestCounter_ << std::endl;
    
    // Cleanup resources
    config_.clear();
    isInitialized_ = false;
    
    std::cout << "ExamplePlugin shut down successfully" << std::endl;
}

HttpResponse ExamplePlugin::handleRequest(const HttpRequest& request) {
    if (!isInitialized_) {
        HttpResponse response;
        response.statusCode = 503;
        response.headers["Content-Type"] = "application/json";
        response.body = R"({"error": "Plugin not initialized"})";
        response.handled = false;
        return response;
    }

    requestCounter_++;

    if (request.method == HttpMethod::GET) {
        return handleGetRequest(request);
    } else if (request.method == HttpMethod::POST) {
        return handlePostRequest(request);
    } else {
        HttpResponse response;
        response.statusCode = 405;
        response.headers["Content-Type"] = "application/json";
        response.headers["Allow"] = "GET, POST";
        response.body = R"({"error": "Method not allowed"})";
        response.handled = true;
        return response;
    }
}

HttpResponse ExamplePlugin::handleGetRequest(const HttpRequest& request) {
    if (request.path == "/api/status" || request.path == "/status") {
        return handleApiStatus(request);
    } else if (request.path == "/api/data" || request.path == "/data") {
        return handleApiData(request);
    } else if (request.path == "/api/random" || request.path == "/random") {
        return handleApiRandom(request);
    } else if (request.path.find("/api/") == 0) {
        // Handle any API path
        HttpResponse response;
        response.statusCode = 200;
        response.headers["Content-Type"] = "application/json";
        
        std::ostringstream oss;
        oss << R"({
    "message": "ExamplePlugin API",
    "version": ")" << getVersion() << R"(",
    "path": ")" << request.path << R"(",
    "timestamp": ")" << getCurrentTime() << R"(",
    "available_endpoints": [
        "/api/status",
        "/api/data",
        "/api/random"
    ]
})";
        
        response.body = oss.str();
        response.handled = true;
        return response;
    }
    
    return handleNotFound(request);
}

HttpResponse ExamplePlugin::handlePostRequest(const HttpRequest& request) {
    if (request.path == "/api/echo" || request.path == "/echo") {
        return handleApiEcho(request);
    }
    
    // Default POST handler
    HttpResponse response;
    response.statusCode = 200;
    response.headers["Content-Type"] = "application/json";
    
    std::ostringstream oss;
    oss << R"({
    "message": "POST request received",
    "plugin": ")" << getName() << R"(",
    "path": ")" << request.path << R"(",
    "body_length": )" << request.body.length() << R"(,
    "timestamp": ")" << getCurrentTime() << R"("
})";
    
    response.body = oss.str();
    response.handled = true;
    return response;
}

HttpResponse ExamplePlugin::handleApiStatus(const HttpRequest& request) {
    HttpResponse response;
    response.statusCode = 200;
    response.headers["Content-Type"] = "application/json";
    
    std::ostringstream oss;
    oss << R"({
    "status": "ok",
    "plugin": ")" << getName() << R"(",
    "version": ")" << getVersion() << R"(",
    "initialized": )" << (isInitialized_ ? "true" : "false") << R"(,
    "requests_processed": )" << requestCounter_ << R"(,
    "timestamp": ")" << getCurrentTime() << R"(",
    "uptime_info": "Plugin is running normally"
})";
    
    response.body = oss.str();
    response.handled = true;
    return response;
}

HttpResponse ExamplePlugin::handleApiData(const HttpRequest& request) {
    HttpResponse response;
    response.statusCode = 200;
    response.headers["Content-Type"] = "application/json";
    
    std::string format = parseQueryParam(request.query, "format");
    std::string limit = parseQueryParam(request.query, "limit");
    
    int limitNum = 5;
    if (!limit.empty()) {
        try {
            limitNum = std::stoi(limit);
            limitNum = std::max(1, std::min(limitNum, 100));
        } catch (...) {
            limitNum = 5;
        }
    }
    
    std::ostringstream oss;
    oss << R"({
    "data": [)";
    
    for (int i = 0; i < limitNum; ++i) {
        if (i > 0) oss << ",";
        oss << R"(
        {
            "id": )" << (i + 1) << R"(,
            "name": "Item )" << (i + 1) << R"(",
            "value": )" << (randomGenerator_() % 1000) << R"(,
            "active": )" << ((i % 2 == 0) ? "true" : "false") << R"(
        })";
    }
    
    oss << R"(
    ],
    "total": )" << limitNum << R"(,
    "format": ")" << (format.empty() ? "default" : format) << R"(",
    "timestamp": ")" << getCurrentTime() << R"("
})";
    
    response.body = oss.str();
    response.handled = true;
    return response;
}

HttpResponse ExamplePlugin::handleApiRandom(const HttpRequest& request) {
    HttpResponse response;
    response.statusCode = 200;
    response.headers["Content-Type"] = "application/json";
    
    // Generate random data
    std::uniform_int_distribution<int> dist(1, 1000);
    std::uniform_real_distribution<float> floatDist(0.0f, 100.0f);
    
    std::ostringstream oss;
    oss << R"({
    "random_int": )" << dist(randomGenerator_) << R"(,
    "random_float": )" << std::fixed << std::setprecision(2) << floatDist(randomGenerator_) << R"(,
    "random_bool": )" << ((randomGenerator_() % 2) ? "true" : "false") << R"(,
    "timestamp": ")" << getCurrentTime() << R"(",
    "request_id": ")" << requestCounter_ << R"("
})";
    
    response.body = oss.str();
    response.handled = true;
    return response;
}

HttpResponse ExamplePlugin::handleApiEcho(const HttpRequest& request) {
    HttpResponse response;
    response.statusCode = 200;
    response.headers["Content-Type"] = "application/json";
    
    std::ostringstream oss;
    oss << R"({
    "echo": {
        "method": ")" << (request.method == HttpMethod::GET ? "GET" :
                         request.method == HttpMethod::POST ? "POST" :
                         request.method == HttpMethod::PUT ? "PUT" :
                         request.method == HttpMethod::DELETE ? "DELETE" :
                         request.method == HttpMethod::PATCH ? "PATCH" :
                         request.method == HttpMethod::OPTIONS ? "OPTIONS" :
                         request.method == HttpMethod::HEAD ? "HEAD" :
                         request.method == HttpMethod::TRACE ? "TRACE" :
                         request.method == HttpMethod::CONNECT ? "CONNECT" :
                         request.method == HttpMethod::CONNECT_UDP ? "CONNECT_UDP" :
                         request.method == HttpMethod::SUB ? "SUB" :
                         request.method == HttpMethod::PUB ? "PUB" :
                         request.method == HttpMethod::UNSUB ? "UNSUB" : "UNKNOWN") << R"(",
        "path": ")" << request.path << R"(",
        "query": ")" << request.query << R"(",
        "body": ")" << request.body << R"(",
        "headers": {)";

    // Use the IHeaders interface to iterate through headers
    bool first = true;
    if (request.headers) {
        request.headers->forEach([&](const std::string& key, const std::string& value) {
            if (!first) oss << ",";
            oss << R"(
            ")" << key << R"(": ")" << value << R"(")";
            first = false;
        });
    }
    
    oss << R"(
        }
    },
    "plugin_info": {
        "name": ")" << getName() << R"(",
        "version": ")" << getVersion() << R"("
    },
    "timestamp": ")" << getCurrentTime() << R"("
})";
    
    response.body = oss.str();
    response.handled = true;
    return response;
}

HttpResponse ExamplePlugin::handleNotFound(const HttpRequest& request) {
    HttpResponse response;
    response.statusCode = 404;
    response.headers["Content-Type"] = "application/json";
    
    std::ostringstream oss;
    oss << R"({
    "error": "Not Found",
    "message": "The requested path was not found in ExamplePlugin",
    "path": ")" << request.path << R"(",
    "plugin": ")" << getName() << R"(",
    "timestamp": ")" << getCurrentTime() << R"("
})";
    
    response.body = oss.str();
    response.handled = false; // Indicate this plugin didn't handle the request
    return response;
}

std::string ExamplePlugin::getCurrentTime() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return oss.str();
}

std::string ExamplePlugin::parseQueryParam(const std::string& query, const std::string& param) const {
    if (query.empty()) return "";
    
    std::string searchParam = param + "=";
    size_t pos = query.find(searchParam);
    if (pos == std::string::npos) return "";
    
    pos += searchParam.length();
    size_t endPos = query.find('&', pos);
    if (endPos == std::string::npos) {
        return query.substr(pos);
    }
    return query.substr(pos, endPos - pos);
}


// Export functions for plugin loading
extern "C" {
    IPlugin* createPlugin() {
        return new ExamplePlugin();
    }
    
    void destroyPlugin(const IPlugin* plugin) {
        delete plugin;
    }
}