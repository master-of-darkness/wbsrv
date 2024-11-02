# wbsrv

`wbsrv` is a high-performance web server designed to handle heavy traffic efficiently and securely. This project is built with a focus on speed, scalability, and ease of use.
**DISCLAIMER:** Please, don't use it in production, it still under heavily development
## Features

- **High Performance:** Optimized for handling thousands of concurrent connections.
- **Scalability:** Easily scales with your infrastructure needs.
- **Ease of Use:** Simple configuration and deployment process.
- **Cache system**: Least Recently Used algorithm of caching allows to store pages in memory
- **PHP**: Implemented with Embed SAPI 
## Benchmarks results

Tested on **Dell XPS 13 9310** laptop with following hardware:
- Processor: 11th Gen Intel i7-1185G7 (8) @ 4.800GHz
- Operating System: Kubuntu 24.04.1 LTS
- Kernel: 6.8.0-48-generic 
- Memory: 15.3 GiB of RAM
## ![Screenshot](https://i.imgur.com/XW1drkz.png)
                    
## Installation

### Prerequisites

- [CMake](https://cmake.org/)
- A C++20 compatible compiler (e.g., GCC, Clang)
- Linux-based system
- [Proxygen](https://github.com/facebook/proxygen) library and its dependencies
#### To build
- Vcpkg package manager

### Steps

#### Under construction 

## Usage

Create configuration and start the server. We recommend you to use **Release** build type as it uses compiler optimizations and works as daemon application. 

### Configuration

The `server.yaml` file allows you to customize threads quantity.

```yaml
threads: 8
```

In the same path with `server.yaml` there must be directory `hosts`. In that directory must be virtual hosts configuration. One file - one virtual host. Example of `hosts/localhost.yaml`
```yaml
www_dir: "/path/to/files/dir" # your static files root
hostname: "localhost" # domain name
certificate: "/path/to/certificate" # .csr extension
private_key: "/path/to/key" # .key extension
password: "/path/to/file/with/password" # or if there is no password leave it blank
port: 11001
ssl: true # false if you don't want to use secure connection
index_page: ['index.html'] # default filename if no exact file specified in request
```

## Dependencies
- [Proxygen](https://github.com/facebook/proxygen)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)
- [oneTBB](https://github.com/oneapi-src/oneTBB)

In near future:
- create php core configuration directives
- file upload support
- cookies
- custom .so php support with dlopen
