#pragma once

#include "../include/interface.h"
#include <embed/php_embed.h>
#include <SAPI.h>

#include <mutex>
#include <string>
#include <memory>
#include <thread>
#include <unordered_map>
#include <future>

/**
 * PHP Plugin for handling PHP script execution
 * Implements the IPlugin interface for async request handling
 */

class PHPPlugin : public IPlugin {
private:
    bool initialized = false;
    std::string web_root_path;
    
    // Static callback functions for PHP SAPI
    static std::pair<std::string, std::string> extractHeaderAndValue(const sapi_header_struct *h);
    static size_t php_ub_write(const char *str, size_t str_length);
    static void php_register_variables(zval *track_vars_array);
    static size_t php_read_post(char *buffer, size_t count_bytes);
    static int php_header_handler(sapi_header_struct *sapi_header,
                                 sapi_header_op_enum op,
                                 sapi_headers_struct *sapi_headers);
    
    // Helper methods
    static HttpMethod stringToHttpMethod(const std::string& method);
    static std::string httpMethodToString(HttpMethod method) ;
    void executeScript(const std::string& script_path, 
                      const HttpRequest& request, 
                      HttpResponse& response) const;
    
public:
    PHPPlugin() = default;
    ~PHPPlugin() override;
    
    // IPlugin interface implementation
    std::string getName() const override;
    std::string getVersion() const override;
    bool initialize() override;
    void shutdown() override;
    HttpResponse handleRequest(const HttpRequest& request) override;
    
    // Utility methods
    bool isInitialized() const;
};

extern "C" {
    IPlugin* createPlugin();
    void destroyPlugin(const IPlugin* plugin);
}