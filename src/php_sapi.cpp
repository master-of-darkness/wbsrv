#include "php_sapi.h"

using namespace proxygen;

using folly::SocketAddress;


std::string sapi_return;
std::string embed_file_name;
std::mutex m;
HTTPMessage g_http_message;

static size_t embed_ub_write(const char *str, size_t str_length) {
    sapi_return += str;
    return str_length;
}

static void embed_php_register_variables(zval *track_vars_array) {
    php_import_environment_variables(track_vars_array);
    php_register_variable("SCRIPT_FILENAME", embed_file_name.c_str(), track_vars_array);

    g_http_message.getHeaders().forEach([&](const std::string &header, const std::string &val) {
        std::string v1 = header;
        std::ranges::replace(v1, '-', '_');
        std::ranges::transform(v1, v1.begin(), toupper);
        v1 = "HTTP_" + v1;
        php_register_variable(v1.c_str(), val.c_str(), track_vars_array);
    });
}


void EmbedPHP::executeScript(const std::string &path, std::string &retval,
                             const std::unique_ptr<HTTPMessage> &http_message) {
    m.lock();
    g_http_message = *http_message;

    php_embed_module.name = "PHP SAPI Module for WBSRV";
    php_embed_module.pretty_name = "WBSRV PHP SAPI";
    php_embed_module.ub_write = embed_ub_write;
    php_embed_module.register_server_variables = embed_php_register_variables;

    embed_file_name = path;

    php_embed_init(0, nullptr);

    // Set the query string for the request
    if (auto query_string = g_http_message.getQueryStringAsStringPiece().data(); query_string != nullptr)
        SG(request_info).query_string = estrdup(g_http_message.getQueryStringAsStringPiece().data());


    // Set request method and URI (optional, but helps simulate full request context)
    SG(request_info).request_method = estrdup(g_http_message.getMethodString().c_str());
    SG(request_info).request_uri = estrdup(g_http_message.getURL().c_str());
    SG(request_info).path_translated = estrdup(path.c_str());
    php_hash_environment();

    zend_file_handle file_handle;

    zend_first_try
        {
            zend_stream_init_filename(&file_handle, path.c_str());
            CG(skip_shebang) = true;
            php_execute_script(&file_handle);
        } zend_catch {
        }
    zend_end_try();
    zend_destroy_file_handle(&file_handle);

    retval = std::move(sapi_return);
    php_embed_shutdown();
    m.unlock();
}
