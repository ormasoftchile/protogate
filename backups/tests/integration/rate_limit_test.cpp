#include <gtest/gtest.h>
#include "security/rate_limiter.h"
#include <thread>
#include <chrono>

using namespace protogate::security;

class RateLimitTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create rate limiter with default limits (100 rps, 200 burst)
        limiter_ = std::make_unique<TunnelRateLimiter>();
    }

    std::unique_ptr<TunnelRateLimiter> limiter_;
};

// ============================================================================
// Token Bucket Algorithm Tests
// ============================================================================

TEST_F(RateLimitTest, AllowRequestsWithinLimit) {
    std::string tunnel_id = "tunnel_test_1";
    
    // Configure: 10 requests per second, 10 burst
    limiter_->set_tunnel_limit(tunnel_id, 10.0, 10);
    
    // Should allow burst capacity (10 requests)
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id)) 
            << "Request " << i << " should be allowed";
    }
}

TEST_F(RateLimitTest, BlockRequestsExceedingBurst) {
    std::string tunnel_id = "tunnel_test_2";
    
    // Configure: 10 requests per second, 10 burst
    limiter_->set_tunnel_limit(tunnel_id, 10.0, 10);
    
    // Allow burst capacity (10 requests)
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    
    // 11th request should be blocked
    EXPECT_FALSE(limiter_->allow_request(tunnel_id))
        << "Request exceeding burst should be blocked";
}

TEST_F(RateLimitTest, TokenRefillOverTime) {
    std::string tunnel_id = "tunnel_test_3";
    
    // Configure: 10 requests per second, 10 burst
    limiter_->set_tunnel_limit(tunnel_id, 10.0, 10);
    
    // Consume all tokens (10 requests)
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    
    // Next request should be blocked
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Wait 100ms (should refill 1 token at 10 rps)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should allow 1 more request after refill
    EXPECT_TRUE(limiter_->allow_request(tunnel_id))
        << "Request should be allowed after token refill";
    
    // But next immediate request should be blocked again
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
}

TEST_F(RateLimitTest, TokenRefillCappedAtBurst) {
    std::string tunnel_id = "tunnel_test_4";
    
    // Configure: 100 requests per second, 50 burst
    limiter_->set_tunnel_limit(tunnel_id, 100.0, 50);
    
    // Consume all tokens
    for (int i = 0; i < 50; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    
    // Wait 1 second (would refill 100 tokens, but capped at burst of 50)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Should allow exactly 50 requests (burst capacity)
    for (int i = 0; i < 50; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id))
            << "Request " << i << " should be allowed after full refill";
    }
    
    // 51st should be blocked
    EXPECT_FALSE(limiter_->allow_request(tunnel_id))
        << "Tokens should be capped at burst capacity";
}

// ============================================================================
// Per-Tunnel Rate Limits
// ============================================================================

TEST_F(RateLimitTest, IndependentTunnelLimits) {
    std::string tunnel_a = "tunnel_a";
    std::string tunnel_b = "tunnel_b";
    
    // Configure different limits
    limiter_->set_tunnel_limit(tunnel_a, 10.0, 5);   // 5 burst
    limiter_->set_tunnel_limit(tunnel_b, 20.0, 10);  // 10 burst
    
    // Tunnel A: allow 5, block 6th
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_a));
    }
    EXPECT_FALSE(limiter_->allow_request(tunnel_a));
    
    // Tunnel B: should still allow 10 (independent limits)
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_b))
            << "Tunnel B request " << i << " should be allowed";
    }
    EXPECT_FALSE(limiter_->allow_request(tunnel_b));
}

TEST_F(RateLimitTest, DefaultLimitsForNewTunnels) {
    std::string tunnel_id = "new_tunnel";
    
    // Default: 100 rps, 200 burst
    // Should allow 200 requests initially
    for (int i = 0; i < 200; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id))
            << "Default limit should allow " << i << " requests";
    }
    
    // 201st should be blocked
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
}

TEST_F(RateLimitTest, UpdateTunnelLimitDynamically) {
    std::string tunnel_id = "dynamic_tunnel";
    
    // Start with strict limit
    limiter_->set_tunnel_limit(tunnel_id, 5.0, 3);
    
    // Allow 3, block 4th
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Update to more permissive limit
    limiter_->set_tunnel_limit(tunnel_id, 10.0, 10);
    
    // Should now allow 10 requests (fresh bucket)
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id))
            << "Updated limit should allow " << i << " requests";
    }
}

// ============================================================================
// Sustained Rate Tests
// ============================================================================

TEST_F(RateLimitTest, SustainedRateOverTime) {
    std::string tunnel_id = "sustained_test";
    
    // Configure: 10 requests per second, 5 burst
    limiter_->set_tunnel_limit(tunnel_id, 10.0, 5);
    
    // Consume burst
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Over 500ms, should be able to make ~5 more requests (10 rps sustained)
    int allowed = 0;
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
        if (limiter_->allow_request(tunnel_id)) {
            allowed++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Should allow roughly 5 requests over 500ms at 10 rps
    EXPECT_GE(allowed, 4) << "Should allow at least 4 requests in 500ms";
    EXPECT_LE(allowed, 6) << "Should not allow more than 6 requests in 500ms";
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

TEST_F(RateLimitTest, VeryLowRate) {
    std::string tunnel_id = "low_rate";
    
    // Configure: 1 request per second, 5 burst
    limiter_->set_tunnel_limit(tunnel_id, 1.0, 5);
    
    // Should allow burst capacity
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    
    // Next should be blocked
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Wait 100ms (not enough for 1 rps refill)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Wait 1 full second for refill
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    EXPECT_TRUE(limiter_->allow_request(tunnel_id))
        << "Should allow 1 request after 1 second at 1 rps";
}

TEST_F(RateLimitTest, HighRate) {
    std::string tunnel_id = "high_rate";
    
    // Configure: 500 requests per second, 200 burst
    limiter_->set_tunnel_limit(tunnel_id, 500.0, 200);
    
    // Should allow burst capacity
    for (int i = 0; i < 200; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    
    // Next request should be blocked
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Wait 10ms - should refill ~5 tokens at 500 rps
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Should allow a few more requests
    int allowed = 0;
    for (int i = 0; i < 10; i++) {
        if (limiter_->allow_request(tunnel_id)) {
            allowed++;
        }
    }
    EXPECT_GE(allowed, 3) << "Should allow at least 3 requests after refill";
    EXPECT_LE(allowed, 7) << "Should not allow more than 7 requests";
}

TEST_F(RateLimitTest, ConcurrentAccess) {
    std::string tunnel_id = "concurrent_test";
    
    // Configure: 100 requests per second, 100 burst
    limiter_->set_tunnel_limit(tunnel_id, 100.0, 100);
    
    std::atomic<int> allowed{0};
    std::atomic<int> blocked{0};
    
    // Spawn multiple threads trying to make requests
    std::vector<std::thread> threads;
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([this, &tunnel_id, &allowed, &blocked]() {
            for (int i = 0; i < 20; i++) {
                if (limiter_->allow_request(tunnel_id)) {
                    allowed++;
                } else {
                    blocked++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Total requests: 10 threads * 20 requests = 200
    // Should allow 100 (burst), block 100
    EXPECT_EQ(allowed + blocked, 200);
    EXPECT_EQ(allowed, 100) << "Should allow exactly burst capacity";
    EXPECT_EQ(blocked, 100) << "Should block excess requests";
}

TEST_F(RateLimitTest, RemoveTunnel) {
    std::string tunnel_id = "remove_test";
    
    limiter_->set_tunnel_limit(tunnel_id, 10.0, 5);
    
    // Consume all tokens
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
    EXPECT_FALSE(limiter_->allow_request(tunnel_id));
    
    // Remove tunnel
    limiter_->remove_tunnel(tunnel_id);
    
    // Should get fresh limiter with default limits
    for (int i = 0; i < 200; i++) {
        EXPECT_TRUE(limiter_->allow_request(tunnel_id));
    }
}

// ============================================================================
// RateLimiter (single key) Tests
// ============================================================================

TEST_F(RateLimitTest, SingleRateLimiterMultipleKeys) {
    RateLimiter limiter(10.0, 10);  // 10 rps, 10 burst
    
    std::string key_a = "key_a";
    std::string key_b = "key_b";
    
    // Each key gets independent token bucket
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(limiter.allow_request(key_a));
        EXPECT_TRUE(limiter.allow_request(key_b));
    }
    
    // Both should be exhausted
    EXPECT_FALSE(limiter.allow_request(key_a));
    EXPECT_FALSE(limiter.allow_request(key_b));
}

TEST_F(RateLimitTest, CheckLimitWithoutConsuming) {
    RateLimiter limiter(10.0, 5);
    std::string key = "check_key";
    
    // Check without consuming
    EXPECT_TRUE(limiter.check_limit(key));
    
    // Should still allow 5 requests
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter.allow_request(key));
    }
    
    // Now check should return false
    EXPECT_FALSE(limiter.check_limit(key));
    EXPECT_FALSE(limiter.allow_request(key));
}

TEST_F(RateLimitTest, GetTokensCount) {
    RateLimiter limiter(10.0, 10);
    std::string key = "tokens_key";
    
    // Initially should have full burst capacity
    EXPECT_DOUBLE_EQ(limiter.get_tokens(key), 10.0);
    
    // Consume 3 tokens
    for (int i = 0; i < 3; i++) {
        limiter.allow_request(key);
    }
    
    // Should have approximately 7 tokens left (accounting for elapsed time refill)
    double tokens = limiter.get_tokens(key);
    EXPECT_GE(tokens, 6.9) << "Should have at least 6.9 tokens after consuming 3";
    EXPECT_LE(tokens, 7.1) << "Should have at most 7.1 tokens (with minimal refill)";
}

TEST_F(RateLimitTest, ResetKey) {
    RateLimiter limiter(10.0, 5);
    std::string key = "reset_key";
    
    // Consume all tokens
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter.allow_request(key));
    }
    EXPECT_FALSE(limiter.allow_request(key));
    
    // Reset
    limiter.reset(key);
    
    // Should have full capacity again
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(limiter.allow_request(key));
    }
}

TEST_F(RateLimitTest, ClearAllKeys) {
    RateLimiter limiter(10.0, 5);
    
    // Create multiple exhausted buckets
    for (int k = 0; k < 5; k++) {
        std::string key = "key_" + std::to_string(k);
        for (int i = 0; i < 5; i++) {
            limiter.allow_request(key);
        }
        EXPECT_FALSE(limiter.allow_request(key));
    }
    
    // Clear all
    limiter.clear();
    
    // All keys should have fresh buckets
    for (int k = 0; k < 5; k++) {
        std::string key = "key_" + std::to_string(k);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(limiter.allow_request(key));
        }
    }
}
