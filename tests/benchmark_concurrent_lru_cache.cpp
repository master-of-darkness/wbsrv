#include <benchmark/benchmark.h>
#include "../src/utils/concurrent_cache.h"
#include <thread>

static void BM_ConcurrentLRUCache_Insert(benchmark::State& state) {
    utils::ConcurrentLRUCache<std::string, std::string> cache(1000);
    for (auto _ : state) {
        cache.put("key", "value");
    }
}

static void BM_ConcurrentLRUCache_Find(benchmark::State& state) {
    utils::ConcurrentLRUCache<std::string, std::string> cache(1000);
    cache.put("key", "value");
    for (auto _ : state) {
        benchmark::DoNotOptimize(cache.get("key"));
    }
}

static void BM_ConcurrentLRUCache_Remove(benchmark::State& state) {
    utils::ConcurrentLRUCache<std::string, std::string> cache(1000);
    cache.put("key", "value");
    for (auto _ : state) {
        cache.remove("key");
    }
}

static void BM_ConcurrentLRUCache_MultiThreaded(benchmark::State& state) {
    utils::ConcurrentLRUCache<std::string, std::string> cache(1000);
    std::vector<std::thread> threads;
    for (auto _ : state) {
        threads.clear();
        for (int i = 0; i < 4; ++i) { // Use 4 threads for mixed read/write
            threads.emplace_back([&cache, i]() {
                for (int j = 0; j < 100; ++j) {
                    cache.put("key" + std::to_string(i * 100 + j), "value");
                    benchmark::DoNotOptimize(cache.get("key" + std::to_string(i * 100 + j)));
                    cache.remove("key" + std::to_string(i * 100 + j));
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

BENCHMARK(BM_ConcurrentLRUCache_Insert);
BENCHMARK(BM_ConcurrentLRUCache_Find);
BENCHMARK(BM_ConcurrentLRUCache_Remove);
BENCHMARK(BM_ConcurrentLRUCache_MultiThreaded);

BENCHMARK_MAIN();
