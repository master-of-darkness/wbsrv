#include "php_sapi.h"
#include <thread>

using namespace proxygen;
using folly::SocketAddress;

// Thread-local storage for each thread's state.
#ifdef ZTS
    thread_local std::string sapi_return;
    thread_local std::string embed_file_name;
    thread_local HTTPMessage thread_http_message;
#else
std::string sapi_return;
std::string embed_file_name;
HTTPMessage thread_http_message;
std::mutex m; // Mutex to control thread access in non-ZTS environments.
#endif

static size_t embed_ub_write(const char* str, size_t str_length)
{
    sapi_return += str;
    return str_length;
}

static void embed_php_register_variables(zval* track_vars_array)
{
    php_import_environment_variables(track_vars_array);
    php_register_variable("SCRIPT_FILENAME", embed_file_name.c_str(), track_vars_array);
    php_register_variable("PHP_SELF", thread_http_message.getPath().c_str(), track_vars_array);

    thread_http_message.getHeaders().forEach([&](const std::string& header, const std::string& val)
    {
        std::string v1 = header;
        std::ranges::replace(v1, '-', '_');
        std::ranges::transform(v1, v1.begin(), toupper);
        v1 = "HTTP_" + v1;
        php_register_variable(v1.c_str(), val.c_str(), track_vars_array);
    });
}

void EmbedPHP::Initialize(int threads_expected)
{
#ifdef ZTS
    php_tsrm_startup_ex(threads_expected);
    zend_signal_startup();
#endif

    php_embed_module.name = "PHP SAPI Module for WBSRV";
    php_embed_module.pretty_name = "WBSRV PHP SAPI";
    php_embed_module.ub_write = embed_ub_write;
    php_embed_module.register_server_variables = embed_php_register_variables;
    php_embed_init(0, nullptr);
}

void EmbedPHP::executeScript(const std::string& path, std::string& retval,
                             const std::unique_ptr<HTTPMessage>& http_message)
{
#ifdef ZTS
    ts_resource(0);  // Thread-safe environments will automatically manage thread-specific resources.
#else
    m.lock();
#endif

    // Copy the incoming HTTP message into the thread-local storage.
    thread_http_message = *http_message;
    embed_file_name = path; // Store the file name in thread-local storage.

    PG(during_request_startup) = false;
    SG(sapi_started) = true;

    php_request_startup();

    // Set the query string for the request.
    if (auto query_string = thread_http_message.getQueryStringAsStringPiece().data(); query_string != nullptr)
    {
        SG(request_info).query_string = estrdup(query_string);
    }

    // Set request method and URI (optional, but helps simulate full request context).
    SG(request_info).request_method = estrdup(thread_http_message.getMethodString().c_str());
    SG(request_info).request_uri = estrdup(thread_http_message.getURL().c_str());
    SG(request_info).path_translated = estrdup(path.c_str());

    php_hash_environment();

    zend_file_handle file_handle;

    zend_try
        {
            zend_stream_init_filename(&file_handle, path.c_str());
            CG(skip_shebang) = true;
            php_execute_script(&file_handle);
        }
    zend_end_try();

    zend_destroy_file_handle(&file_handle);

    // Return error message as php response if there's a fatal error, otherwise return the script output.
    if (PG(last_error_type) & E_FATAL_ERRORS)
    {
        retval = ZSTR_VAL(PG(last_error_message));
    }
    else
    {
        retval = std::move(sapi_return);
    }

    // Shutdown the PHP request and cleanup.
    php_request_shutdown(nullptr);

#ifndef ZTS
    m.unlock();
#endif
}

void EmbedPHP::Shutdown()
{
    php_embed_shutdown(); // Shut down PHP first.
#ifdef ZTS
    tsrm_shutdown();  // Then shut down TSRM (thread safety) in ZTS builds.
#endif
}
