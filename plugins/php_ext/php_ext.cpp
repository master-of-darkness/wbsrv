#include "php_ext.h"
#include <php_main.h>
#include <php_variables.h>
#include <zend_exceptions.h>
#include <zend_interfaces.h>

#include <algorithm>
#include <utility>
#include <chrono>
#include <iostream>
#include <filesystem>

thread_local std::string tl_sapi_return;
thread_local std::string tl_embed_file_name;
thread_local std::string tl_web_root;
thread_local HttpRequest tl_http_request;
thread_local std::unordered_map<std::string, std::string> tl_headers_to_send;

PHPPlugin::~PHPPlugin() {
    if (initialized) {
        PHPPlugin::shutdown();
    }
}

std::string PHPPlugin::getName() const {
    return "PHP Handler Plugin";
}

std::string PHPPlugin::getVersion() const {
    return "1.0.0";
}

bool PHPPlugin::initialize() {
    if (initialized) {
        return true;
    }

    try {
        php_tsrm_startup_ex(std::thread::hardware_concurrency());

        // Configure PHP SAPI module
        php_embed_module.name = const_cast<char *>("PHP Plugin SAPI");
        php_embed_module.pretty_name = const_cast<char *>("Plugin PHP SAPI");
        php_embed_module.ub_write = php_ub_write;
        php_embed_module.register_server_variables = php_register_variables;
        php_embed_module.read_post = php_read_post;
        php_embed_module.header_handler = php_header_handler;

        // Initialize PHP embed
        if (php_embed_init(0, nullptr) == FAILURE) {
            std::cerr << "PHP Plugin: Failed to initialize PHP embed" << std::endl;
            return false;
        }

        initialized = true;
        std::cout << "PHP Plugin: Initialized successfully" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "PHP Plugin: Exception during initialization: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "PHP Plugin: Unknown exception during initialization" << std::endl;
        return false;
    }
}

void PHPPlugin::shutdown() {
    if (!initialized) {
        return;
    }

    try {
        php_embed_shutdown();
        tsrm_shutdown();
        initialized = false;
        std::cout << "PHP Plugin: Shutdown completed" << std::endl;
    } catch (...) {
        std::cerr << "PHP Plugin: Exception during shutdown" << std::endl;
    }
}

HttpResponse PHPPlugin::handleRequest(const HttpRequest &request) {
    if (!initialized) {
        HttpResponse error_response;
        error_response.statusCode = 500;
        error_response.body = "PHP Plugin not initialized";
        error_response.headers["Content-Type"] = "text/plain";
        return error_response;
    }
    HttpResponse response;

    try {
        std::string script_path = request.web_root + request.path;

        if (script_path.back() == '/' || std::filesystem::is_directory(script_path)) {
            if (script_path.back() != '/') {
                script_path += '/';
            }
            script_path += "index.php";
        }

        if (!std::filesystem::exists(script_path)) {
            response.statusCode = 404;
            response.body = "File not found: " + request.path;
            response.headers["Content-Type"] = "text/plain";
        } else {
            executeScript(script_path, request, response);
        }
    } catch (const std::exception &e) {
        response.statusCode = 500;
        response.body = "Internal server error: " + std::string(e.what());
        response.headers["Content-Type"] = "text/plain";
    } catch (...) {
        response.statusCode = 500;
        response.body = "Unknown internal server error";
        response.headers["Content-Type"] = "text/plain";
    }
    return response;
}

bool PHPPlugin::isInitialized() const {
    return initialized;
}

void PHPPlugin::executeScript(const std::string &script_path,
                              const HttpRequest &request,
                              HttpResponse &response) const {
    auto cleanup = [&]() {
        if (SG(request_info).request_method) {
            free(const_cast<char *>(SG(request_info).request_method));
            SG(request_info).request_method = nullptr;
        }
        if (SG(request_info).request_uri) {
            free(SG(request_info).request_uri);
            SG(request_info).request_uri = nullptr;
        }
        if (SG(request_info).path_translated) {
            free(SG(request_info).path_translated);
            SG(request_info).path_translated = nullptr;
        }
        if (SG(request_info).query_string) {
            free(SG(request_info).query_string);
            SG(request_info).query_string = nullptr;
        }
        if (SG(request_info).cookie_data) {
            free(SG(request_info).cookie_data);
            SG(request_info).cookie_data = nullptr;
        }

        php_request_shutdown(nullptr);

        tl_sapi_return.clear();
        tl_headers_to_send.clear();
    };

    try {
        ts_resource(0);

        tl_http_request = std::move(const_cast<HttpRequest &>(request));
        tl_web_root = web_root_path;
        tl_embed_file_name = script_path;
        tl_sapi_return.clear();
        tl_headers_to_send.clear();

        // Initialize PHP request
        PG(during_request_startup) = false;
        SG(sapi_started) = true;
        SG(post_read) = true;

        if (php_request_startup() == FAILURE) {
            response.statusCode = 500;
            response.body = "PHP request startup failed";
            response.headers["Content-Type"] = "text/plain";
            cleanup();
            return;
        }

        SG(sapi_headers).http_response_code = 200;

        if (!request.query.empty()) {
            SG(request_info).query_string = strdup(request.query.c_str());
        }

        std::string method_str = httpMethodToString(request.method);
        SG(request_info).request_method = strdup(method_str.c_str());
        SG(request_info).request_uri = strdup(request.path.c_str());
        SG(request_info).path_translated = strdup(script_path.c_str());

        zend_string *compiled_filename = zend_string_init(script_path.c_str(), script_path.length(), false);
        zend_set_compiled_filename(compiled_filename);
        zend_string_release(compiled_filename);

        // Use IHeaders interface to get Cookie header
        if (request.headers) {
            std::string cookie_header = request.headers->get("Cookie");
            if (!cookie_header.empty()) {
                SG(request_info).cookie_data = strdup(cookie_header.c_str());
            }
        }

        if (request.method == HttpMethod::POST && !request.body.empty()) {
            SG(request_info).content_length = request.body.length();
            SG(request_info).content_type = "application/x-www-form-urlencoded";

            // Use IHeaders interface to get Content-Type header
            if (request.headers) {
                std::string content_type = request.headers->get("Content-Type");
                if (!content_type.empty()) {
                    SG(request_info).content_type = strdup(content_type.c_str());
                }
            }
        }

        php_hash_environment();

        zend_file_handle file_handle;
        zend_stream_init_filename(&file_handle, script_path.c_str());

        zend_try
            {
                CG(skip_shebang) = true;
                php_execute_script(&file_handle);
            }
        zend_catch {
                zend_destroy_file_handle(&file_handle);
                response.statusCode = 500;
                response.body = "PHP execution failed";
                response.headers["Content-Type"] = "text/plain";
                cleanup();
                return;
            }
        zend_end_try();

        zend_destroy_file_handle(&file_handle);

        if (PG(last_error_type) & E_FATAL_ERRORS && PG(last_error_message)) {
            response.statusCode = 500;
            response.body = ZSTR_VAL(PG(last_error_message));
            response.headers["Content-Type"] = "text/plain";
        } else {
            response.statusCode = SG(sapi_headers).http_response_code;
            response.headers = tl_headers_to_send;
            response.body = tl_sapi_return;

            if (response.headers.find("Content-Type") == response.headers.end()) {
                response.headers["Content-Type"] = "text/html; charset=UTF-8";
            }

            response.handled = true;
        }
    } catch (const std::exception &e) {
        response.statusCode = 500;
        response.body = "Exception during PHP execution: " + std::string(e.what());
        response.headers["Content-Type"] = "text/plain";
    } catch (...) {
        response.statusCode = 500;
        response.body = "Unknown exception during PHP execution";
        response.headers["Content-Type"] = "text/plain";
    }

    cleanup();
}

std::pair<std::string, std::string> PHPPlugin::extractHeaderAndValue(const sapi_header_struct *h) {
    if (!h || !h->header || h->header_len == 0) {
        return {"", ""};
    }

    std::string header;
    header.reserve(h->header_len);

    const char *start = h->header;
    const char *end = h->header + h->header_len;
    const char *colon_pos = nullptr;

    // Find colon position
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

size_t PHPPlugin::php_ub_write(const char *str, size_t str_length) {
    if (!str || str_length == 0) {
        return 0;
    }

    tl_sapi_return.append(str, str_length);

    return str_length;
}

void PHPPlugin::php_register_variables(zval *track_vars_array) {
    if (!track_vars_array) {
        return;
    }

    // Import environment variables
    php_import_environment_variables(track_vars_array);

    const HttpRequest &req = tl_http_request;
    const std::string &file_name = tl_embed_file_name;
    const std::string &doc_root = tl_web_root;

    // Register standard CGI variables
    php_register_variable("SCRIPT_FILENAME", file_name.c_str(), track_vars_array);
    php_register_variable("DOCUMENT_ROOT", doc_root.c_str(), track_vars_array);
    php_register_variable("PHP_SELF", req.path.c_str(), track_vars_array);
    php_register_variable("REQUEST_METHOD", httpMethodToString(req.method).c_str(), track_vars_array);
    php_register_variable("QUERY_STRING", req.query.c_str(), track_vars_array);
    php_register_variable("REQUEST_URI", req.path.c_str(), track_vars_array);

    // Register server name and port (defaults)
    php_register_variable("SERVER_NAME", "localhost", track_vars_array);
    php_register_variable("SERVER_PORT", "80", track_vars_array);
    php_register_variable("SERVER_SOFTWARE", "PHP Plugin Server/1.0", track_vars_array);

    // Register HTTP headers as HTTP_* variables using IHeaders interface
    if (req.headers) {
        req.headers->forEach([&](const std::string& header, const std::string& value) {
            std::string var_name = header;
            std::ranges::replace(var_name, '-', '_');
            std::ranges::transform(var_name, var_name.begin(), ::toupper);
            var_name = "HTTP_" + var_name;
            php_register_variable(var_name.c_str(), value.c_str(), track_vars_array);
        });
    }
}

size_t PHPPlugin::php_read_post(char *buffer, size_t count_bytes) {
    if (!buffer || count_bytes <= 0) {
        return 0;
    }

    const std::string &body = tl_http_request.body;

    size_t data_length = std::min(body.size(), count_bytes);
    if (data_length > 0) {
        memcpy(buffer, body.c_str(), data_length);
    }
    return data_length;
}

int PHPPlugin::php_header_handler(sapi_header_struct *sapi_header,
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

// Utility methods
HttpMethod PHPPlugin::stringToHttpMethod(const std::string &method) {
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

std::string PHPPlugin::httpMethodToString(HttpMethod method) {
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

// Plugin factory functions (C-style for dynamic loading)
extern "C" {
IPlugin *createPlugin() {
    return new PHPPlugin();
}

void destroyPlugin(const IPlugin *plugin) {
    delete plugin;
}
}