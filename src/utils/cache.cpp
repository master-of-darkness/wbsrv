#include "cache.h"

namespace Cache {
    ARC<std::string, VirtualHostConfig> host_config_cache(100);
    ARC<std::string, FileSystemMetadata> file_metadata_cache(1000);
}
