#include "php_sapi.h"
#include "defines.h"

#include <mutex>
#include <string>
#include <algorithm>
#include <utils.h>
#include <proxygen/httpserver/ResponseBuilder.h>

using namespace proxygen;
using folly::SocketAddress;

#ifdef ZTS
thread_local std::string sapi_return;
thread_local std::string embed_file_name;
thread_local std::string thread_web_root;
thread_local HTTPMessage thread_http_message;
thread_local std::string* thread_message_body = nullptr;
thread_local ResponseHandler* downstream_ = nullptr;
thread_local ResponseBuilder* builder = nullptr;
thread_local std::unordered_map<std::string, std::string> headers_to_send;
#else
std::string sapi_return;
std::string embed_file_name;
HTTPMessage thread_http_message;
std::string* thread_message_body = nullptr;
ResponseHandler* downstream_ = nullptr;
std::mutex m;
#endif

static std::pair<std::string, std::string> extractHeaderAndValue(const sapi_header_struct* h)
{
    // Reserve memory upfront to avoid reallocations
    std::string he;
    he.reserve(h->header_len);

    const char* start = h->header;
    const char* end = h->header + h->header_len;
    const char* colonPos = nullptr;

    // Find the colon position
    for (const char* it = start; it < end; ++it)
    {
        if (*it == ':')
        {
            colonPos = it;
            break;
        }
        he += *it;
    }

    // If no colon found, return empty strings
    if (!colonPos)
    {
        return {"", ""};
    }

    // Create val from the substring after the colon
    std::string val(colonPos + 1, end);

    return {he, val};
}


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
    php_register_variable("REQUEST_METHOD", thread_http_message.getMethodString().c_str(), track_vars_array);
    php_register_variable("DOCUMENT_ROOT", thread_web_root.c_str(), track_vars_array);
    php_register_variable("QUERY_STRING",
                          thread_http_message.getQueryStringAsStringPiece().data() == nullptr
                              ? ""
                              : thread_http_message.getQueryStringAsStringPiece().data(), track_vars_array);
    thread_http_message.getHeaders().forEach([track_vars_array](const std::string& header, const std::string& val)
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
    {
        return 0;
    }

    size_t data_length = std::min(thread_message_body->size(), count_bytes);
    memcpy(buffer, thread_message_body->c_str(), data_length);
    return data_length;
}

int embed_php_header_handler(sapi_header_struct* sapi_header, sapi_header_op_enum op, sapi_headers_struct* sapi_headers)
{
    auto p = extractHeaderAndValue(sapi_header);
    switch (op)
    {
    case SAPI_HEADER_DELETE:
        headers_to_send.erase(p.first);
        break;
    case 1:
        headers_to_send[p.first] = p.second;
        break;
    case SAPI_HEADER_DELETE_ALL:
        headers_to_send.clear();
        break;
    case SAPI_HEADER_REPLACE:
        headers_to_send[p.first] = p.second;
        break;
    default:
        return FAILURE;
    }

    return 0; // Success
}

void EmbedPHP::Initialize(int threads_expected)
{
#ifdef ZTS
    php_tsrm_startup_ex(threads_expected);
#endif

    php_embed_module.name = strdup("PHP SAPI Module for WBSRV");
    php_embed_module.pretty_name = strdup("WBSRV PHP SAPI");
    php_embed_module.ub_write = embed_ub_write;
    php_embed_module.register_server_variables = embed_php_register_variables;
    php_embed_module.read_post = embed_php_read_post;
    php_embed_module.header_handler = embed_php_header_handler;

    php_embed_init(0, nullptr);
}

void EmbedPHP::executeScript(const std::string& path, const std::unique_ptr<HTTPMessage>& http_message,
                             std::string* message_body, ResponseHandler* downstream,
                             utils::ConcurrentLRUCache<std::string, std::shared_ptr<CacheRow>>* cache,
                             std::string web_root)
{
#ifdef ZTS
    (void)ts_resource(0);
#else
    std::lock_guard<std::mutex> guard(m);
#endif


    // TODO: 1. Implement a proper caching of request based on requested url
    // TODO: 2. Cache only GET responses and not POST
    // TODO: 3. Fix memory leak of PHP execution
    thread_http_message = *http_message;
    thread_web_root = web_root;
    embed_file_name = path;
    thread_message_body = message_body;
    downstream_ = downstream;
    builder = new ResponseBuilder(downstream);

    PG(during_request_startup) = false;
    SG(sapi_started) = true;
    SG(post_read) = true;

    php_request_startup();
    SG(sapi_headers).http_response_code = 200;

    if (const char* query_string = thread_http_message.getQueryStringAsStringPiece().data(); query_string != nullptr)
    {
        SG(request_info).query_string = strdup(query_string);
    }
    SG(request_info).request_method = strdup(thread_http_message.getMethodString().c_str());

    SG(request_info).request_uri = strdup(thread_http_message.getURL().c_str());
    SG(request_info).path_translated = strdup(path.c_str());
    const std::string& cookie = thread_http_message.getHeaders().getSingleOrEmpty("Cookie");
    SG(request_info).cookie_data = cookie.empty() ? nullptr : strdup(cookie.c_str());
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
        builder->status(STATUS_500).body(ZSTR_VAL(PG(last_error_message))).sendWithEOM();
    }
    else
    {
        CacheAccessor cache_acc;
        if (!cache->find(cache_acc, path))
        {
            builder->status(SG(sapi_headers).http_response_code, "Ok");
            for (const auto& [fst, snd] : headers_to_send)
            {
                if (!fst.empty())
                    builder->header(fst, snd);
            }
            builder->body(sapi_return).sendWithEOM();
            CacheRow cache_row;
            cache_row.content_type = headers_to_send["Content-Type"];
            cache_row.headers = headers_to_send;
            cache_row.text = sapi_return;
            cache->insert(path, std::make_shared<CacheRow>(cache_row));
        }
    }
    if (SG(request_info).request_method)
    {
        free((void*)SG(request_info).request_method);
        SG(request_info).request_method = nullptr;
    }
    if (SG(request_info).request_uri)
    {
        free(SG(request_info).request_uri);
        SG(request_info).request_uri = nullptr;
    }
    if (SG(request_info).path_translated)
    {
        free(SG(request_info).path_translated);
        SG(request_info).path_translated = nullptr;
    }
    if (SG(request_info).query_string)
    {
        free(SG(request_info).query_string);
        SG(request_info).query_string = nullptr;
    }
    if (SG(request_info).cookie_data)
    {
        free(SG(request_info).cookie_data);
        SG(request_info).cookie_data = nullptr;
    }

    php_request_shutdown(nullptr);
    thread_message_body = nullptr;
    downstream_ = nullptr;
    sapi_return.clear();
    delete builder;
    builder = nullptr;
    headers_to_send.clear();
}

void EmbedPHP::Shutdown()
{
    php_embed_shutdown();
#ifdef ZTS
    tsrm_shutdown();
#endif
}
