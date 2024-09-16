#ifndef FCGI_H_
#define FCGI_H_

#include <string>
#include <map>

namespace fastcgi {
    inline unsigned int toUnsignedByte(int value) {
        return static_cast<unsigned int>(value) & 0xff;
    }

    struct Header {
        unsigned int version;
        unsigned int type;
        unsigned int requestId;
        unsigned int contentLength;
        unsigned int paddingLength;
        unsigned int reserved;
        unsigned int flag;
    };

    constexpr unsigned int FCGI_VERSION_1 = 1;

    constexpr unsigned int FCGI_BEGIN_REQUEST = 1;
    constexpr unsigned int FCGI_ABORT_REQUEST = 2;
    constexpr unsigned int FCGI_END_REQUEST = 3;
    constexpr unsigned int FCGI_PARAMS = 4;
    constexpr unsigned int FCGI_STDIN = 5;
    constexpr unsigned int FCGI_STDOUT = 6;
    constexpr unsigned int FCGI_STDERR = 7;

    constexpr int FCGI_DATA = 8;
    constexpr int FCGI_GET_VALUES = 9;
    constexpr int FCGI_GET_VALUES_RESULT = 10;
    constexpr int FCGI_UNKNOWN_TYPE = 11;
    constexpr int FCGI_MAX_TYPE = 11;

    constexpr int FCGI_ROLE_RESPONDER = 1;
    constexpr int FCGI_ROLE_AUTHORIZER = 2;
    constexpr int FCGI_ROLE_FILTER = 3;

    constexpr unsigned char FCGI_REQUEST_COMPLETE = 0;
    constexpr unsigned char FCGI_CANT_MPX_CONN = 1;
    constexpr unsigned char FCGI_OVERLOADED = 2;
    constexpr unsigned char FCGI_UNKNOWN_ROLE = 3;

    constexpr char FCGI_MAX_CONNS[] = "MAX_CONNS";
    constexpr char FCGI_MAX_REQS[] = "MAX_REQS";
    constexpr char FCGI_MPXS_CONNS[] = "MPXS_CONNS";

    constexpr int HEADER_LENGTH = 8;
    constexpr int READ_BUFFER_LENGTH = 1024;
    constexpr int MAX_CONTENT_LENGTH = 0xffff;

    class Client {
    public:
        Client(const std::string &listenAddress, int port);

        Client(const std::string &listenAddress);

        std::map<std::string, std::string> parameters;
        std::string record;
        std::string response;
        Header header{};
        int socket_;
        struct sockaddr *address;
        int addressLength;

        ~Client();

        bool sendRequest(const std::string *stdinData);

        std::string createRequest(const std::string *stdinData);

    private:
        void buildRecord(const std::string *stdinData);

        void buildPacket(int type, const std::string *content, int requestId);

        static std::string buildNameValuePair(const std::string &name, const std::string &value);

        static std::string encodeContentLength(size_t length);

        void readPacketHeader();

        void readPacket();

        bool connectToServer() const;
    };
}

#endif
