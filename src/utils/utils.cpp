#include "utils.h"

utils::ConcurrentLRUCache<std::string, CacheRow> utils::cache(256);

namespace utils {
    const char* getContentType(const std::string& path) {
        const size_t len = path.length();
        if (len < 3) return "application/octet-stream";

        // Check last 4 characters for maximum efficiency
        const char c1 = path[len - 1];
        const char c2 = path[len - 2];
        const char c3 = len >= 3 ? path[len - 3] : 0;
        const char c4 = len >= 4 ? path[len - 4] : 0;
        const char c5 = len >= 5 ? path[len - 5] : 0;

        // .html / .HTML
        if ((c1 == 'l' || c1 == 'L') && (c2 == 'm' || c2 == 'M') &&
            (c3 == 't' || c3 == 'T') && (c4 == 'h' || c4 == 'H') && c5 == '.') {
            return "text/html";
        }

        // .htm / .HTM
        if ((c1 == 'm' || c1 == 'M') && (c2 == 't' || c2 == 'T') &&
            (c3 == 'h' || c3 == 'H') && c4 == '.') {
            return "text/html";
        }

        // .css / .CSS
        if ((c1 == 's' || c1 == 'S') && (c2 == 's' || c2 == 'S') &&
            (c3 == 'c' || c3 == 'C') && c4 == '.') {
            return "text/css";
        }

        // .js / .JS
        if ((c1 == 's' || c1 == 'S') && (c2 == 'j' || c2 == 'J') && c3 == '.') {
            return "application/javascript";
        }

        // .png / .PNG
        if ((c1 == 'g' || c1 == 'G') && (c2 == 'n' || c2 == 'N') &&
            (c3 == 'p' || c3 == 'P') && c4 == '.') {
            return "image/png";
        }

        // .jpg / .JPG
        if ((c1 == 'g' || c1 == 'G') && (c2 == 'p' || c2 == 'P') &&
            (c3 == 'j' || c3 == 'J') && c4 == '.') {
            return "image/jpeg";
        }

        // .jpeg / .JPEG
        if (len >= 5 && (c1 == 'g' || c1 == 'G') && (c2 == 'e' || c2 == 'E') &&
            (c3 == 'p' || c3 == 'P') && (c4 == 'j' || c4 == 'J') &&
            (path[len - 5] == '.' )) {
            return "image/jpeg";
        }

        // .gif / .GIF
        if ((c1 == 'f' || c1 == 'F') && (c2 == 'i' || c2 == 'I') &&
            (c3 == 'g' || c3 == 'G') && c4 == '.') {
            return "image/gif";
        }

        // .pdf / .PDF
        if ((c1 == 'f' || c1 == 'F') && (c2 == 'd' || c2 == 'D') &&
            (c3 == 'p' || c3 == 'P') && c4 == '.') {
            return "application/pdf";
        }

        // .txt / .TXT
        if ((c1 == 't' || c1 == 'T') && (c2 == 'x' || c2 == 'X') &&
            (c3 == 't' || c3 == 'T') && c4 == '.') {
            return "text/plain";
        }

        // .json / .JSON
        if (len >= 5 && (c1 == 'n' || c1 == 'N') && (c2 == 'o' || c2 == 'O') &&
            (c3 == 's' || c3 == 'S') && (c4 == 'j' || c4 == 'J') &&
            (path[len - 5] == '.')) {
            return "application/json";
        }

        // .xml / .XML
        if ((c1 == 'l' || c1 == 'L') && (c2 == 'm' || c2 == 'M') &&
            (c3 == 'x' || c3 == 'X') && c4 == '.') {
            return "application/xml";
        }

        // .ico / .ICO
        if ((c1 == 'o' || c1 == 'O') && (c2 == 'c' || c2 == 'C') &&
            (c3 == 'i' || c3 == 'I') && c4 == '.') {
            return "image/vnd.microsoft.icon";
        }

        // .svg / .SVG
        if ((c1 == 'g' || c1 == 'G') && (c2 == 'v' || c2 == 'V') &&
            (c3 == 's' || c3 == 'S') && c4 == '.') {
            return "image/svg+xml";
        }

        // .mp3 / .MP3
        if (c1 == '3' && (c2 == 'p' || c2 == 'P') &&
            (c3 == 'm' || c3 == 'M') && c4 == '.') {
            return "audio/mpeg";
        }

        // .mp4 / .MP4
        if (c1 == '4' && (c2 == 'p' || c2 == 'P') &&
            (c3 == 'm' || c3 == 'M') && c4 == '.') {
            return "video/mp4";
        }

        // .zip / .ZIP
        if ((c1 == 'p' || c1 == 'P') && (c2 == 'i' || c2 == 'I') &&
            (c3 == 'z' || c3 == 'Z') && c4 == '.') {
            return "application/zip";
        }

        // .gz / .GZ
        if ((c1 == 'z' || c1 == 'Z') && (c2 == 'g' || c2 == 'G') && c3 == '.') {
            return "application/gzip";
        }

        // .bmp / .BMP
        if ((c1 == 'p' || c1 == 'P') && (c2 == 'm' || c2 == 'M') &&
            (c3 == 'b' || c3 == 'B') && c4 == '.') {
            return "image/bmp";
        }

        // .avi / .AVI
        if ((c1 == 'i' || c1 == 'I') && (c2 == 'v' || c2 == 'V') &&
            (c3 == 'a' || c3 == 'A') && c4 == '.') {
            return "video/x-msvideo";
        }

        // .tar / .TAR
        if ((c1 == 'r' || c1 == 'R') && (c2 == 'a' || c2 == 'A') &&
            (c3 == 't' || c3 == 'T') && c4 == '.') {
            return "application/x-tar";
        }

        // .rar / .RAR
        if ((c1 == 'r' || c1 == 'R') && (c2 == 'a' || c2 == 'A') &&
            (c3 == 'r' || c3 == 'R') && c4 == '.') {
            return "application/x-rar-compressed";
        }

        // .7z / .7Z
        if ((c1 == 'z' || c1 == 'Z') && c2 == '7' && c3 == '.') {
            return "application/x-7z-compressed";
        }

        // Longer extensions (4+ chars after dot)
        if (len >= 6) {
            const char c6 = path[len - 6];

            // .xlsx / .XLSX
            if ((c1 == 'x' || c1 == 'X') && (c2 == 's' || c2 == 'S') &&
                (c3 == 'l' || c3 == 'L') && (c4 == 'x' || c4 == 'X') && c5 == '.') {
                return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
            }

            // .docx / .DOCX
            if ((c1 == 'x' || c1 == 'X') && (c2 == 'c' || c2 == 'C') &&
                (c3 == 'o' || c3 == 'O') && (c4 == 'd' || c4 == 'D') && c5 == '.') {
                return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
            }

            // .pptx / .PPTX
            if ((c1 == 'x' || c1 == 'X') && (c2 == 't' || c2 == 'T') &&
                (c3 == 'p' || c3 == 'P') && (c4 == 'p' || c4 == 'P') && c5 == '.') {
                return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
            }
        }

        if (len >= 5) {
            // .xls / .XLS
            if ((c1 == 's' || c1 == 'S') && (c2 == 'l' || c2 == 'L') &&
                (c3 == 'x' || c3 == 'X') && c4 == '.') {
                return "application/vnd.ms-excel";
            }

            // .doc / .DOC
            if ((c1 == 'c' || c1 == 'C') && (c2 == 'o' || c2 == 'O') &&
                (c3 == 'd' || c3 == 'D') && c4 == '.') {
                return "application/msword";
            }

            // .ppt / .PPT
            if ((c1 == 't' || c1 == 'T') && (c2 == 'p' || c2 == 'P') &&
                (c3 == 'p' || c3 == 'P') && c4 == '.') {
                return "application/vnd.ms-powerpoint";
            }
        }

        return "application/octet-stream";
    }

    const char* getErrorPage(const int error) {
        switch (error) {
            case 400: return "<html><head><title>400 Bad Request</title></head><body><center><h1>400 Bad Request</h1></center><hr><center>WBSRV</center></body></html>";
            case 403: return "<html><head><title>403 Forbidden</title></head><body><center><h1>403 Forbidden</h1></center><hr><center>WBSRV</center></body></html>";
            case 404: return "<html><head><title>404 Not Found</title></head><body><center><h1>404 Not Found</h1></center><hr><center>WBSRV</center></body></html>";
            case 405: return "<html><head><title>405 Method Not Allowed</title></head><body><center><h1>405 Method Not Allowed</h1></center><hr><center>WBSRV</center></body></html>";
            case 500: return "<html><head><title>500 Internal Server Error</title></head><body><center><h1>500 Internal Server Error</h1></center><hr><center>WBSRV</center></body></html>";
            case 502: return "<html><head><title>502 Bad Gateway</title></head><body><center><h1>502 Bad Gateway</h1></center><hr><center>WBSRV</center></body></html>";
            case 503: return "<html><head><title>503 Service Unavailable</title></head><body><center><h1>503 Service Unavailable</h1></center><hr><center>WBSRV</center></body></html>";
            case 504: return "<html><head><title>504 Gateway Timeout</title></head><body><center><h1>504 Gateway Timeout</h1></center><hr><center>WBSRV</center></body></html>";
            default: return nullptr;
        }
    }
} // namespace utils