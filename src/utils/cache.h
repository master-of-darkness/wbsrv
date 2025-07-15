#pragma once
#include <memory>
#include <string>
#include <folly/FBString.h>

namespace folly {
    class IOBuf;
}

namespace Cache {
    struct VirtualHostConfig {
        folly::fbstring web_root_directory;
        std::vector<std::string> index_page_files;

        VirtualHostConfig() = default;

        VirtualHostConfig(const std::string &web_root, std::vector<std::string> index_files)
            : web_root_directory(web_root)
              , index_page_files(std::move(index_files)) {
        }
    };

    struct FileSystemMetadata {
        bool is_directory;
        // TODO: Add more metadata fields as needed
    };

    struct ResponseData {
        folly::fbstring content_type;
        std::shared_ptr<folly::IOBuf> data;

        ResponseData() = default;
    };
} // namespace Cache
