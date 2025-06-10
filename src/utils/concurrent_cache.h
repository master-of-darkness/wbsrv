#pragma once

#include <unordered_map>
#include <list>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <mutex>

namespace utils {
    template<typename Key, typename Value, size_t SHARD_COUNT = 16>
    class ConcurrentLRUCache {
    public:
        explicit ConcurrentLRUCache(size_t capacity) : capacity_(capacity / SHARD_COUNT) {
            if (capacity_ == 0) capacity_ = 1; // Avoid division by zero
        }

        // Retrieve a value from the cache (thread-safe read with shared lock)
        std::optional<Value> get(const Key &key) {
            size_t shard = getShard(key);
            std::shared_lock lock(shards_[shard].mutex);
            auto it = shards_[shard].map.find(key);
            if (it == shards_[shard].map.end()) {
                return std::nullopt;
            }
            // Move accessed item to front (most recently used)
            shards_[shard].items.splice(shards_[shard].items.begin(), shards_[shard].items, it->second);
            return it->second->second;
        }

        // Insert or update a key-value pair (thread-safe write with exclusive lock)
        void put(const Key &key, const Value &value) {
            size_t shard = getShard(key);
            std::unique_lock lock(shards_[shard].mutex);

            auto it = shards_[shard].map.find(key);
            if (it != shards_[shard].map.end()) {
                it->second->second = value;
                shards_[shard].items.splice(shards_[shard].items.begin(), shards_[shard].items, it->second);
                return;
            }

            // Insert new key-value pair
            shards_[shard].items.emplace_front(key, value);
            shards_[shard].map[key] = shards_[shard].items.begin();

            // Eviction if capacity exceeded
            if (shards_[shard].map.size() > capacity_) {
                auto last = shards_[shard].items.end();
                --last;
                shards_[shard].map.erase(last->first);
                shards_[shard].items.pop_back();
            }
        }

        // Remove a key from the cache (thread-safe delete)
        void remove(const Key &key) {
            size_t shard = getShard(key);
            std::unique_lock lock(shards_[shard].mutex);
            auto it = shards_[shard].map.find(key);
            if (it != shards_[shard].map.end()) {
                shards_[shard].items.erase(it->second);
                shards_[shard].map.erase(it);
            }
        }

        int size() {
            return shards_.size();
        }

    private:
        struct LRUShard {
            std::list<std::pair<Key, Value> > items; // Doubly linked list for LRU order
            std::unordered_map<Key, typename std::list<std::pair<Key, Value> >::iterator> map;
            mutable std::shared_mutex mutex; // Read-Write Lock
        };

        size_t capacity_;
        std::vector<LRUShard> shards_ = std::vector<LRUShard>(SHARD_COUNT);

        size_t getShard(const Key &key) const {
            return std::hash<Key>{}(key) % SHARD_COUNT;
        }
    };
}
