#include <folly/Range.h>
#include "concurrent_lru_cache.h"

namespace utils {
    const char *getContentType(const std::string &path);
    const char *getErrorPage(const int &error);
}
