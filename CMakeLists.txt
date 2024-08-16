cmake_minimum_required(VERSION 3.28)
project(webproxy)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -fpermissive")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
option(TBB_TEST OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(deps/h2o)
find_package(TBB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB)
find_path(LIBUV_INCLUDE_DIR NAMES uv.h)
find_library(LIBUV_LIBRARIES NAMES uv libuv)

include(FetchContent)
FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG master # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_GetProperties(yaml-cpp)

if(NOT yaml-cpp_POPULATED)
    message(STATUS "Fetching yaml-cpp...")
    FetchContent_Populate(yaml-cpp)
    add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
endif()

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})
#target_link_libraries(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/deps/h2o/build/libh2o.a OpenSSL::SSL ${LIBUV_LIBRARIES}
#        ZLIB::ZLIB yaml-cpp::yaml-cpp TBB::tbb)
target_link_libraries(${PROJECT_NAME} libh2o-evloop OpenSSL::SSL ${LIBUV_LIBRARIES}
        ZLIB::ZLIB yaml-cpp::yaml-cpp TBB::tbb)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include ${LIBUV_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE "$<TARGET_PROPERTY:h2o,INTERFACE_INCLUDE_DIRECTORIES>")
#target_include_directories(${PROJECT_NAME} INTERFACE libh2o)

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)
