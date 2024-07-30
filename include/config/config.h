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
    private:
        std::string path_;
    };

    class VHost {
    public:
        explicit VHost(std::string path) {
            path_ = std::move(path);
        }

        bool Load();

        std::string private_key;
        std::string cert;
        std::string password;
        std::string hostname;
        std::string web_dir;
        bool ssl = false;

        int port = 80;
    private:
        std::string path_;
    };
}