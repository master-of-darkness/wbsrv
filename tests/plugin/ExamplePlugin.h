#pragma once
#include "../../include/interface.h" // Include your plugin interface
#include <string>
#include <unordered_map>
#include <future>
#include <thread>
#include <chrono>
#include <sstream>
#include <random>
#include <iostream>

class ExamplePlugin : public IPlugin {
public:
    ExamplePlugin();
    virtual ~ExamplePlugin();

    // IPlugin interface implementation
    std::string getName() const override;
    std::string getVersion() const override;
    bool initialize() override;
    void shutdown() override;
    HttpResponse handleRequest(const HttpRequest& request) override;

private:
    // Plugin state
    bool isInitialized_;
    std::unordered_map<std::string, std::string> config_;
    std::mt19937 randomGenerator_;
    int requestCounter_;
    
    // Private helper methods
    HttpResponse handleGetRequest(const HttpRequest& request);
    HttpResponse handlePostRequest(const HttpRequest& request);
    HttpResponse handleApiStatus(const HttpRequest& request);
    HttpResponse handleApiData(const HttpRequest& request);
    HttpResponse handleApiRandom(const HttpRequest& request);
    HttpResponse handleApiEcho(const HttpRequest& request);
    HttpResponse handleNotFound(const HttpRequest& request);
    
    std::string getCurrentTime() const;
    std::string parseQueryParam(const std::string& query, const std::string& param) const;
};