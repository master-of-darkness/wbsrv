#include <algorithm>

#include "interface.h"
#include <main/php.h>
#include <main/SAPI.h>
#include <main/php_main.h>
#include <main/php_variables.h>
#include <zend_ini.h>
#include <iostream>
#include <sstream>

thread_local std::stringstream *current_output_buffer = nullptr;
thread_local std::unordered_map<std::string, std::string> tl_headers_to_send;
thread_local HttpRequest *tl_req;

HttpMethod stringToHttpMethod(const std::string &method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "PATCH") return HttpMethod::PATCH;
    if (method == "TRACE") return HttpMethod::TRACE;
    if (method == "CONNECT") return HttpMethod::CONNECT;
    if (method == "CONNECT_UDP") return HttpMethod::CONNECT_UDP;
    if (method == "SUB") return HttpMethod::SUB;
    if (method == "PUB") return HttpMethod::PUB;
    if (method == "UNSUB") return HttpMethod::UNSUB;
    return HttpMethod::GET; // Default fallback
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
        case HttpMethod::CONNECT_UDP: return "CONNECT-UDP";
        case HttpMethod::SUB: return "SUB";
        case HttpMethod::PUB: return "PUB";
        case HttpMethod::UNSUB: return "UNSUB";
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
    while (value_end > value_start && (*(value_end - 1) == ' ' || *(value_end - 1) == '\t' || *(value_end - 1) == '\r'
                                       || *(value_end - 1) == '\n')) {
        --value_end;
    }

    std::string value(value_start, value_end);

    return {header, value};
}

static int php_embed_startup(sapi_module_struct *sapi_module) {
    return php_module_startup(sapi_module, nullptr);
}

static int php_embed_deactivate(void) {
    return SUCCESS;
}

static size_t php_embed_ub_write(const char *str, size_t str_length) {
    current_output_buffer->write(str, str_length);
    return str_length;
}

static int php_embed_header_handler(sapi_header_struct *sapi_header,
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

static size_t read_post_offset = 0; // Global state to track position

static size_t php_embed_read_post(char *buffer, size_t count_bytes) {
    if (!tl_req || !tl_req->body || tl_req->body->empty() || !buffer || count_bytes == 0)
        return 0;

    const char *body_data = tl_req->body->data();
    size_t body_size = tl_req->body->size();

    // Check if we've already read everything
    if (read_post_offset >= body_size)
        return 0;

    // Calculate how much we can read
    size_t available = body_size - read_post_offset;
    size_t to_read = std::min(count_bytes, available);

    // Copy the data
    memcpy(buffer, body_data + read_post_offset, to_read);

    // Update global position
    read_post_offset += to_read;

    return to_read;
}

void php_register_variables(zval *track_vars_array) {
    if (!track_vars_array) {
        return;
    }
    php_import_environment_variables(track_vars_array);

    // php_register_variable("DOCUMENT_ROOT", tl_req->web_root.c_str(), track_vars_array);
    // php_register_variable("REQUEST_URI", tl_req->path.c_str(), track_vars_array);
    // php_register_variable("REQUEST_METHOD", httpMethodToString(tl_req->method).c_str(), track_vars_array);
    //
    // php_register_variable("SERVER_NAME", "localhost", track_vars_array);
    // php_register_variable("SERVER_PORT", "80", track_vars_array);
    // php_register_variable("SERVER_SOFTWARE", "PHP Plugin Server/1.0", track_vars_array);

    if (tl_req->body && !tl_req->body->empty()) {
        php_register_variable("CONTENT_LENGTH", std::to_string(tl_req->body->size()).c_str(), track_vars_array);
    }

    std::string content_type = tl_req->getHeader("Content-Type");
    if (!content_type.empty()) {
        php_register_variable("CONTENT_TYPE", content_type.c_str(), track_vars_array);
    }

    if (tl_req->headers) {
        tl_req->headers->forEach([&](const std::string &header, const std::string &value) {
            std::string var_name = header;
            std::ranges::replace(var_name, '-', '_');
            std::ranges::transform(var_name, var_name.begin(), ::toupper);
            var_name = "HTTP_" + var_name;
            php_register_variable(var_name.c_str(), value.c_str(), track_vars_array);
        });
    }
}

static int php_embed_send_headers(sapi_headers_struct *sapi_headers) {
    return SAPI_HEADER_SENT_SUCCESSFULLY;
}

void php_embed_sapi_error(int type, const char *error_msg, ...) {
    std::cout << "PHP Error: " << error_msg << "\n";
}

static char *php_embed_sapi_getenv(const char *name, size_t name_len) {
    char buffer[256];
    if (name_len >= sizeof(buffer)) {
        // name too long
        return NULL;
    }
    memcpy(buffer, name, name_len);
    buffer[name_len] = '\0';

    // Print the name for debugging
    printf("Requested env var: %s\n", buffer);

    // Get environment variable value
    char *value = getenv(buffer);
    if (value) {
        // Return a duplicated string because getenv memory shouldn't be modified or freed
        return strdup(value);
    } else {
        return NULL;
    }
}

static char* php_embed_read_cookies() {
    if (!tl_req || !tl_req->headers->exists("Cookie")) {
        return "";
    }

    std::string cookie_header = tl_req->getHeader("Cookie");

    if (cookie_header.empty()) {
        return "";
    }

    return estrdup(cookie_header.c_str());
}

SAPI_API sapi_module_struct php_embed_module = {
    "PHP Extension", /* name */
    "PHP Extension for WBSRV", /* pretty name */

    php_embed_startup, /* startup */
    php_module_shutdown_wrapper, /* shutdown */

    nullptr, /* activate */
    php_embed_deactivate, /* deactivate */

    php_embed_ub_write, /* unbuffered write */
    nullptr, /* flush */
    nullptr, /* get uid */
    php_embed_sapi_getenv, /* getenv */

    php_embed_sapi_error, /* error handler */

    php_embed_header_handler, /* header handler */
    php_embed_send_headers, /* send headers handler */
    nullptr, /* send header handler */

    php_embed_read_post, /* read POST data */
    php_embed_read_cookies, /* read Cookies */

    php_register_variables, /* register server variables */
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
public:
    [[nodiscard]] std::string getName() const override {
        return "PHP Extension";
    }

    [[nodiscard]] std::string getVersion() const override {
        return "1.0.0";
    }

    bool initialize() override {
        php_tsrm_startup();
        zend_signal_startup();
        sapi_startup(&php_embed_module);

        if (php_embed_module.startup(&php_embed_module) == FAILURE) {
            return false;
        }

        SG(options) |= SAPI_OPTION_NO_CHDIR;
        PG(file_uploads) = 1;
        PG(enable_post_data_reading) = 1;

        std::cout << "[PHP Extension] Initialized.\n";
        return true;
    }

    void shutdown() override {
        std::cout << "[PHP Extension] Shutdown.\n";
    }

    HttpResponse handleRequest(HttpRequest *request) override {
        ts_resource(0);
        HttpResponse response;
        response.statusCode = 200;
        response.headers["Content-Type"] = "text/html";
        response.handled = true;

        tl_req = request;

        OutputCapture capture;
        std::string full_path = request->web_root + request->path;

        try {
            SG(server_context) = (void *)1;

            SG(sapi_headers).http_response_code = 200;
            SG(request_info).request_method = methodToString(request->method).c_str();
            SG(request_info).request_uri = strdup(request->path.c_str());
            SG(request_info).query_string = strdup(request->query.c_str());
            SG(request_info).content_length = request->body->size();
            SG(request_info).content_type = request->headers->get("Content-Type").empty() ? "" : estrdup(request->headers->get("Content-Type").c_str());
            SG(request_info).path_translated = estrdup(full_path.c_str());
            SG(request_info).proto_num = 2000;

            if (php_request_startup() == FAILURE) {
                std::cout << "php_request_startup() failed\n";
                response.statusCode = 500;
                response.body = "PHP request startup failed";
                response.headers["Content-Type"] = "text/plain";
                return response;
            }

            zend_file_handle file_handle;
            zend_stream_init_filename(&file_handle, full_path.c_str());

            zend_try
                {
                    CG(skip_shebang) = true;
                    php_execute_script(&file_handle);
                    response.body = capture.getOutput();
                }
            zend_catch {
                    zend_destroy_file_handle(&file_handle);
                    response.statusCode = 500;
                    response.body = "PHP execution failed";

                    // Include any output that was generated before the error
                    std::string error_output = capture.getOutput();
                    if (!error_output.empty()) {
                        response.body += "\nOutput before error: " + error_output;
                    }

                    response.headers["Content-Type"] = "text/plain";
                    php_request_shutdown(nullptr);
                    return response;
                }
            zend_end_try();

            zend_destroy_file_handle(&file_handle);
            php_request_shutdown(nullptr);
        } catch (const std::exception &e) {
            response.statusCode = 500;
            response.body = "Exception: " + std::string(e.what());

            std::string error_output = capture.getOutput();
            if (!error_output.empty()) {
                response.body += "\nOutput before exception: " + error_output;
            }

            response.headers["Content-Type"] = "text/plain";
        }
        return response;
    }

private:
    [[nodiscard]] static std::string methodToString(HttpMethod method) {
        switch (method) {
            case HttpMethod::GET: return "GET";
            case HttpMethod::POST: return "POST";
            case HttpMethod::OPTIONS: return "OPTIONS";
            case HttpMethod::DELETE: return "DELETE";
            case HttpMethod::HEAD: return "HEAD";
            case HttpMethod::CONNECT: return "CONNECT";
            case HttpMethod::CONNECT_UDP: return "CONNECT_UDP";
            case HttpMethod::PUT: return "PUT";
            case HttpMethod::TRACE: return "TRACE";
            case HttpMethod::PATCH: return "PATCH";
            case HttpMethod::SUB: return "SUB";
            case HttpMethod::PUB: return "PUB";
            case HttpMethod::UNSUB: return "UNSUB";
            default: return "UNKNOWN";
        }
    }
};

// Plugin factory functions
extern "C" IPlugin *createPlugin() {
    return new PHPExtension();
}

extern "C" void destroyPlugin(IPlugin *plugin) {
    delete plugin;
}
