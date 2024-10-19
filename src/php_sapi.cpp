#include "php_sapi.h"
#include "defines.h"

#include <mutex>
#include <string>
#include <algorithm>
#include <utils.h>
#include <proxygen/httpserver/ResponseBuilder.h>

using namespace proxygen;
using folly::SocketAddress;

// Thread-local storage for each thread's state.
#ifdef ZTS
thread_local std::string sapi_return;
thread_local std::string embed_file_name;
thread_local HTTPMessage thread_http_message;
thread_local std::string* thread_message_body;
thread_local ResponseHandler* downstream_;

#else
std::string sapi_return;
std::string embed_file_name;
HTTPMessage thread_http_message;
std::string* thread_message_body;

std::mutex m; // Mutex to control thread access in non-ZTS environments.
#endif

static size_t embed_ub_write(const char* str, size_t str_length)
{
    sapi_return.append(str, str_length);
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
        std::ranges::transform(v1, v1.begin(), ::toupper);
        v1 = "HTTP_" + v1;
        php_register_variable(v1.c_str(), val.c_str(), track_vars_array);
    });
}

static size_t embed_php_read_post(char* buffer, size_t count_bytes)
{
    if (!thread_message_body || count_bytes <= 0)
        return 0;

    size_t data_length = std::min(thread_message_body->size(), count_bytes);
    memcpy(buffer, thread_message_body->c_str(), data_length);
    return data_length;
}

static int embed_php_send_headers(sapi_headers_struct* sapi_headers)
{
    if (!sapi_headers || sapi_headers->headers.count == 0)
        return SAPI_HEADER_SEND_FAILED;

    try
    {
        for (int i = 0; i < sapi_headers->headers.count; ++i)
        {
            if (const auto* header = static_cast<sapi_header_struct*>(zend_llist_get_next(&sapi_headers->headers)))
            {
                std::string header_line(header->header, header->header_len);
                std::cout << header->header << " " << header_line << std::endl;
                // ResponseBuilder(downstream_).header(header->header, header_line).send();
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << e.what();
        return SAPI_HEADER_SEND_FAILED;
    }

    return SAPI_HEADER_SENT_SUCCESSFULLY;
}

static char* embed_php_read_cookies()
{
    const std::string& cookie_header = thread_http_message.getHeaders().getSingleOrEmpty("Cookie");
    if (cookie_header.empty())
        return nullptr;


    char* cookies = strdup(cookie_header.c_str());
    return cookies;
}

static void embed_php_send_header(sapi_header_struct* sapi_header, void* server_context) // todo: implement
{
    if (!sapi_header || !server_context)
        return;

    std::string header_line(sapi_header->header, sapi_header->header_len);
    std::cout << sapi_header->header << " " << header_line << std::endl;

    // ResponseBuilder(downstream_).header(sapi_header->header, header_line).send();
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
    php_embed_module.read_post = embed_php_read_post;
    php_embed_module.send_headers = embed_php_send_headers;
    php_embed_module.send_header = embed_php_send_header;

    php_embed_module.read_cookies = embed_php_read_cookies;


    php_embed_init(0, nullptr);
}

void EmbedPHP::executeScript(const std::string& path, const std::unique_ptr<HTTPMessage>& http_message,
                             std::string* message_body, ResponseHandler* downstream_)
{
#ifdef ZTS
    ts_resource(0); // Thread-safe environments will automatically manage thread-specific resources.
#else
    std::lock_guard<std::mutex> guard(m);
#endif
    thread_http_message = *http_message; // Copy the incoming HTTP message into the thread-local storage.
    embed_file_name = path; // Store the file name in thread-local storage.
    thread_message_body = message_body;

    PG(during_request_startup) = false;
    SG(sapi_started) = true;
    SG(post_read) = true;

    php_request_startup();
    SG(sapi_headers).http_response_code = 200;

    if (const char* query_string = thread_http_message.getQueryStringAsStringPiece().data(); query_string != nullptr)
    {
        SG(request_info).query_string = strdup(query_string);
    }
    SG(request_info).request_method = thread_http_message.getMethodString().c_str();
    SG(request_info).request_uri = strdup(thread_http_message.getURL().c_str());
    SG(request_info).path_translated = strdup(path.c_str());

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

    if (PG(last_error_type) & E_FATAL_ERRORS && PG(last_error_message))
    {
        ResponseBuilder(downstream_).status(STATUS_500).body(ZSTR_VAL(PG(last_error_message))).sendWithEOM();
    }
    else if (PG(last_error_type) & E_FATAL_ERRORS)
    {
        ResponseBuilder(downstream_).status(STATUS_500).body(utils::getErrorPage(500)).sendWithEOM();
    }
    else
    {
        ResponseBuilder(downstream_).status(SG(sapi_headers).http_response_code, "OK").body(std::move(sapi_return)).
                                     sendWithEOM();
    }


    if(SG(request_info).request_uri)
        free(SG(request_info).request_uri);

    if(SG(request_info).path_translated)
        free(SG(request_info).path_translated);

    if(SG(request_info).query_string)
        free(SG(request_info).query_string);

    php_request_shutdown(nullptr);
}

void EmbedPHP::Shutdown()
{
    php_embed_shutdown(); // Shut down PHP first.
#ifdef ZTS
    tsrm_shutdown(); // Then shut down TSRM (thread safety) in ZTS builds.
#endif
}
