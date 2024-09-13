// #include <iostream>
// #include <sstream>
// #include <string>
// #include <unistd.h>
//
// #include "../include/fastcgi.h"
//
// int main(int argc, char **argv) {
//     fastcgi::Client fcgcli("127.0.0.1", 9000);
//
//     std::string response, contents("values");
//
//     fcgcli.parameters["GATEWAY_INTERFACE"] = "FastCGI/2.0";
//     fcgcli.parameters["REQUEST_METHOD"]    = "GET";
//     fcgcli.parameters["SCRIPT_FILENAME"]   = "/var/www/html/index.php";
//     fcgcli.parameters["SERVER_SOFTWARE"]   = "wbsrv";
//     fcgcli.parameters["SERVER_PROTOCOL"]   = "HTTP/2";
//     fcgcli.parameters["CONTENT_TYPE"]      = "application/x-www-form-urlencoded";
//     std::cout<<fcgcli.createRequest(&contents);
//
//     return 0;
// }