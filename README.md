# wbsrv

**`wbsrv`** is a high-performance, extensible web server designed for handling modern web workloads with maximum efficiency. Built in C++20 and powered by [Proxygen](https://github.com/facebook/proxygen).

> ⚠️ **Disclaimer:** This project is under active development and is not yet production-ready.

---

## 🚀 Features

- **High Performance** – Handles thousands of concurrent connections with event-driven architecture.
- **Scalable** – Easily configurable for multi-threaded deployments on multi-core systems.
- **Easy to Configure** – YAML-based configuration files for server and virtual hosts.
- **Smart Caching** – Built-in ARC (Adaptive Replacement Cache) to store frequently accessed content in memory.
- [**PHP Support**](https://github.com/master-of-darkness/wbsrv/tree/master/php_ext) – Native support for embedded PHP execution using the Embed SAPI.
- **Extensions API for Developers** – Add new features yourself. Check out the [example](https://github.com/master-of-darkness/wbsrv/blob/master/tests/plugin/ExamplePlugin.cpp).
---

## 🧰 Prerequisites

Ensure the following tools and libraries are installed on a **Linux-based** system:

- [CMake](https://cmake.org/)
- A C++20-compatible compiler (originally developed with Clang, not sure about GCC compatibility)
- [Vcpkg](https://github.com/microsoft/vcpkg)
- [Proxygen](https://github.com/facebook/proxygen) and dependencies
- `yaml-cpp` (for configuration parsing)

---

## 🛠 Build Instructions (Ubuntu)

### 1. Install System Dependencies

```bash
sudo apt update
sudo apt install -y build-essential cmake clang git libssl-dev
```

### 2. Install Vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg integrate install
```

### 3. Install Required Libraries

```bash
./vcpkg install proxygen yaml-cpp
```

### 4. Clone the Project

```bash
git clone https://github.com/master-of-darkness/wbsrv.git
cd wbsrv
```

### 5. Build the Server

```bash
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ..
cmake --build .
```

- **Release Mode:** Optimized, runs as a background service.
- **Debug Mode:** Includes debug symbols, runs in the foreground.

---

## ⚙️ Configuration

Create a `server.yaml` by path `/etc/wbsrv/` file to specify global server settings:

```yaml
threads: 6
plugins:
  - path: "/path/to/plugin.so"  # Path to a shared library plugin
    enabled: true
    config:  # Optional plugin-specific configuration
      key: "value"
```

Within the same directory, create a `hosts/` folder with individual virtual host configurations. Example: `hosts/localhost.yaml`

```yaml
www_dir: "/path/to/static/files"
hostname: "localhost"
certificate: "/path/to/cert.csr"
private_key: "/path/to/key.key"
password: "/path/to/password"  # Leave empty if no password
port: 11001
ssl: true
index_page: ['index.html']
```

---

## 🧪 Running the Server

From the `build` directory:

```bash
./wbsrv
```

> For best performance, use the **Release** build in production-like environments.

---

## 📦 Dependencies

- [Proxygen](https://github.com/facebook/proxygen) – HTTP framework
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) – YAML configuration parser
- [xxHash](https://github.com/Cyan4973/xxHash) - Fast hashing library for caching
---

## 🔭 Roadmap

Planned features include:

- [ ] File upload support
- [ ] URL-based caching for dynamic routes
- [ ] Advanced logging and access control
- [ ] WebSocket support
- [ ] Improved interface for caching container
---

## 📣 Contributing

Contributions are welcome! I'm open to pull requests, issues and critiques.
