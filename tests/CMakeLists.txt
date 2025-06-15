# Benchmark executable
add_executable(benchmark_concurrent_lru_cache
        benchmark_concurrent_lru_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/utils/concurrent_cache.h
)

target_include_directories(benchmark_concurrent_lru_cache PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(benchmark_concurrent_lru_cache
        benchmark::benchmark
)

add_subdirectory(plugin)