cmake_minimum_required(VERSION 3.28)
project(wbsrv)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
option(TBB_TEST OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(proxygen CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(gflags REQUIRED)
find_package(TBB REQUIRED)

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME} ${SRC})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include deps/lru-cache/include)
target_link_libraries(${PROJECT_NAME} PRIVATE proxygen::proxygen proxygen::proxygenhttpserver yaml-cpp::yaml-cpp TBB::tbb)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG> CDS_THREADING_CXX11)
