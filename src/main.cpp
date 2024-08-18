#include <cerrno>
#include <climits>
#include <netinet/in.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <sys/socket.h>
#include <sys/stat.h>
#include "h2o.h"

#include "h2o/memcached.h"
#include "defines.h"
#include "concurrent_lru_cache.h"

#define USE_HTTPS 0
#define USE_MEMCACHED 0
#define H2O_USE_LIBUV 1
static h2o_globalconf_t config;
static h2o_context_t ctx;
static h2o_multithread_receiver_t libmemcached_receiver;
static h2o_accept_ctx_t accept_ctx;

struct rowCache {
    const char* content_type;
    const char* content_text;
};

static ConcurrentLRUCache<const char*, const char*> docCache(256);
ConcurrentLRUCache<const char*, const char*>::ConstAccessor const_accessor;
static const char *default_index_files[] = {"index.html", "index.htm", "index.txt", nullptr};

static int setup_ssl(const char *cert_file, const char *key_file, const char *ciphers)
{
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    accept_ctx.ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    SSL_CTX_set_options(accept_ctx.ssl_ctx, SSL_OP_NO_SSLv2);

    if (USE_MEMCACHED) {
        accept_ctx.libmemcached_receiver = &libmemcached_receiver;
        h2o_accept_setup_memcached_ssl_resumption(h2o_memcached_create_context("127.0.0.1", 11211, 0, 1, "h2o:ssl-resumption:"),
                                                  86400);
        h2o_socket_ssl_async_resumption_setup_ctx(accept_ctx.ssl_ctx);
    }

    SSL_CTX_set_ecdh_auto(accept_ctx.ssl_ctx, 1);

    /* load certificate and private key */
    if (SSL_CTX_use_certificate_chain_file(accept_ctx.ssl_ctx, cert_file) != 1) {
        fprintf(stderr, "an error occurred while trying to load server certificate file:%s\n", cert_file);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(accept_ctx.ssl_ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "an error occurred while trying to load private key file:%s\n", key_file);
        return -1;
    }

    if (SSL_CTX_set_cipher_list(accept_ctx.ssl_ctx, ciphers) != 1) {
        fprintf(stderr, "ciphers could not be set: %s\n", ciphers);
        return -1;
    }

    h2o_ssl_register_alpn_protocols(accept_ctx.ssl_ctx, h2o_http2_alpn_protocols);
    return 0;
}


static void on_accept(uv_stream_t *listener, int status)
{
    uv_tcp_t *conn;
    h2o_socket_t *sock;

    if (status != 0)
        return;

    conn = static_cast<uv_tcp_t *>(h2o_mem_alloc(sizeof(*conn)));
    uv_tcp_init(listener->loop, conn);

    if (uv_accept(listener, reinterpret_cast<uv_stream_t *>(conn)) != 0) {
        uv_close(reinterpret_cast<uv_handle_t *>(conn), reinterpret_cast<uv_close_cb>(free));
        return;
    }

    sock = h2o_uv_socket_create(reinterpret_cast<uv_handle_t *>(conn), reinterpret_cast<uv_close_cb>(free));
    h2o_accept(&accept_ctx, sock);
}

static int create_listener()
{
    static uv_tcp_t listener;
    sockaddr_in addr{};
    int r;

    uv_tcp_init(ctx.loop, &listener);
    uv_ip4_addr("127.0.0.1", 7890, &addr);
    if ((r = uv_tcp_bind(&listener, reinterpret_cast<struct sockaddr *>(&addr), 0)) != 0) {
        fprintf(stderr, "uv_tcp_bind:%s\n", uv_strerror(r));
        goto Error;
    }
    if ((r = uv_listen(reinterpret_cast<uv_stream_t *>(&listener), 128, on_accept)) != 0) {
        fprintf(stderr, "uv_listen:%s\n", uv_strerror(r));
        goto Error;
    }

    return 0;
Error:
    uv_close(reinterpret_cast<uv_handle_t *>(&listener), nullptr);
    return r;
}


int main()
{
    h2o_hostconf_t *hostconf;
    h2o_access_log_filehandle_t *logfh = h2o_access_log_open_handle("/dev/stdout", nullptr, H2O_LOGCONF_ESCAPE_APACHE);
    h2o_pathconf_t *pathconf;

    signal(SIGPIPE, SIG_IGN);

    // TODO: make vhost loading option
    h2o_config_init(&config);
    hostconf = h2o_config_register_host(&config, h2o_iovec_init(H2O_STRLIT("default")), 65535);
    pathconf = h2o_config_register_path(hostconf, "/", 0);
    h2o_file_register(pathconf, "examples/doc_root", nullptr, nullptr, 0);
    if (logfh != nullptr)
        h2o_access_log_register(pathconf, logfh);

    uv_loop_t loop;
    uv_loop_init(&loop);
    h2o_context_init(&ctx, &loop, &config);

    if (USE_HTTPS && setup_ssl("myCA.pem", "myCA.key",
                               "DEFAULT:!MD5:!DSS:!DES:!RC4:!RC2:!SEED:!IDEA:!NULL:!ADH:!EXP:!SRP:!PSK") != 0)
        return 1;

    accept_ctx.ctx = &ctx;
    accept_ctx.hosts = config.hosts;

    if (create_listener() != 0) {
        fprintf(stderr, "failed to listen to 127.0.0.1:7890:%s\n", strerror(errno));
        return 1;
    }

    uv_run(ctx.loop, UV_RUN_DEFAULT);

    return 1;
}