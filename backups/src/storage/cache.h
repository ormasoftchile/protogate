#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>

namespace protogate {
namespace storage {

/**
 * @brief Thread-safe in-memory cache with TTL support
 * 
 * Uses std::shared_mutex for read-write locking (C++17)
 */
template <typename Key, typename Value>
class Cache {
public:
    /**
     * @brief Cache entry with expiration
     */
    struct Entry {
        Value value;
        std::chrono::steady_clock::time_point expires_at;
        
        bool is_expired() const {
            return std::chrono::steady_clock::now() >= expires_at;
        }
    };
    
    explicit Cache(std::chrono::seconds default_ttl = std::chrono::seconds(3600))
        : default_ttl_(default_ttl) {}
    
    /**
     * @brief Insert or update a value in the cache
     */
    void put(const Key& key, const Value& value) {
        put(key, value, default_ttl_);
    }
    
    /**
     * @brief Insert or update a value with custom TTL
     */
    void put(const Key& key, const Value& value, std::chrono::seconds ttl) {
        std::unique_lock lock(mutex_);
        auto expires_at = std::chrono::steady_clock::now() + ttl;
        cache_[key] = Entry{value, expires_at};
    }
    
    /**
     * @brief Get a value from the cache
     * @return std::nullopt if key not found or expired
     */
    std::optional<Value> get(const Key& key) const {
        std::shared_lock lock(mutex_);
        auto it = cache_.find(key);
        
        if (it == cache_.end()) {
            return std::nullopt;
        }
        
        if (it->second.is_expired()) {
            // Note: We don't remove expired entries here to avoid upgrade to unique_lock
            // They'll be removed by cleanup() or overwritten by put()
            return std::nullopt;
        }
        
        return it->second.value;
    }
    
    /**
     * @brief Remove a value from the cache
     */
    bool remove(const Key& key) {
        std::unique_lock lock(mutex_);
        return cache_.erase(key) > 0;
    }
    
    /**
     * @brief Check if key exists and is not expired
     */
    bool contains(const Key& key) const {
        std::shared_lock lock(mutex_);
        auto it = cache_.find(key);
        return it != cache_.end() && !it->second.is_expired();
    }
    
    /**
     * @brief Get number of entries in cache (including expired)
     */
    size_t size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }
    
    /**
     * @brief Clear all entries
     */
    void clear() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }
    
    /**
     * @brief Remove expired entries
     * @return Number of entries removed
     */
    size_t cleanup() {
        std::unique_lock lock(mutex_);
        size_t removed = 0;
        
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.is_expired()) {
                it = cache_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        
        return removed;
    }
    
    /**
     * @brief Apply a function to all non-expired entries
     */
    void for_each(std::function<void(const Key&, const Value&)> func) const {
        std::shared_lock lock(mutex_);
        for (const auto& [key, entry] : cache_) {
            if (!entry.is_expired()) {
                func(key, entry.value);
            }
        }
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<Key, Entry> cache_;
    std::chrono::seconds default_ttl_;
};

}  // namespace storage
}  // namespace protogate
