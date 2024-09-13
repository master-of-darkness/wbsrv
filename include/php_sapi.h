#pragma once

class EmbedPHP {
public:
    EmbedPHP();
    ~EmbedPHP();

    void executeScript(std::string path, std::string &retval);
private:
    std::mutex m;
};
