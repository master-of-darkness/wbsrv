cmake_minimum_required(VERSION 3.28)

# Compiler settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_COMPILER  "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

if(EXISTS "/usr/bin/clang-scan-deps")
    set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "/usr/bin/clang-scan-deps")
elseif (EXISTS "/usr/bin/clang-scan-deps-18")
    set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "/usr/bin/clang-scan-deps-18")
elseif (EXISTS "/usr/bin/clang-scan-deps-19")
    set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "/usr/bin/clang-scan-deps-19")
else()
    message(FATAL_ERROR "No clang-tools have been found. Please, run: sudo apt install clang-tools")
endif ()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -fpermissive")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math")

# VCPKG settings
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_LIBRARY_LINKAGE static)

project(wbsrv_workspace)

# Add module path for shared CMake files
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

# Find common packages that might be used across subprojects
find_package(proxygen CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(gflags REQUIRED)
find_package(benchmark REQUIRED)
find_package(xxHash REQUIRED)

# Add subdirectories for each component
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(plugins)