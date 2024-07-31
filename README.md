# wbsrv

`wbsrv` is a high-performance web server designed to handle heavy traffic efficiently and securely. This project is built with a focus on speed, scalability, and ease of use.

## Features

- **High Performance:** Optimized for handling thousands of concurrent connections.
- **Scalability:** Easily scales with your infrastructure needs.
- **Ease of Use:** Simple configuration and deployment process.
- **Cache system**: Least Recently Used algorithm of caching allows to store pages in memory
## Benchmarks results

Tested on **Dell XPS 13 9310** laptop with following hardware:
- Processor: 11th Gen Intel i7-1185G7 (8) @ 4.800GHz
- Operating System: Fedora Linux 40 (KDE Plasma) x86_64
- Kernel: 6.9.7-200.fc40.x86_64
- Memory: 15.3 GiB of RAM
## ![Screenshot](https://i.imgur.com/p7X24U1.png)
                    
## Installation

### Prerequisites

- [CMake](https://cmake.org/)
- A C++20 compatible compiler (e.g., GCC, Clang)
- Linux-based system
- [Proxygen](https://github.com/facebook/proxygen) library and its dependencies

### Steps

1. Clone the repository:
    ```sh
    git clone https://github.com/master-of-darkness/wbsrv.git
    cd wbsrv
    ```

2. If you are going to use as daemon, you should to build project in **Release**. Build the project:
    ```sh
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make
    ```

3. Run the server:
    ```sh
    ./wbsrv
    ```

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

```

## Dependencies
- [Proxygen](https://github.com/facebook/proxygen)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)