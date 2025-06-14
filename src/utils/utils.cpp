#include "utils.h"

utils::ConcurrentLRUCache<std::string, CacheRow> utils::cache(256);

constexpr std::pair<const char *, const char *> contentTypes[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".txt", "text/plain"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".pdf", "application/pdf"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".ico", "image/vnd.microsoft.icon"},
    {".svg", "image/svg+xml"},
    {".mp3", "audio/mpeg"},
    {".mp4", "video/mp4"},
    {".avi", "video/x-msvideo"},
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {".rar", "application/x-rar-compressed"},
    {".7z", "application/x-7z-compressed"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
};

constexpr std::pair<int, const char *> errorPages[] = {
    {
        400,
        "<html>"
        "<head><title>400 Bad Request</title></head>"
        "<body>"
        "<center><h1>400 Bad Request</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },

    {
        403,
        "<html>"
        "<head><title>403 Forbidden</title></head>"
        "<body>"
        "<center><h1>403 Forbidden</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },

    {
        404,
        "<html>"
        "<head><title>404 Not Found</title></head>"
        "<body>"
        "<center><h1>404 Not Found</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },
    {
        405,
        "<html>"
        "<head><title>405 Method Not Allowed</title></head>"
        "<body>"
        "<center><h1>405 Method Not Allowed</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },
    {
        500,
        "<html>"
        "<head><title>500 Internal Server Error</title></head>"
        "<body>"
        "<center><h1>500 Internal Server Error</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },

    {
        502,
        "<html>"
        "<head><title>502 Bad Gateway</title></head>"
        "<body>"
        "<center><h1>502 Bad Gateway</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },

    {
        503,
        "<html>"
        "<head><title>503 Service Unavailable</title></head>"
        "<body>"
        "<center><h1>503 Service Unavailable</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    },

    {
        504,
        "<html>"
        "<head><title>504 Gateway Timeout</title></head>"
        "<body>"
        "<center><h1>504 Gateway Timeout</h1></center>"
        "<hr><center>WBSRV</center>"
        "</body></html>"
    }
};


namespace utils {
    const char *getContentType(const std::string &path) {
        for (const auto &extension: contentTypes) {
            if (path.ends_with(extension.first)) {
                return extension.second;
            }
        }
        return "application/octet-stream";
    }

    const char *getErrorPage(const int &error) {
        for (const auto &errorPage: errorPages)
            if (errorPage.first == error)
                return errorPage.second;
    }
} // namespace utils
