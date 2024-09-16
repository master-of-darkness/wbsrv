cmake_minimum_required(VERSION 3.28)
project(wbsrv)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -fpermissive")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_LIBRARY_LINKAGE static)
set(FINDPHP_DEBUG ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

option(TBB_TEST OFF)

find_package(proxygen CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(gflags REQUIRED)
find_package(TBB REQUIRED)

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        "/usr/include/php/20230831/sapi"
        "/usr/include/php/20230831/ext"
        "/usr/include/php/20230831/main"
        "/usr/include/php/20230831/TSRM"
        "/usr/include/php/20230831/Zend"
        "/usr/include/php/20230831"

)


target_link_libraries(${PROJECT_NAME} PRIVATE
        proxygen::proxygen
        proxygen::proxygenhttpserver
        yaml-cpp::yaml-cpp
        TBB::tbb
        /usr/lib/libphp.so)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG> CDS_THREADING_CXX11)
