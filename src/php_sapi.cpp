#include <string>
#include <mutex>
#include <sapi/embed/php_embed.h>

#include "php_sapi.h"

std::string sapi_return;
std::string embed_file_name;
std::mutex m;

static size_t embed_ub_write(const char *str, size_t str_length) {
    sapi_return = str;
    return str_length;
}

static void embed_php_register_variables(zval *track_vars_array) {
    php_import_environment_variables(track_vars_array);
    php_register_variable("SCRIPT_FILENAME", embed_file_name.c_str(), track_vars_array);
}


void EmbedPHP::executeScript(const std::string &path, std::string &retval) {
    m.lock();

    php_embed_module.name = "PHP SAPI Module for WBSRV";
    php_embed_module.pretty_name = "WBSRV PHP SAPI";
    php_embed_module.ub_write = embed_ub_write;
    php_embed_module.register_server_variables = embed_php_register_variables;

    embed_file_name = path;

    php_embed_init(0, nullptr); // Initialize PHP

    zend_first_try
        {
            zend_file_handle file_handle;
            zend_stream_init_filename(&file_handle, path.c_str());
            php_execute_script(&file_handle);
        }

    zend_end_try();

    retval = std::move(sapi_return);

    php_embed_shutdown();

    m.unlock();
}
