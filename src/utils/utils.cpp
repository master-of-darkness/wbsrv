#include "utils/utils.h"

constexpr std::pair<const char *, const char *> contentTypes[] = {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".txt",  "text/plain"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png",  "image/png"},
        {".pdf",  "application/pdf"},
        {".json", "application/json"},
        {".xml",  "application/xml"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".gif",  "image/gif"},
        {".bmp",  "image/bmp"},
        {".ico",  "image/vnd.microsoft.icon"},
        {".svg",  "image/svg+xml"},
        {".mp3",  "audio/mpeg"},
        {".mp4",  "video/mp4"},
        {".avi",  "video/x-msvideo"},
        {".zip",  "application/zip"},
        {".tar",  "application/x-tar"},
        {".gz",   "application/gzip"},
        {".rar",  "application/x-rar-compressed"},
        {".7z",   "application/x-7z-compressed"},
        {".xls",  "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".doc",  "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".ppt",  "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
};

namespace utils {

    const char* getContentType(const std::string &path) {
        for (const auto &extension: contentTypes) {
            if (path.ends_with(extension.first)) {
                return extension.second;
            }
        }
        return "application/octet-stream";
    }

} // namespace utils
