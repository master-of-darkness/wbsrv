#pragma once

#include <list>
#include <optional>
#include <utility>
#include <cstddef>
#include <algorithm>
#include <xxhash.h> // xxh3_64bits
#include <folly/container/F14Map.h>

namespace Cache {
    template<typename Key>
    struct xxh3_hash {
        std::size_t operator()(const Key &key) const {
            return XXH3_64bits(&key, sizeof(Key));
        }
    };

    template<>
    struct xxh3_hash<std::string> {
        std::size_t operator()(const std::string &key) const {
            return XXH3_64bits(key.data(), key.size());
        }
    };

    template<typename Key, typename Value, typename Hasher = xxh3_hash<Key> >
    class ARC {
    public:
        using key_value_pair_t = std::pair<Key, Value>;
        using list_iterator_t = typename std::list<key_value_pair_t>::iterator;
        using key_list_iterator_t = typename std::list<Key>::iterator;

        enum class ListType { T1, T2, B1, B2 };

        struct CacheEntry {
            ListType list_type;
            list_iterator_t data_iter; // Valid for T1, T2
            key_list_iterator_t key_iter; // Valid for B1, B2

            CacheEntry() : list_type(ListType::T1) {
            }

            CacheEntry(ListType type, list_iterator_t iter)
                : list_type(type), data_iter(iter) {
            }

            CacheEntry(ListType type, key_list_iterator_t iter)
                : list_type(type), key_iter(iter) {
            }

            // Copy and move constructors
            CacheEntry(const CacheEntry &) = default;

            CacheEntry(CacheEntry &&) = default;

            CacheEntry &operator=(const CacheEntry &) = default;

            CacheEntry &operator=(CacheEntry &&) = default;
        };

        explicit ARC(std::size_t max_size) noexcept
            : max_size_(max_size), p_(0) {
        }

        // Move-only
        ARC(const ARC &) = delete;

        ARC &operator=(const ARC &) = delete;

        ARC(ARC &&) noexcept = default;

        ARC &operator=(ARC &&) noexcept = default;

        void put(const Key &key, const Value &value) {
            auto it = map_.find(key);

            if (it != map_.end()) {
                // Key exists in one of the lists
                if (it->second.list_type == ListType::T1) {
                    // Move from T1 to T2 (promote to frequent)
                    Value old_value = std::move(it->second.data_iter->second);
                    t1_.erase(it->second.data_iter);
                    t2_.emplace_front(key, value);
                    it->second = CacheEntry(ListType::T2, t2_.begin());
                } else if (it->second.list_type == ListType::T2) {
                    // Update value and move to front of T2
                    it->second.data_iter->second = value;
                    t2_.splice(t2_.begin(), t2_, it->second.data_iter);
                }
                return;
            }

            // Key not in cache, check ghost lists
            it = map_.find(key);
            if (it != map_.end()) {
                if (it->second.list_type == ListType::B1) {
                    // Cache hit in B1 - increase p
                    std::size_t delta = std::max(static_cast<std::size_t>(1), b2_.size() / b1_.size());
                    p_ = std::min(max_size_, p_ + delta);

                    // Remove from B1
                    b1_.erase(it->second.key_iter);

                    // Add to T2
                    t2_.emplace_front(key, value);
                    it->second = CacheEntry(ListType::T2, t2_.begin());

                    replace();
                    return;
                } else if (it->second.list_type == ListType::B2) {
                    // Cache hit in B2 - decrease p
                    std::size_t delta = std::max(static_cast<std::size_t>(1), b1_.size() / b2_.size());
                    p_ = (p_ > delta) ? p_ - delta : 0;

                    // Remove from B2
                    b2_.erase(it->second.key_iter);

                    // Add to T2
                    t2_.emplace_front(key, value);
                    it->second = CacheEntry(ListType::T2, t2_.begin());

                    replace();
                    return;
                }
            }

            // Cache miss - add to T1
            t1_.emplace_front(key, value);
            map_.emplace(key, CacheEntry(ListType::T1, t1_.begin()));
            replace();
        }

        void put(const Key &key, Value &&value) {
            auto it = map_.find(key);

            if (it != map_.end()) {
                // Key exists in one of the lists
                if (it->second.list_type == ListType::T1) {
                    // Move from T1 to T2 (promote to frequent)
                    t1_.erase(it->second.data_iter);
                    t2_.emplace_front(key, std::move(value));
                    it->second = CacheEntry(ListType::T2, t2_.begin());
                } else if (it->second.list_type == ListType::T2) {
                    // Update value and move to front of T2
                    it->second.data_iter->second = std::move(value);
                    t2_.splice(t2_.begin(), t2_, it->second.data_iter);
                }
                return;
            }

            // Key not in cache, check ghost lists
            it = map_.find(key);
            if (it != map_.end()) {
                if (it->second.list_type == ListType::B1) {
                    // Cache hit in B1 - increase p
                    std::size_t delta = std::max(static_cast<std::size_t>(1), b2_.size() / b1_.size());
                    p_ = std::min(max_size_, p_ + delta);

                    // Remove from B1
                    b1_.erase(it->second.key_iter);

                    // Add to T2
                    t2_.emplace_front(key, std::move(value));
                    it->second = CacheEntry(ListType::T2, t2_.begin());

                    replace();
                    return;
                } else if (it->second.list_type == ListType::B2) {
                    // Cache hit in B2 - decrease p
                    std::size_t delta = std::max(static_cast<std::size_t>(1), b1_.size() / b2_.size());
                    p_ = (p_ > delta) ? p_ - delta : 0;

                    // Remove from B2
                    b2_.erase(it->second.key_iter);

                    // Add to T2
                    t2_.emplace_front(key, std::move(value));
                    it->second = CacheEntry(ListType::T2, t2_.begin());

                    replace();
                    return;
                }
            }

            // Cache miss - add to T1
            t1_.emplace_front(key, std::move(value));
            map_.emplace(key, CacheEntry(ListType::T1, t1_.begin()));
            replace();
        }

        template<typename V>
        void emplace(const Key &key, V &&value) {
            put(key, std::forward<V>(value));
        }

        std::optional<Value> get(const Key &key) {
            auto it = map_.find(key);
            if (it == map_.end()) return std::nullopt;

            if (it->second.list_type == ListType::T1) {
                // Move from T1 to T2 (promote to frequent)
                Value value = it->second.data_iter->second;
                t1_.erase(it->second.data_iter);
                t2_.emplace_front(key, std::move(value));
                it->second = CacheEntry(ListType::T2, t2_.begin());
                return t2_.begin()->second;
            } else if (it->second.list_type == ListType::T2) {
                // Move to front of T2
                t2_.splice(t2_.begin(), t2_, it->second.data_iter);
                return it->second.data_iter->second;
            }

            return std::nullopt; // Key in ghost lists
        }

        std::optional<Value> peek(const Key &key) const {
            auto it = map_.find(key);
            if (it == map_.end()) return std::nullopt;

            if (it->second.list_type == ListType::T1) {
                return it->second.data_iter->second;
            } else if (it->second.list_type == ListType::T2) {
                return it->second.data_iter->second;
            }

            return std::nullopt; // Key in ghost lists
        }

        bool contains(const Key &key) const noexcept {
            auto it = map_.find(key);
            if (it == map_.end()) return false;
            return it->second.list_type == ListType::T1 || it->second.list_type == ListType::T2;
        }

        bool remove(const Key &key) noexcept {
            auto it = map_.find(key);
            if (it == map_.end()) return false;

            switch (it->second.list_type) {
                case ListType::T1:
                    t1_.erase(it->second.data_iter);
                    break;
                case ListType::T2:
                    t2_.erase(it->second.data_iter);
                    break;
                case ListType::B1:
                    b1_.erase(it->second.key_iter);
                    break;
                case ListType::B2:
                    b2_.erase(it->second.key_iter);
                    break;
            }

            map_.erase(it);
            return true;
        }

        void clear() noexcept {
            map_.clear();
            t1_.clear();
            t2_.clear();
            b1_.clear();
            b2_.clear();
            p_ = 0;
        }

        std::size_t size() const noexcept { return t1_.size() + t2_.size(); }
        std::size_t max_size() const noexcept { return max_size_; }
        bool empty() const noexcept { return t1_.empty() && t2_.empty(); }
        bool full() const noexcept { return size() >= max_size_; }

        // ARC-specific methods
        std::size_t t1_size() const noexcept { return t1_.size(); }
        std::size_t t2_size() const noexcept { return t2_.size(); }
        std::size_t b1_size() const noexcept { return b1_.size(); }
        std::size_t b2_size() const noexcept { return b2_.size(); }
        std::size_t p() const noexcept { return p_; }

    private:
        void replace() noexcept {
            if (t1_.size() + t2_.size() >= max_size_) {
                if (t1_.size() > 0 && ((t1_.size() > p_) || (t2_.empty()))) {
                    // Remove LRU from T1 and add to B1
                    auto last = std::prev(t1_.end());
                    Key key = last->first;
                    t1_.erase(last);

                    b1_.emplace_front(key);
                    map_.emplace(key, CacheEntry(ListType::B1, b1_.begin()));
                } else if (t2_.size() > 0) {
                    // Remove LRU from T2 and add to B2
                    auto last = std::prev(t2_.end());
                    Key key = last->first;
                    t2_.erase(last);

                    b2_.emplace_front(key);
                    map_.emplace(key, CacheEntry(ListType::B2, b2_.begin()));
                }
            }

            // Maintain ghost list sizes
            if (b1_.size() > max_size_) {
                auto last = std::prev(b1_.end());
                map_.erase(*last);
                b1_.pop_back();
            }

            if (b2_.size() > max_size_) {
                auto last = std::prev(b2_.end());
                map_.erase(*last);
                b2_.pop_back();
            }
        }

        std::list<key_value_pair_t> t1_; // Recent items (first time access)
        std::list<key_value_pair_t> t2_; // Frequent items (multiple access)
        std::list<Key> b1_; // Ghost list for evicted T1 items
        std::list<Key> b2_; // Ghost list for evicted T2 items

        folly::F14FastMap<Key, CacheEntry, Hasher> map_;
        std::size_t max_size_;
        std::size_t p_; // Target size for T1
    };

    struct VirtualHostConfig {
        std::string web_root_directory;
        std::vector<std::string> index_page_files;

        VirtualHostConfig() = default;

        VirtualHostConfig(std::string web_root, std::vector<std::string> index_files)
            : web_root_directory(std::move(web_root))
              , index_page_files(std::move(index_files)) {
        }
    };

    struct FileSystemMetadata {
        bool is_directory;
        // TODO: Add more metadata fields as needed
    };

    extern ARC<std::string, VirtualHostConfig> host_config_cache;
    extern ARC<std::string, FileSystemMetadata> file_metadata_cache;
} // namespace Cache
