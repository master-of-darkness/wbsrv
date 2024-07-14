#include <string>
#include <utility>

namespace Config {

    class General {
    public:
        explicit General(std::string path) {
            path_ = path;
        }
        bool Load();
        int threads = 0;
        std::string host;
        bool http2 = false;
    private:
        std::string path_;
    };

    class VHost {
    public:
        explicit VHost(std::string path) {
            path_ = path;
        }
        bool Load();

        std::string private_key;
        std::string cert;
        std::string password;
        std::string hostname;
        std::string web_dir;
        int port = 80;
    private:
        std::string path_;
    };
}