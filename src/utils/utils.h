#pragma once
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h> // xxh3_64bits
#include <chrono>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>

namespace Utils {
    const char *getContentType(const folly::fbstring &path);

    const char *getErrorPage(const int error);

    template<typename... Args>
    __attribute__((always_inline)) XXH64_hash_t computeXXH64Hash(const Args &... args) {
        XXH64_state_t state;
        XXH64_reset(&state, 0);
        (XXH64_update(&state, args.data(), args.size()), ...);
        return XXH64_digest(&state);
    }
}
