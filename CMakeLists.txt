cmake_minimum_required(VERSION 3.28)
project(webproxy)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -fpermissive")

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
option(TBB_TEST OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

add_subdirectory(deps/h2o)
set_target_properties(h2o PROPERTIES EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)

get_target_property(LIBH2O_INCLUDE_DIRS libh2o INCLUDE_DIRECTORIES)
get_target_property(LIBH2O_LINK_LIBS libh2o LINK_LIBRARIES)
get_target_property(LIBH2O_SOURCES libh2o SOURCES)

message(STATUS "[libh2o] Include directories: ${LIBH2O_INCLUDE_DIRS}")
message(STATUS "[libh2o] Linked libraries: ${LIBH2O_LINK_LIBS}")
set(LIBH2O_SOURCES_MOD "")
foreach(item ${LIBH2O_SOURCES})
    if(NOT ${item} MATCHES "${CMAKE_SOURCE_DIR}")
        string(CONCAT modified_item "${CMAKE_SOURCE_DIR}/deps/h2o/" ${item})
        list(APPEND LIBH2O_SOURCES_MOD ${modified_item})
    else ()
        list(APPEND LIBH2O_SOURCES_MOD ${item})
    endif ()
endforeach()
message(STATUS "[libh2o] Sources: ${LIBH2O_SOURCES_MOD}")

find_package(TBB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

find_path(LIBUV_INCLUDE_DIR NAMES uv.h)
find_library(LIBUV_LIBRARIES NAMES uv libuv)

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC} ${LIBH2O_SOURCES_MOD})
target_link_libraries(${PROJECT_NAME} OpenSSL::SSL ${LIBUV_LIBRARIES} ${LIBH2O_LINK_LIBS} ZLIB::ZLIB TBB::tbb yaml)
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${LIBUV_INCLUDE_DIR}
        ${LIBH2O_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)
