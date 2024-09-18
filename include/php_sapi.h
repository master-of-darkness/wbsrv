#pragma once

namespace EmbedPHP {
    void executeScript(const std::string &path, std::string &retval, const std::unique_ptr<proxygen::HTTPMessage> &headers);
}
