#pragma once

#include <string>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace protogate {
namespace security {

/**
 * @brief Token bucket rate limiter
 * 
 * Implements token bucket algorithm for rate limiting:
 * - Tokens are added at a constant rate (requests per second)
 * - Each request consumes one token
 * - Bucket has maximum capacity (burst size)
 * - Requests are denied when bucket is empty
 * 
 * Thread-safe for concurrent access
 */
class RateLimiter {
public:
    /**
     * @brief Create rate limiter with specified rate and burst capacity
     * @param requests_per_second Maximum sustained request rate
     * @param burst_capacity Maximum burst size (tokens in bucket)
     */
    RateLimiter(double requests_per_second, size_t burst_capacity);
    
    // Prevent copying and moving (mutex is neither copyable nor movable)
    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;
    RateLimiter(RateLimiter&&) = delete;
    RateLimiter& operator=(RateLimiter&&) = delete;
    
    /**
     * @brief Check if request is allowed (consumes token if available)
     * @param key Rate limit key (e.g., tunnel_id, IP address)
     * @return true if request allowed, false if rate limit exceeded
     */
    bool allow_request(const std::string& key);
    
    /**
     * @brief Check if request would be allowed (without consuming token)
     * @param key Rate limit key
     * @return true if request would be allowed
     */
    bool check_limit(const std::string& key) const;
    
    /**
     * @brief Reset rate limit for specific key
     * @param key Rate limit key to reset
     */
    void reset(const std::string& key);
    
    /**
     * @brief Clear all rate limit state
     */
    void clear();
    
    /**
     * @brief Get current token count for key
     * @param key Rate limit key
     * @return Number of tokens available
     */
    double get_tokens(const std::string& key) const;
    
    /**
     * @brief Get requests per second limit
     */
    double get_rate() const { return requests_per_second_; }
    
    /**
     * @brief Get burst capacity
     */
    size_t get_burst_capacity() const { return burst_capacity_; }

private:
    /**
     * @brief Token bucket state for a single key
     */
    struct Bucket {
        double tokens;                                    // Current token count
        std::chrono::steady_clock::time_point last_refill; // Last refill time
        
        Bucket(double initial_tokens)
            : tokens(initial_tokens),
              last_refill(std::chrono::steady_clock::now()) {}
    };
    
    /**
     * @brief Refill tokens based on elapsed time
     * @param bucket Bucket to refill
     */
    void refill_tokens(Bucket& bucket) const;
    
    double requests_per_second_;
    size_t burst_capacity_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, Bucket> buckets_;
};

/**
 * @brief Per-tunnel rate limiter manager
 * 
 * Manages rate limiters for multiple tunnels with individual configurations
 */
class TunnelRateLimiter {
public:
    /**
     * @brief Create tunnel rate limiter with default limits
     * @param default_requests_per_second Default rate for new tunnels
     * @param default_burst_capacity Default burst size for new tunnels
     */
    TunnelRateLimiter(double default_requests_per_second = 100.0,
                      size_t default_burst_capacity = 200);
    
    /**
     * @brief Set rate limit for specific tunnel
     * @param tunnel_id Tunnel identifier
     * @param requests_per_second Maximum request rate
     * @param burst_capacity Maximum burst size
     */
    void set_tunnel_limit(const std::string& tunnel_id,
                         double requests_per_second,
                         size_t burst_capacity);
    
    /**
     * @brief Check if request is allowed for tunnel
     * @param tunnel_id Tunnel identifier
     * @return true if allowed, false if rate limit exceeded
     */
    bool allow_request(const std::string& tunnel_id);
    
    /**
     * @brief Remove tunnel from rate limiter
     * @param tunnel_id Tunnel identifier
     */
    void remove_tunnel(const std::string& tunnel_id);
    
    /**
     * @brief Clear all tunnel rate limiters
     */
    void clear();

private:
    double default_requests_per_second_;
    size_t default_burst_capacity_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RateLimiter> limiters_;
};

}  // namespace security
}  // namespace protogate
