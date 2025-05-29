# wbsrv

`wbsrv` is a high-performance web server designed to handle heavy traffic efficiently and securely. This project is built with a focus on speed, scalability, and ease of use.

**DISCLAIMER:** Please, don't use it in production, it is still under heavy development.

## Features

- **High Performance:** Optimized for handling thousands of concurrent connections.
- **Scalability:** Easily scales with your infrastructure needs.
- **Ease of Use:** Simple configuration and deployment process.
- **Cache system:** Least Recently Used algorithm of caching allows storing pages in memory.
- **PHP:** Implemented with Embed SAPI.

### Prerequisites

- [CMake](https://cmake.org/)
- A C++20 compatible compiler (Clang)
- Linux-based system
- [Proxygen](https://github.com/facebook/proxygen) library and its dependencies
- Vcpkg package manager

### Steps to Build on Ubuntu

1. **Install Dependencies:**

    ```sh
    sudo apt update
    sudo apt install -y build-essential cmake clang libboost-all-dev
    ```

2. **Install Vcpkg:**

    ```sh
    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ```

3. **Install Proxygen and other dependencies using Vcpkg:**

    ```sh
    ./vcpkg install proxygen yaml-cpp
    ```

4. **Clone the `wbsrv` repository:**

    ```sh
    git clone https://github.com/master-of-darkness/wbsrv.git
    cd wbsrv
    ```

5. **Configure and Build the Project:**

    ```sh
    mkdir build
    cd build
    cmake -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ..
    cmake --build .
    ```

    - **Release Build:** Runs the server as a service in the background.
    - **Debug Build:** Runs the server as a process in the foreground.

### Usage

Create a configuration file and start the server. We recommend using the **Release** build type as it uses compiler optimizations and works as a daemon application.
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

## Future Plans:
- Implement PHP core configuration directives
- Add file upload support
- Support custom .so PHP extensions with dlopen (testing in branch)
- Implement proper caching of requests based on requested URL
