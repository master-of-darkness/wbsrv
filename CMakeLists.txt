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
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# VCPKG settings
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_LIBRARY_LINKAGE static)

project(wbsrv)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

find_package(proxygen CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(gflags REQUIRED)
find_package(benchmark REQUIRED)

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC} include/interface.h)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PROCESSED_INCLUDE_DIRS}
)


target_link_libraries(${PROJECT_NAME} PRIVATE
        proxygen::proxygen
        proxygen::proxygenhttpserver
        yaml-cpp::yaml-cpp
#        ${PHP_EMBED_LIB}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)

# Link Google Benchmark
add_executable(benchmark_concurrent_lru_cache tests/benchmark_concurrent_lru_cache.cpp src/utils/concurrent_cache.h)
target_link_libraries(benchmark_concurrent_lru_cache benchmark::benchmark)

add_library(test_plugin SHARED tests/plugin/ExamplePlugin.cpp tests/plugin/ExamplePlugin.h include/interface.h)


# PHP plugin support
add_library(php_plugin SHARED include/interface.h php_ext/php_ext.cpp php_ext/php_ext.h)
target_include_directories(php_plugin PRIVATE
        php_ext/php-src/
        php_ext/php-src/main
        php_ext/php-src/sapi
        php_ext/php-src/TSRM
        php_ext/php-src/Zend
        php_ext/php-src/ext
)
target_link_libraries(php_plugin PRIVATE ${CMAKE_SOURCE_DIR}/php_ext/php-src/libs/libphp.so)
