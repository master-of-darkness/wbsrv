#include "server/module.h"
#include <glog/logging.h>
#include <folly/logging/xlog.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <memory>
#include <unordered_map>

#include <php.h>
#include <main/SAPI.h>
#include <main/php_main.h>
#include <main/php_variables.h>
#include <zend_ini.h>

#include "utils/defines.h"
#include "utils/utils.h"

using namespace ModuleManage;

thread_local proxygen::HTTPHeaders tl_headers_response;
thread_local ModuleContext *tl_context = nullptr;
thread_local size_t read_post_offset = 0;

static int wbsrv_php_startup(sapi_module_struct *sapi_module) {
    return php_module_startup(sapi_module, nullptr);
}

static int wbsrv_php_deactivate(void) {
    return SUCCESS;
}

static size_t wbsrv_php_ub_write(const char *str, size_t str_length) {
    tl_context->response->body(str);
    return str_length;
}

static int wbsrv_php_header_handler(sapi_header_struct *sapi_header,
                                    sapi_header_op_enum op,
                                    sapi_headers_struct *sapi_headers) {
    if (!sapi_header || !sapi_header->header) {
        return FAILURE;
    }

    const char *start = sapi_header->header;
    const char *end = start + sapi_header->header_len;
    const char *colon = static_cast<const char *>(memchr(start, ':', sapi_header->header_len));
    if (!colon) {
        return FAILURE;
    }

    std::string_view header(start, colon - start);
    std::string_view value(colon + 1, end - (colon + 1));

    switch (op) {
        case SAPI_HEADER_DELETE:
            tl_headers_response.rawRemove(header.data());
            break;
        case SAPI_HEADER_ADD:
        case SAPI_HEADER_REPLACE:
            // Remove existing header with the same name
            tl_headers_response.removeByPredicate(
                [header](proxygen::HTTPHeaderCode, const std::string &name, const std::string &) {
                    if (name == header.data())
                        return false;
                    return true;
                });

            tl_headers_response.rawSet(header.data(), value.data());
            break;
        case SAPI_HEADER_DELETE_ALL:
            tl_headers_response.removeAll();
            break;
        default:
            return FAILURE;
    }

    return SUCCESS;
}

static size_t wbsrv_php_read_post(char *buffer, size_t count_bytes) {
    if (!tl_context || !buffer || count_bytes == 0) {
        return 0;
    }

    folly::fbstring body = tl_context->getRequestBody();

    if (body.empty() || read_post_offset >= body.size()) {
        return 0;
    }

    size_t available = body.size() - read_post_offset;
    size_t to_read = std::min(count_bytes, available);

    std::memcpy(buffer, body.data() + read_post_offset, to_read);
    read_post_offset += to_read;

    return to_read;
}

void wbsrv_php_register_variables(zval *track_vars_array) {
    if (!track_vars_array || !tl_context || !tl_context->request) {
        return;
    }

    php_import_environment_variables(track_vars_array);

    std::string uri = tl_context->request->getURL();
    std::string method = tl_context->request->getMethodString();

    php_register_variable("DOCUMENT_ROOT", tl_context->document_root.c_str(), track_vars_array);
    php_register_variable("REQUEST_URI", uri.c_str(), track_vars_array);
    php_register_variable("REQUEST_METHOD", method.c_str(), track_vars_array);
    php_register_variable("SERVER_SOFTWARE", "WBSRV", track_vars_array);
    php_register_variable("PHP_SELF", uri.c_str(), track_vars_array);

    size_t body_size = tl_context->getRequestBodySize();
    php_register_variable("CONTENT_LENGTH", std::to_string(body_size).c_str(), track_vars_array);

    // Handle headers
    tl_context->request->getHeaders().forEachWithCode(
        [&](proxygen::HTTPHeaderCode code, const std::string &name, const std::string &value) {
            if (code == proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE) {
                php_register_variable("CONTENT_TYPE", value.c_str(), track_vars_array);
            }

            std::string var_name = name;
            for (auto &c: var_name) {
                if (c == '-') c = '_';
                c = std::toupper(c);
            }
            var_name = "HTTP_" + var_name;
            php_register_variable(var_name.c_str(), value.c_str(), track_vars_array);
        });

    // Handle custom headers
    tl_context->request->getHeaders().forEach([&](const std::string &header, const std::string &value) {
        std::string var_name = header;
        for (auto &c: var_name) {
            if (c == '-') c = '_';
            c = std::toupper(c);
        }
        var_name = "HTTP_" + var_name;
        php_register_variable(var_name.c_str(), value.c_str(), track_vars_array);
    });
}

static int wbsrv_php_send_headers(sapi_headers_struct *sapi_headers) {
    return SAPI_HEADER_SENT_SUCCESSFULLY;
}

void wbsrv_php_sapi_error(int type, const char *error_msg, ...) {
    XLOG(ERR) << "PHP Error: " << error_msg;
}

static char *wbsrv_php_sapi_getenv(const char *name, size_t name_len) {
    if (!name) return nullptr;
    const char *env_value = getenv(name);
    return env_value ? estrdup(env_value) : nullptr;
}

static char *wbsrv_php_read_cookies() {
    std::string cookie_header;
    if (tl_context->request->getHeaders().exists("Cookie")) {
        cookie_header = tl_context->request->getHeaders().getSingleOrEmpty("Cookie");
    }

    if (cookie_header.empty()) {
        return nullptr;
    }

    return estrdup(cookie_header.c_str());
}

// PHP SAPI module definition
SAPI_API sapi_module_struct php_embed_module = {
    "PHP Module", /* name */
    "PHP Module for WBSRV", /* pretty name */

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

inline bool isPhpFile(const folly::fbstring &path) {
    size_t len = path.length();
    return len >= 4 &&
           path[len - 4] == '.' &&
           path[len - 3] == 'p' &&
           path[len - 2] == 'h' &&
           path[len - 1] == 'p';
}

static bool PHPModule_init() {
    php_tsrm_startup_ex(3);
    zend_signal_startup();
    sapi_startup(&php_embed_module);

    if (php_embed_module.startup(&php_embed_module) == FAILURE) {
        return false;
    }

    PG(file_uploads) = 1;
    PG(enable_post_data_reading) = 1;

    return true;
}

static void PHPModule_cleanup() {
    php_embed_module.shutdown(&php_embed_module);
    sapi_shutdown();
    tsrm_shutdown();
}

static ModuleResult PHPModule_pre_response(ModuleContext &ctx) {
    if (!isPhpFile(ctx.file_path))
        return ModuleResult::CONTINUE;


    tl_context = &ctx;
    read_post_offset = 0;
    ts_resource(0);

    SG(server_context) = (void *) 1;
    SG(sapi_headers).http_response_code = 200;
    SG(request_info).request_method = estrdup(ctx.request->getMethodString().c_str());
    SG(request_info).request_uri = estrdup(ctx.request->getURL().c_str());
    SG(request_info).query_string = estrdup(ctx.request->getQueryString().c_str());

    size_t body_size = ctx.getRequestBodySize();
    SG(request_info).content_length = static_cast<long>(body_size);

    std::string content_type;
    if (ctx.request->getHeaders().exists(proxygen::HTTP_HEADER_CONTENT_TYPE)) {
        content_type = ctx.request->getHeaders().getSingleOrEmpty(proxygen::HTTP_HEADER_CONTENT_TYPE);
    }
    if (content_type.empty() && ctx.request->getMethodString() == "POST" && body_size > 0) {
        content_type = "application/x-www-form-urlencoded";
    }
    if (!content_type.empty()) {
        SG(request_info).content_type = estrdup(content_type.c_str());
    }

    SG(request_info).path_translated = estrdup(ctx.file_path.c_str());
    SG(request_info).proto_num = 2000;
    SG(post_read) = 0;

    if (php_request_startup() == FAILURE)
        return ModuleResult::CONTINUE;


    // Execute PHP script
    zend_file_handle file_handle;
    zend_stream_init_filename(&file_handle, ctx.file_path.c_str());

    bool execution_success = false;
    zend_try
        {
            CG(skip_shebang) = true;
            php_execute_script(&file_handle);

            // Get status code from PHP
            int status_code = SG(sapi_headers).http_response_code;
            if (status_code == 0) {
                status_code = 200;
            }

            ctx.response->status(status_code, proxygen::HTTPMessage::getDefaultReason(status_code));

            tl_headers_response.forEach([&](const std::string &name, const std::string &value) {
                ctx.response->header(name, value);
            });

            ctx.response->header(proxygen::HTTP_HEADER_CONTENT_TYPE, "text/html; charset=UTF-8");

            ctx.response->sendWithEOM();

            execution_success = true;
        }
    zend_catch {
            ctx.response->status(STATUS_500)
                    .header(proxygen::HTTP_HEADER_CONTENT_TYPE, "text/html; charset=UTF-8")
                    .body(Utils::getErrorPage(500))
                    .sendWithEOM();
            execution_success = false;
        }
    zend_end_try();

    // Cleanup
    zend_destroy_file_handle(&file_handle);
    php_request_shutdown(nullptr);


    return (execution_success ? ModuleResult::BREAK : ModuleResult::CONTINUE);
}

static ModuleResult PHPModule_post_response(ModuleContext &ctx) {
    tl_context = nullptr;
    read_post_offset = 0;

    return ModuleResult::CONTINUE;
}

static Module PHPModule = {
    "PHPModule",
    "2.0.0",
    0, //
    true, // enabled
    nullptr,
    PHPModule_pre_response,
    PHPModule_post_response,
    PHPModule_init,
    PHPModule_cleanup
};

REGISTER_MODULE(PHPModule);
