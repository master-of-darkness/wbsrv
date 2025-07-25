#pragma once
#define STATUS_101 101, "Switching Protocols"
#define STATUS_200 200, "OK"
#define STATUS_404 404, "Not found"
#define STATUS_400 400, "Bad request"
#define STATUS_405 405, "Method Not Allowed"
#define STATUS_500 500, "Internal server error"
#define STATUS_501 501, "Not implemented"
#define STATUS_502 502, "Bad response"
#define STATUS_503 503, "Forbidden"
#define STATUS_504 504, "Not authorized"
#define STATUS_505 505, "Not implemented"
#define STATUS_506 506, "Not authorized"
#define STATUS_507 507, "Permission denied"
#define STATUS_508 508, "Resource Limit Reached"

#define CONFIG_DIR "/etc/wbsrv/"
#define DAEMON_NAME "webserver"
#define VERSION_NUM "1.3"
#define NAME_N_VERSION "wbsrv rc"

#define CACHE_TTL 300 // 5 minutes
