#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <memory>

#include "interface.h"
#include <main/php.h>
#include <main/SAPI.h>
#include <main/php_main.h>
#include <main/php_variables.h>
#include <zend_ini.h>

using namespace PluginManager;

thread_local std::stringstream *current_output_buffer = nullptr;
thread_local std::unordered_map<std::string, std::string> tl_headers_to_send;
thread_local RequestContext *tl_context = nullptr;
thread_local volatile size_t read_post_offset = 0;

HttpMethod stringToHttpMethod(const std::string &method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "PATCH") return HttpMethod::PATCH;
    if (method == "CONNECT") return HttpMethod::CONNECT;
    if (method == "TRACE") return HttpMethod::TRACE;
    return HttpMethod::UNKNOWN;
}

std::string httpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::PATCH: return "PATCH";
        case HttpMethod::TRACE: return "TRACE";
        case HttpMethod::CONNECT: return "CONNECT";
        default: return "GET";
    }
}

std::pair<std::string, std::string> extractHeaderAndValue(const sapi_header_struct *h) {
    if (!h || !h->header || h->header_len == 0) {
        return {"", ""};
    }

    std::string header;
    header.reserve(h->header_len);

    const char *start = h->header;
    const char *end = h->header + h->header_len;
    const char *colon_pos = nullptr;

    for (const char *it = start; it < end; ++it) {
        if (*it == ':') {
            colon_pos = it;
            break;
        }
        header += *it;
    }

    if (!colon_pos) {
        return {"", ""};
    }

    const char *value_start = colon_pos + 1;
    while (value_start < end && (*value_start == ' ' || *value_start == '\t')) {
        ++value_start;
    }

    const char *value_end = end;
    while (value_end > value_start && (*(value_end - 1) == ' ' || *(value_end - 1) == '\t' ||
                                       *(value_end - 1) == '\r' || *(value_end - 1) == '\n')) {
        --value_end;
    }

    std::string value(value_start, value_end);
    return {header, value};
}

static int wbsrv_php_startup(sapi_module_struct *sapi_module) {
    return php_module_startup(sapi_module, nullptr);
}

static int wbsrv_php_deactivate(void) {
    return SUCCESS;
}

static size_t wbsrv_php_ub_write(const char *str, size_t str_length) {
    if (current_output_buffer) {
        current_output_buffer->write(str, str_length);
    }
    return str_length;
}

static int wbsrv_php_header_handler(sapi_header_struct *sapi_header,
                                    sapi_header_op_enum op,
                                    sapi_headers_struct *sapi_headers) {
    if (!sapi_header) {
        return FAILURE;
    }

    auto [header, value] = extractHeaderAndValue(sapi_header);
    auto &headers_map = tl_headers_to_send;

    switch (op) {
        case SAPI_HEADER_DELETE:
            headers_map.erase(header);
            break;
        case SAPI_HEADER_ADD:
        case SAPI_HEADER_REPLACE:
            headers_map[header] = value;
            break;
        case SAPI_HEADER_DELETE_ALL:
            headers_map.clear();
            break;
        default:
            return FAILURE;
    }

    return SUCCESS;
}

static size_t wbsrv_php_read_post(char *buffer, size_t count_bytes) {
    if (!tl_context || !tl_context->request || tl_context->request->body.empty() ||
        !buffer || count_bytes == 0) {
        return 0;
    }

    const char *body_data = tl_context->request->body.data();
    size_t body_size = tl_context->request->body.size();

    if (read_post_offset >= body_size) {
        return 0;
    }

    size_t available = body_size - read_post_offset;
    size_t to_read = std::min(count_bytes, available);

    std::memcpy(buffer, body_data + read_post_offset, to_read);
    read_post_offset += to_read;

    return to_read;
}

void wbsrv_php_register_variables(zval *track_vars_array) {
    if (!track_vars_array || !tl_context || !tl_context->request) {
        return;
    }

    HttpRequest *req = tl_context->request;
    php_import_environment_variables(track_vars_array);

    std::string doc_root = "/var/www/html";
    if (tl_context->hasMetadata("document_root")) {
        doc_root = tl_context->getMetadata("document_root").asString();
    }

    php_register_variable("DOCUMENT_ROOT", doc_root.c_str(), track_vars_array);
    php_register_variable("REQUEST_URI", (req->path + req->query).c_str(), track_vars_array);
    php_register_variable("REQUEST_METHOD", httpMethodToString(req->method).c_str(), track_vars_array);
    php_register_variable("SERVER_SOFTWARE", "WBSRV/2.0 PHP Extension", track_vars_array);
    php_register_variable("PHP_SELF", req->path.c_str(), track_vars_array);
    php_register_variable("QUERY_STRING", req->query.c_str(), track_vars_array);

    php_register_variable("CONTENT_LENGTH", std::to_string(req->body.size()).c_str(), track_vars_array);

    std::string content_type = req->getHeader("Content-Type");
    if (content_type.empty() && req->method == HttpMethod::POST) {
        content_type = "application/x-www-form-urlencoded";
    }
    if (!content_type.empty()) {
        php_register_variable("CONTENT_TYPE", content_type.c_str(), track_vars_array);
    }

    for (const auto &[header, value]: req->headers) {
        std::string var_name = header;
        for (auto &c: var_name) {
            if (c == '-') c = '_';
            c = std::toupper(c);
        }
        var_name = "HTTP_" + var_name;
        php_register_variable(var_name.c_str(), value.c_str(), track_vars_array);
    }

    if (!req->clientIP.empty()) {
        php_register_variable("REMOTE_ADDR", req->clientIP.c_str(), track_vars_array);
    }

    if (req->method == HttpMethod::POST && !req->body.empty()) {
        read_post_offset = 0;

        if (PG(enable_post_data_reading) && !SG(post_read)) {
            SG(post_read) = 0;
        }
    }
}

static int wbsrv_php_send_headers(sapi_headers_struct *sapi_headers) {
    return SAPI_HEADER_SENT_SUCCESSFULLY;
}

void wbsrv_php_sapi_error(int type, const char *error_msg, ...) {
    std::cout << "PHP Error: " << error_msg << std::endl;
}

static char *wbsrv_php_sapi_getenv(const char *name, size_t name_len) {
    if (!name) return nullptr;
    const char *env_value = getenv(name);
    return env_value ? estrdup(env_value) : nullptr;
}

static char *wbsrv_php_read_cookies() {
    if (!tl_context || !tl_context->request || !tl_context->request->hasHeader("Cookie")) {
        return nullptr;
    }

    std::string cookie_header = tl_context->request->getHeader("Cookie");
    if (cookie_header.empty()) {
        return nullptr;
    }

    return estrdup(cookie_header.c_str());
}

SAPI_API sapi_module_struct php_embed_module = {
    "PHP Extension v2", /* name */
    "PHP Extension for WBSRV v2.0", /* pretty name */

    wbsrv_php_startup, /* startup */
    php_module_shutdown_wrapper, /* shutdown */

    nullptr, /* activate */
    wbsrv_php_deactivate, /* deactivate */

    wbsrv_php_ub_write, /* unbuffered write */
    nullptr, /* flush */
    nullptr, /* get uid */
    wbsrv_php_sapi_getenv, /* getenv */

    wbsrv_php_sapi_error, /* error handler */

    wbsrv_php_header_handler, /* header handler */
    wbsrv_php_send_headers, /* send headers handler */
    nullptr, /* send header handler */

    wbsrv_php_read_post, /* read POST data */
    wbsrv_php_read_cookies, /* read Cookies */

    wbsrv_php_register_variables, /* register server variables */
    nullptr, /* Log message */
    nullptr, /* Get request time */
    nullptr, /* Child terminate */

    STANDARD_SAPI_MODULE_PROPERTIES
};

class OutputCapture {
private:
    std::stringstream buffer;
    std::stringstream *old_buffer;

public:
    OutputCapture() : old_buffer(current_output_buffer) {
        current_output_buffer = &buffer;
    }

    ~OutputCapture() {
        current_output_buffer = old_buffer;
    }

    std::string getOutput() const {
        return buffer.str();
    }

    void clear() {
        buffer.str("");
        buffer.clear();
    }
};

class PHPExtension : public IPlugin {
private:
    bool initialized_ = false;
    std::unordered_map<std::string, ConfigValue> config_;
    std::vector<std::string> php_extensions_;

public:
    bool initialize(const std::unordered_map<std::string, ConfigValue> &config) override {
        config_ = config;

        php_tsrm_startup();
        zend_signal_startup();
        sapi_startup(&php_embed_module);

        if (php_embed_module.startup(&php_embed_module) == FAILURE) {
            return false;
        }

        PG(file_uploads) = 1;
        PG(enable_post_data_reading) = 1;

        initialized_ = true;
        return true;
    }

    void shutdown() override {
        if (initialized_) {
            php_embed_module.shutdown(&php_embed_module);
            sapi_shutdown();
            tsrm_shutdown();
            initialized_ = false;
        }
    }

    std::string getName() const override {
        return "PHP Extension";
    }

    std::string getVersion() const override {
        return "2.0.0";
    }

    std::string getDescription() const override {
        return "PHP script execution engine for WBSRV";
    }

    void registerHooks(HookManager &hookManager) override {
        // Register POST_REQUEST hook with high priority to handle PHP files
        hookManager.registerHook(HookType::POST_REQUEST, getName(),
                                 [this](RequestContext &context) -> bool {
                                     return handlePostRequest(context);
                                 }, 100); // High priority to handle PHP files first

        // Register PRE_RESPONSE hook to modify headers if needed
        hookManager.registerHook(HookType::PRE_RESPONSE, getName(),
                                 [this](RequestContext &context) -> bool {
                                     return handlePreResponse(context);
                                 }, 50);

        // Register POST_RESPONSE hook for cleanup and logging
        hookManager.registerHook(HookType::POST_RESPONSE, getName(),
                                 [this](RequestContext &context) -> bool {
                                     return handlePostResponse(context);
                                 }, 50);
    }

    bool validateConfig(const std::unordered_map<std::string, ConfigValue> &config) const override {
        // Currently we do not have specific validation logic
        // TODO: add PHP configuration
        return true;
    }

private:
    bool handlePostRequest(RequestContext &context) {
        if (!isPhpFile(context.request->path)) {
            return true; // Continue processing with other handlers
        }

        // Execute PHP script and set response
        return executePhpScript(context);
    }

    static bool handlePreResponse(RequestContext &context) {
        if (!isPhpFile(context.request->path) || context.response->statusCode == 0) {
            return true; // Continue processing
        }

        context.response->headers["X-Powered-By"] = "WBSRV/2.0 PHP Extension";

        return true; // Continue processing
    }

    static bool handlePostResponse(RequestContext &context) {
        if (!isPhpFile(context.request->path)) {
            return true; // Continue processing
        }

        // Cleanup thread-local data
        tl_context = nullptr;
        read_post_offset = 0;
        tl_headers_to_send.clear();

        return true; // Continue processing
    }

    static bool isPhpFile(const std::string& path) {
        size_t len = path.length();
        return len >= 4 &&
               path[len - 4] == '.' &&
               path[len - 3] == 'p' &&
               path[len - 2] == 'h' &&
               path[len - 1] == 'p';
    }


    bool executePhpScript(RequestContext &context) {
        if (!initialized_) {
            context.response->setStatus(500, "Internal Server Error");
            context.response->body = "PHP Extension not initialized";
            context.response->setTextContent();
            return false;
        }

        // Set up thread-local context
        tl_context = &context;
        read_post_offset = 0;
        tl_headers_to_send.clear();

        ts_resource(0);
        OutputCapture capture;

        // Get document root from config or use default
        std::string doc_root = "/var/www/html";
        auto doc_root_it = config_.find("document_root");
        if (doc_root_it != config_.end() && doc_root_it->second.isString()) {
            doc_root = doc_root_it->second.asString();
        }

        // Set document root in context metadata for use in PHP variables
        context.setMetadata("document_root", ConfigValue(doc_root));

        std::string full_path = doc_root + context.request->path;

        // Initialize SAPI globals
        SG(server_context) = (void *) 1;
        SG(sapi_headers).http_response_code = 200;
        SG(request_info).request_method = estrdup(httpMethodToString(context.request->method).c_str());
        SG(request_info).request_uri = estrdup(context.request->path.c_str());
        SG(request_info).query_string = estrdup(context.request->query.c_str());
        SG(request_info).content_length = static_cast<long>(context.request->body.size());

        std::string content_type = context.request->getHeader("Content-Type");
        if (content_type.empty() && context.request->method == HttpMethod::POST && !context.request->body.empty()) {
            content_type = "application/x-www-form-urlencoded";
        }
        if (!content_type.empty()) {
            SG(request_info).content_type = estrdup(content_type.c_str());
        }

        SG(request_info).path_translated = estrdup(full_path.c_str());
        SG(request_info).proto_num = 2000;
        SG(post_read) = 0;

        // Check if file exists
        if (access(full_path.c_str(), R_OK) != 0) {
            context.response->setStatus(404, "Not Found");
            context.response->body = "File not found: " + context.request->path;
            context.response->setTextContent();
            return false;
        }

        // Start PHP request
        if (php_request_startup() == FAILURE) {
            context.response->setStatus(500, "Internal Server Error");
            context.response->body = "PHP request startup failed";
            context.response->setTextContent();
            return false;
        }

        // Reset POST data offset for this request
        if (context.request->method == HttpMethod::POST && !context.request->body.empty()) {
            read_post_offset = 0;
        }

        // Execute PHP script
        zend_file_handle file_handle;
        zend_stream_init_filename(&file_handle, full_path.c_str());

        bool execution_success = false;
        zend_try
            {
                CG(skip_shebang) = true;
                php_execute_script(&file_handle);

                // Get output and headers
                context.response->body = capture.getOutput();
                context.response->headers = tl_headers_to_send;

                // Set default content type if not set
                if (context.response->headers.find("Content-Type") == context.response->headers.end()) {
                    context.response->setHeader("Content-Type", "text/html; charset=UTF-8");
                }

                // Set status code from PHP (if changed)
                if (SG(sapi_headers).http_response_code != 200) {
                    context.response->statusCode = SG(sapi_headers).http_response_code;
                } else {
                    context.response->setStatus(200, "OK");
                }

                execution_success = true;
            }
        zend_catch {
                context.response->setStatus(500, "Internal Server Error");
                context.response->body = "PHP execution failed";

                std::string error_output = capture.getOutput();
                if (!error_output.empty()) {
                    context.response->body += "\nOutput before error: " + error_output;
                }
                context.response->setTextContent();
                execution_success = false;
            }
        zend_end_try();

        // Cleanup
        zend_destroy_file_handle(&file_handle);
        php_request_shutdown(nullptr);

        return execution_success;
    }
};

extern "C" {
IPlugin *createPlugin() {
    return new PHPExtension();
}

void destroyPlugin(IPlugin *plugin) {
    delete plugin;
}

const char *getPluginName() {
    return "PHP Extension";
}

const char *getPluginVersion() {
    return "2.0.0";
}

const char *getPluginDescription() {
    return "PHP script execution engine for WBSRV";
}

int getPluginAPIVersion() {
    return 1;
}
}
