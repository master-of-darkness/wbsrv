cmake_minimum_required(VERSION 3.28)

# Compiler settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_COMPILER  "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "/usr/bin/clang-scan-deps-19")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -fpermissive")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# VCPKG settings
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_LIBRARY_LINKAGE static)

project(wbsrv)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

option(TBB_TEST OFF)

find_package(proxygen CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(gflags REQUIRED)
find_package(TBB REQUIRED)

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})


execute_process(
        COMMAND php-config --includes
        OUTPUT_VARIABLE PHP_INCLUDES
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Split the result from php-config into an array
separate_arguments(PHP_INCLUDE_DIRS UNIX_COMMAND "${PHP_INCLUDES}")
# Remove the `-I` from each include paphpath
set(PROCESSED_INCLUDE_DIRS "")
foreach(INCLUDE_DIR ${PHP_INCLUDE_DIRS})
    string(REGEX REPLACE "^-I" "" CLEANED_INCLUDE_DIR ${INCLUDE_DIR})  # Remove the -I
    list(APPEND PROCESSED_INCLUDE_DIRS ${CLEANED_INCLUDE_DIR})  # Add to new list
endforeach()


# custom php build test
set(PROCESSED_INCLUDE_DIRS "deps/php-src/" "deps/php-src/main" "deps/php-src/sapi" "deps/php-src/TSRM" "deps/php-src/Zend")

find_library(PHP_EMBED_LIB
        NAMES libphp.so
        HINTS
        /usr/lib
        /usr/local/lib
        /usr/lib64
        /usr/local/lib64
        /opt/php/lib
)

# custom php build test
set(PHP_EMBED_LIB ${CMAKE_SOURCE_DIR}/deps/php-src/libs/libphp.so)

message("PHP Library: ${PHP_EMBED_LIB}")
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${PROCESSED_INCLUDE_DIRS}
)


target_link_libraries(${PROJECT_NAME} PRIVATE
        proxygen::proxygen
        proxygen::proxygenhttpserver
        yaml-cpp::yaml-cpp
        TBB::tbb
        ${PHP_EMBED_LIB})
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG> CDS_THREADING_CXX11)
