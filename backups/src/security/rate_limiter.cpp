#include "rate_limiter.h"
#include "../observability/logger.h"
#include <algorithm>

namespace protogate {
namespace security {

RateLimiter::RateLimiter(double requests_per_second, size_t burst_capacity)
    : requests_per_second_(requests_per_second),
      burst_capacity_(burst_capacity) {
    
    if (requests_per_second <= 0) {
        observability::Logger::instance().warning("Invalid rate limit, using default", {
            {"requests_per_second", std::to_string(requests_per_second)}
        });
        requests_per_second_ = 100.0;
    }
    
    if (burst_capacity == 0) {
        observability::Logger::instance().warning("Invalid burst capacity, using default", {
            {"burst_capacity", std::to_string(burst_capacity)}
        });
        burst_capacity_ = 200;
    }
}

bool RateLimiter::allow_request(const std::string& key) {
    std::lock_guard lock(mutex_);
    
    // Get or create bucket for this key
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        // New key - create bucket with full capacity
        it = buckets_.emplace(key, Bucket(static_cast<double>(burst_capacity_))).first;
    }
    
    // Refill tokens based on elapsed time
    refill_tokens(it->second);
    
    // Check if token available
    if (it->second.tokens >= 1.0) {
        it->second.tokens -= 1.0;
        return true;
    }
    
    // Rate limit exceeded
    observability::Logger::instance().debug("Rate limit exceeded", {
        {"key", key},
        {"tokens", std::to_string(it->second.tokens)},
        {"rate", std::to_string(requests_per_second_)}
    });
    
    return false;
}

bool RateLimiter::check_limit(const std::string& key) const {
    std::lock_guard lock(mutex_);
    
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        // New key would have full capacity
        return true;
    }
    
    // Create temporary bucket to check without modifying state
    Bucket temp_bucket = it->second;
    refill_tokens(temp_bucket);
    
    return temp_bucket.tokens >= 1.0;
}

void RateLimiter::reset(const std::string& key) {
    std::lock_guard lock(mutex_);
    buckets_.erase(key);
}

void RateLimiter::clear() {
    std::lock_guard lock(mutex_);
    buckets_.clear();
}

double RateLimiter::get_tokens(const std::string& key) const {
    std::lock_guard lock(mutex_);
    
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        return static_cast<double>(burst_capacity_);
    }
    
    // Create temporary bucket to check without modifying state
    Bucket temp_bucket = it->second;
    refill_tokens(temp_bucket);
    
    return temp_bucket.tokens;
}

void RateLimiter::refill_tokens(Bucket& bucket) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - bucket.last_refill);
    
    // Calculate tokens to add based on elapsed time
    double tokens_to_add = (elapsed.count() / 1000000.0) * requests_per_second_;
    
    if (tokens_to_add > 0) {
        bucket.tokens = std::min(
            bucket.tokens + tokens_to_add,
            static_cast<double>(burst_capacity_)
        );
        bucket.last_refill = now;
    }
}

// TunnelRateLimiter implementation

TunnelRateLimiter::TunnelRateLimiter(double default_requests_per_second,
                                     size_t default_burst_capacity)
    : default_requests_per_second_(default_requests_per_second),
      default_burst_capacity_(default_burst_capacity) {
    
    observability::Logger::instance().info("TunnelRateLimiter initialized", {
        {"default_rps", std::to_string(default_requests_per_second)},
        {"default_burst", std::to_string(default_burst_capacity)}
    });
}

void TunnelRateLimiter::set_tunnel_limit(const std::string& tunnel_id,
                                         double requests_per_second,
                                         size_t burst_capacity) {
    std::lock_guard lock(mutex_);
    
    // Remove existing limiter if present
    limiters_.erase(tunnel_id);
    
    // Create new limiter with specified limits using piecewise construction
    limiters_.emplace(std::piecewise_construct,
                      std::forward_as_tuple(tunnel_id),
                      std::forward_as_tuple(requests_per_second, burst_capacity));
    
    observability::Logger::instance().info("Tunnel rate limit configured", {
        {"tunnel_id", tunnel_id},
        {"requests_per_second", std::to_string(requests_per_second)},
        {"burst_capacity", std::to_string(burst_capacity)}
    });
}

bool TunnelRateLimiter::allow_request(const std::string& tunnel_id) {
    std::lock_guard lock(mutex_);
    
    // Get or create limiter for this tunnel
    auto it = limiters_.find(tunnel_id);
    if (it == limiters_.end()) {
        // Create new limiter with default settings using piecewise construction
        it = limiters_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(tunnel_id),
            std::forward_as_tuple(default_requests_per_second_, default_burst_capacity_)
        ).first;
        
        observability::Logger::instance().debug("Created rate limiter for tunnel", {
            {"tunnel_id", tunnel_id},
            {"requests_per_second", std::to_string(default_requests_per_second_)},
            {"burst_capacity", std::to_string(default_burst_capacity_)}
        });
    }
    
    // Check rate limit using tunnel_id as key
    return it->second.allow_request(tunnel_id);
}

void TunnelRateLimiter::remove_tunnel(const std::string& tunnel_id) {
    std::lock_guard lock(mutex_);
    limiters_.erase(tunnel_id);
    
    observability::Logger::instance().debug("Removed rate limiter for tunnel", {
        {"tunnel_id", tunnel_id}
    });
}

void TunnelRateLimiter::clear() {
    std::lock_guard lock(mutex_);
    limiters_.clear();
    
    observability::Logger::instance().info("Cleared all tunnel rate limiters");
}

}  // namespace security
}  // namespace protogate
