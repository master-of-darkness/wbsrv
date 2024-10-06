#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <cstring>
#include <cassert>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <memory>

#include "fastcgi.h"

fastcgi::Client::Client(const std::string& listen, int port)
{
    bzero(&header, sizeof(header));

    auto* sin = new struct sockaddr_in;
    bzero(sin, sizeof(struct sockaddr_in));
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = inet_addr(listen.c_str());
    sin->sin_port = htons(port);

    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    address = reinterpret_cast<struct sockaddr*>(sin);
    addressLength = sizeof(*sin);
}

fastcgi::Client::Client(const std::string& listen)
{
    bzero(&header, sizeof(header));

    auto* sun = new struct sockaddr_un;

    bzero(sun, sizeof(struct sockaddr_un));
    sun->sun_family = AF_LOCAL;
    strcpy(sun->sun_path, listen.c_str());
    socket_ = socket(PF_UNIX, SOCK_STREAM, 0);
    address = reinterpret_cast<struct sockaddr*>(sun);
    addressLength = sizeof(*sun);
}

fastcgi::Client::~Client()
{
    delete address;
}

bool fastcgi::Client::sendRequest(const std::string*stdin)
{
    buildRecord(stdin);

    if (!connectToServer())
    {
        return false;
    }

    write(socket_, record.data(), record.size());

    if (socket_ > 0)
    {
        close(socket_);
    }

    return true;
}

std::string fastcgi::Client::createRequest(const std::string*stdin)
{
    buildRecord(stdin);

    if (!connectToServer())
    {
        return "Unable to connect to FastCGI application.";
    }
    write(socket_, record.data(), record.size());

    do
    {
        readPacket();
    }
    while (header.type != FCGI_END_REQUEST);

    if (socket_ > 0)
    {
        close(socket_);
    }

    switch (header.flag)
    {
    case FCGI_REQUEST_COMPLETE:
        return response;
    case FCGI_CANT_MPX_CONN:
        return "This app can't multiplex [CANT_MPX_CONN]";
    case FCGI_OVERLOADED:
        return "New request rejected; too busy [OVERLOADED]";
    case FCGI_UNKNOWN_ROLE:
        return "Role value not known [UNKNOWN_ROLE]";
    default:
        return response;
    }
}

void fastcgi::Client::buildRecord(const std::string*stdin)
{
    std::stringstream conlength;
    conlength << stdin->length();

    parameters["CONTENT_LENGTH"] = conlength.str();
    record.clear();

    std::string head;
    head += static_cast<char>(0);
    head += static_cast<char>(1);
    head += static_cast<char>(0);
    head += static_cast<char>(0);
    head += static_cast<char>(0);
    head += static_cast<char>(0);
    head += static_cast<char>(0);
    head += static_cast<char>(0);
    buildPacket(FCGI_BEGIN_REQUEST, &head, 1);

    std::string paramsRequest;
    for (auto& param : parameters)
    {
        paramsRequest += buildNameValuePair(param.first, param.second);
    }

    std::string empty;
    if (!paramsRequest.empty())
    {
        buildPacket(FCGI_PARAMS, &paramsRequest, 1);
    }
    buildPacket(FCGI_PARAMS, &empty, 1);

    if (stdin->size() > 0)
    {
        buildPacket(FCGI_STDIN, stdin, 1);
    }
    buildPacket(FCGI_STDIN, &empty, 1);
}

void fastcgi::Client::buildPacket(int type, const std::string* content, const int requestId)
{
    size_t contentLength = content->size();

    assert(contentLength >= 0 && contentLength <= MAX_CONTENT_LENGTH);

    record += static_cast<char>(FCGI_VERSION_1);
    record += static_cast<char>(type);
    record += static_cast<char>((requestId >> 8) & 0xff);
    record += static_cast<char>((requestId) & 0xff);
    record += static_cast<char>((contentLength >> 8) & 0xff);
    record += static_cast<char>((contentLength) & 0xff);
    record += static_cast<char>(0);
    record += static_cast<char>(0);
    record.append(content->data(), content->size());
}

std::string fastcgi::Client::encodeContentLength(size_t length)
{
    std::string encoded;
    if (length < 128)
    {
        encoded += static_cast<char>(length);
    }
    else
    {
        encoded += static_cast<char>((length >> 24) | 0x80);
        encoded += static_cast<char>((length >> 16) & 0xff);
        encoded += static_cast<char>((length >> 8) & 0xff);
        encoded += static_cast<char>(length & 0xff);
    }
    return encoded;
}

std::string fastcgi::Client::buildNameValuePair(const std::string& name, const std::string& value)
{
    std::string nvpair;

    nvpair += encodeContentLength(name.size());
    nvpair += encodeContentLength(value.size());
    nvpair += name + value;

    return nvpair;
}


void fastcgi::Client::readPacketHeader()
{
    char pack[HEADER_LENGTH];

    ssize_t len = read(socket_, pack, sizeof(pack));
    if (len < 0)
    {
        throw std::runtime_error("Unable to read response header.");
    }
    header.version = toUnsignedByte(pack[0]);
    header.type = toUnsignedByte(pack[1]);
    header.requestId = (toUnsignedByte(pack[2]) << 8) + toUnsignedByte(pack[3]);
    header.contentLength = (toUnsignedByte(pack[4]) << 8) + toUnsignedByte(pack[5]);
    header.paddingLength = toUnsignedByte(pack[6]);
    header.reserved = toUnsignedByte(pack[7]);
}

void fastcgi::Client::readPacket()
{
    readPacketHeader();

    if (header.contentLength > 0)
    {
        char* buff = new char[header.contentLength];
        ssize_t length = read(socket_, buff, header.contentLength);
        if (length < 0)
        {
            throw std::runtime_error("Unable to read content buffer.");
        }

        if (header.type == FCGI_STDOUT || header.type == FCGI_STDERR)
        {
            response.append(buff);
        }
        else if (header.type == FCGI_END_REQUEST)
        {
            header.flag = buff[4];
        }
        delete [] buff;
    }

    if (header.paddingLength > 0)
    {
        char* padding = new char[header.paddingLength];
        ssize_t len = read(socket_, padding, header.paddingLength);
        delete [] padding;
        if (len < 0)
        {
            throw std::runtime_error("Unable to read padding buffer.");
        }
    }
}

bool fastcgi::Client::connectToServer() const
{
    if (connect(socket_, address, addressLength))
    {
        return false;
    }
    return true;
}
