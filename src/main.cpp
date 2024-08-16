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
#include "yaml-cpp/yaml.h"
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

// //from h2o repository
// static int on_req(h2o_handler_t *_self, h2o_req_t *req)
// {
//     h2o_file_handler_t *self = reinterpret_cast<h2o_file_handler_t *>(_self);
//     char *rpath;
//     size_t rpath_len, req_path_prefix;
//     struct st_h2o_sendfile_generator_t *generator = NULL;
//     int is_dir;
//
//     if (req->path_normalized.len < self->conf_path.len) {
//         h2o_iovec_t dest = h2o_uri_escape(&req->pool, self->conf_path.base, self->conf_path.len, "/");
//         if (req->query_at != SIZE_MAX)
//             dest = h2o_concat(&req->pool, dest, h2o_iovec_init(req->path.base + req->query_at, req->path.len - req->query_at));
//         h2o_send_redirect(req, 301, "Moved Permanently", dest.base, dest.len);
//         return 0;
//     }
//
//     /* build path (still unterminated at the end of the block) */
//     req_path_prefix = self->conf_path.len;
//     rpath = alloca(self->real_path.len + (req->path_normalized.len - req_path_prefix) + self->max_index_file_len + 1);
//     rpath_len = 0;
//     memcpy(rpath + rpath_len, self->real_path.base, self->real_path.len);
//     rpath_len += self->real_path.len;
//     memcpy(rpath + rpath_len, req->path_normalized.base + req_path_prefix, req->path_normalized.len - req_path_prefix);
//     rpath_len += req->path_normalized.len - req_path_prefix;
//
//     h2o_resp_add_date_header(req);
//
//     h2o_iovec_t resolved_path = req->path_normalized;
//
//     /* build generator (as well as terminating the rpath and its length upon success) */
//     if (rpath[rpath_len - 1] == '/') {
//         h2o_iovec_t *index_file;
//         for (index_file = self->index_files; index_file->base != NULL; ++index_file) {
//             memcpy(rpath + rpath_len, index_file->base, index_file->len);
//             rpath[rpath_len + index_file->len] = '\0';
//             if ((generator = create_generator(req, rpath, rpath_len + index_file->len, &is_dir, self->flags)) != NULL) {
//                 rpath_len += index_file->len;
//                 resolved_path = h2o_concat(&req->pool, req->path_normalized, *index_file);
//                 goto Opened;
//             }
//             if (is_dir) {
//                 /* note: apache redirects "path/" to "path/index.txt/" if index.txt is a dir */
//                 h2o_iovec_t dest = h2o_concat(&req->pool, req->path_normalized, *index_file, h2o_iovec_init(H2O_STRLIT("/")));
//                 dest = h2o_uri_escape(&req->pool, dest.base, dest.len, "/");
//                 if (req->query_at != SIZE_MAX)
//                     dest =
//                         h2o_concat(&req->pool, dest, h2o_iovec_init(req->path.base + req->query_at, req->path.len - req->query_at));
//                 h2o_send_redirect(req, 301, "Moved Permantently", dest.base, dest.len);
//                 return 0;
//             }
//             if (errno != ENOENT)
//                 break;
//         }
//         if (index_file->base == NULL && (self->flags & H2O_FILE_FLAG_DIR_LISTING) != 0) {
//             rpath[rpath_len] = '\0';
//             int is_get = 0;
//             if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET"))) {
//                 is_get = 1;
//             } else if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("HEAD"))) {
//                 /* ok */
//             } else {
//                 send_method_not_allowed(req);
//                 return 0;
//             }
//             if (send_dir_listing(req, rpath, rpath_len, is_get) == 0)
//                 return 0;
//         }
//     } else {
//         rpath[rpath_len] = '\0';
//         if ((generator = create_generator(req, rpath, rpath_len, &is_dir, self->flags)) != NULL)
//             goto Opened;
//         if (is_dir) {
//             h2o_iovec_t dest = h2o_concat(&req->pool, req->path_normalized, h2o_iovec_init(H2O_STRLIT("/")));
//             dest = h2o_uri_escape(&req->pool, dest.base, dest.len, "/");
//             if (req->query_at != SIZE_MAX)
//                 dest = h2o_concat(&req->pool, dest, h2o_iovec_init(req->path.base + req->query_at, req->path.len - req->query_at));
//             h2o_send_redirect(req, 301, "Moved Permanently", dest.base, dest.len);
//             return 0;
//         }
//     }
//     /* failed to open */
//
//     if (errno == ENFILE || errno == EMFILE) {
//         h2o_send_error_503(req, "Service Unavailable", "please try again later", 0);
//     } else {
//         if (h2o_mimemap_has_dynamic_type(self->mimemap) && try_dynamic_request(self, req, rpath, rpath_len) == 0)
//             return 0;
//         if (errno == ENOENT || errno == ENOTDIR) {
//             return -1;
//         } else {
//             h2o_send_error_403(req, "Access Forbidden", "access forbidden", 0);
//         }
//     }
//     return 0;
//
// Opened:
//     return serve_with_generator(generator, req, resolved_path, rpath, rpath_len,
//                                 h2o_mimemap_get_type_by_extension(self->mimemap, h2o_get_filext(rpath, rpath_len)));
// }
//
// static void on_context_init(h2o_handler_t *_self, h2o_context_t *ctx)
// {
//     h2o_file_handler_t *self = reinterpret_cast<h2o_file_handler_t *>(_self);
//
//     h2o_mimemap_on_context_init(self->mimemap, ctx);
// }
//
// static void on_context_dispose(h2o_handler_t *_self, h2o_context_t *ctx)
// {
//     h2o_file_handler_t *self = _self;
//
//     h2o_mimemap_on_context_dispose(self->mimemap, ctx);
// }
//
// static void on_handler_dispose(h2o_handler_t *_self)
// {
//     h2o_file_handler_t *self = _self;
//     size_t i;
//
//     free(self->conf_path.base);
//     free(self->real_path.base);
//     h2o_mem_release_shared(self->mimemap);
//     for (i = 0; self->index_files[i].base != NULL; ++i)
//         free(self->index_files[i].base);
// }
// //end
//
// h2o_file_handler_t *file_register(h2o_pathconf_t *pathconf, const char *real_path, const char **index_files,
//                                       h2o_mimemap_t *mimemap, int flags)
// {
//     h2o_file_handler_t *self;
//     size_t i;
//
//     if (index_files == nullptr)
//         index_files = default_index_files;
//
//     /* allocate memory */
//     for (i = 0; index_files[i] != nullptr; ++i)
//         ;
//     self = h2o_create_handler(pathconf, offsetof(h2o_file_handler_t, index_files[0]) + sizeof(self->index_files[0]) * (i + 1));
//
//     /* setup callbacks */
//     self->super.on_context_init = on_context_init;
//     self->super.on_context_dispose = on_context_dispose;
//     self->super.dispose = on_handler_dispose;
//     self->super.on_req = on_req;
//
//     /* setup attributes */
//     self->conf_path = h2o_strdup_slashed(nullptr, pathconf->path.base, pathconf->path.len);
//     self->real_path = h2o_strdup_slashed(nullptr, real_path, SIZE_MAX);
//     if (mimemap != nullptr) {
//         h2o_mem_addref_shared(mimemap);
//         self->mimemap = mimemap;
//     } else {
//         self->mimemap = h2o_mimemap_create();
//     }
//     self->flags = flags;
//     for (i = 0; index_files[i] != nullptr; ++i) {
//         self->index_files[i] = h2o_strdup(nullptr, index_files[i], SIZE_MAX);
//         if (self->max_index_file_len < self->index_files[i].len)
//             self->max_index_file_len = self->index_files[i].len;
//     }
//
//     return self;
// }

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