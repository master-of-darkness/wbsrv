#include <mutex>
#include <string>
#include <sapi/embed/php_embed.h>

#include "php_sapi.h"

std::string sapi_return;
static size_t embed_ub_write(const char *str, size_t str_length){
    sapi_return = str;
    return str_length;
}


EmbedPHP::EmbedPHP() {
    int argc = 0;
    char **argv = nullptr;
    php_embed_module.ub_write = embed_ub_write;

    php_embed_init(argc, argv);  // Initialize PHP
}

EmbedPHP::~EmbedPHP() {
    php_embed_shutdown();
}

void EmbedPHP::executeScript(std::string path, std::string &retval) {
    m.lock();
    zend_file_handle file_handle;
    zend_stream_init_filename(&file_handle, path.c_str());

    if (php_execute_script(&file_handle) == FAILURE) {
        php_printf("Failed to execute PHP script.\n");
    }

    retval = std::move(sapi_return);
    m.unlock();
}