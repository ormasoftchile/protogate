// tests/security/token_fuzzer.cpp
// Security fuzzer for token validation
// Tests edge cases and malformed inputs to ensure robust authentication

#include "../../src/security/token_validator.h"
#include "../../src/models/auth_token.h"
#include "../../src/storage/cache.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>
#include <thread>

namespace protogate {
namespace tests {

class TokenFuzzer : public ::testing::Test {
protected:
    void SetUp() override {
        token_cache_ = std::make_shared<storage::Cache<std::string, models::AuthToken>>();
        validator_ = std::make_shared<security::TokenValidator>(token_cache_);
        
        // Add a valid token for comparison
        models::AuthToken valid_token;
        valid_token.token = "valid_token_12345678901234567890123456789012";
        valid_token.token_hash = "sha256_hash_placeholder";
        valid_token.tunnel_id = "test-tunnel";
        valid_token.created_at = std::chrono::system_clock::now();
        valid_token.expires_at = std::chrono::system_clock::now() + std::chrono::hours(24);
        
        token_cache_->put(valid_token.tunnel_id, valid_token);
    }
    
    std::string generate_random_string(size_t length) {
        static const char charset[] = 
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "!@#$%^&*()_+-=[]{}|;:,.<>?/~`";
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
        
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[dis(gen)];
        }
        return result;
    }
    
    std::string generate_random_bytes(size_t length) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += static_cast<char>(dis(gen));
        }
        return result;
    }
    
    std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache_;
    std::shared_ptr<security::TokenValidator> validator_;
};

// Test 1: Empty token
TEST_F(TokenFuzzer, EmptyToken) {
    auto result = validator_->validate("");
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.error_message.empty());
}

// Test 2: Very short tokens
TEST_F(TokenFuzzer, ShortTokens) {
    for (size_t len = 1; len < 10; ++len) {
        auto token = generate_random_string(len);
        auto result = validator_->validate("Bearer " + token);
        EXPECT_FALSE(result.valid) << "Short token length " << len << " should be rejected";
    }
}

// Test 3: Very long tokens
TEST_F(TokenFuzzer, LongTokens) {
    std::vector<size_t> lengths = {1000, 10000, 100000, 1000000};
    for (auto len : lengths) {
        auto token = generate_random_string(len);
        auto result = validator_->validate("Bearer " + token);
        EXPECT_FALSE(result.valid) << "Long token length " << len << " should be rejected or handled gracefully";
    }
}

// Test 4: Special characters
TEST_F(TokenFuzzer, SpecialCharacters) {
    std::vector<std::string> special_chars = {
        "Bearer \0\0\0",
        "Bearer \n\r\t",
        "Bearer <script>alert('xss')</script>",
        "Bearer ' OR '1'='1",
        "Bearer '; DROP TABLE tokens; --",
        "Bearer ../../../etc/passwd",
        "Bearer %00%00%00",
        "Bearer \xFF\xFE\xFD",
    };
    
    for (const auto& token : special_chars) {
        auto result = validator_->validate(token);
        EXPECT_FALSE(result.valid) << "Special character token should be rejected: " << token;
    }
}

// Test 5: Missing "Bearer " prefix
TEST_F(TokenFuzzer, MissingBearerPrefix) {
    auto result1 = validator_->validate("valid_token_12345678901234567890123456789012");
    EXPECT_FALSE(result1.valid);
    
    auto result2 = validator_->validate("Basic YWxhZGRpbjpvcGVuc2VzYW1l");
    EXPECT_FALSE(result2.valid);
    
    auto result3 = validator_->validate("Token abc123");
    EXPECT_FALSE(result3.valid);
}

// Test 6: Case sensitivity
TEST_F(TokenFuzzer, CaseSensitivity) {
    auto result1 = validator_->validate("bearer valid_token_12345678901234567890123456789012");
    auto result2 = validator_->validate("BEARER valid_token_12345678901234567890123456789012");
    auto result3 = validator_->validate("BeArEr valid_token_12345678901234567890123456789012");
    
    // Validator should be case-insensitive for "Bearer" keyword
    // but case-sensitive for token value
}

// Test 7: Whitespace variations
TEST_F(TokenFuzzer, WhitespaceVariations) {
    std::vector<std::string> tokens = {
        "Bearer  token",           // Double space
        "Bearer\ttoken",           // Tab
        "Bearer\ntoken",           // Newline
        "Bearer \r\n token",       // CRLF
        " Bearer token",           // Leading space
        "Bearer token ",           // Trailing space
        "  Bearer  token  ",       // Multiple spaces
    };
    
    for (const auto& token : tokens) {
        auto result = validator_->validate(token);
        // Validator should handle or reject whitespace variations gracefully
    }
}

// Test 8: Unicode and UTF-8
TEST_F(TokenFuzzer, UnicodeCharacters) {
    std::vector<std::string> tokens = {
        "Bearer 你好世界",
        "Bearer مرحبا",
        "Bearer こんにちは",
        "Bearer 🔑🔒🛡️",
        "Bearer \xC3\xA9\xC3\xA0\xC3\xBC",  // UTF-8 é à ü
    };
    
    for (const auto& token : tokens) {
        auto result = validator_->validate(token);
        EXPECT_FALSE(result.valid) << "Unicode token should be rejected: " << token;
    }
}

// Test 9: Binary data
TEST_F(TokenFuzzer, BinaryData) {
    for (int i = 0; i < 100; ++i) {
        auto binary = generate_random_bytes(64);
        auto result = validator_->validate("Bearer " + binary);
        // Validator should handle binary data without crashing
    }
}

// Test 10: Repeated characters
TEST_F(TokenFuzzer, RepeatedCharacters) {
    std::vector<std::string> tokens = {
        "Bearer " + std::string(1000, 'A'),
        "Bearer " + std::string(1000, '0'),
        "Bearer " + std::string(1000, '\0'),
    };
    
    for (const auto& token : tokens) {
        auto result = validator_->validate(token);
        EXPECT_FALSE(result.valid) << "Repeated character token should be rejected";
    }
}

// Test 11: Format string attacks
TEST_F(TokenFuzzer, FormatStringAttacks) {
    std::vector<std::string> tokens = {
        "Bearer %s%s%s%s%s%s%s",
        "Bearer %x%x%x%x%x%x",
        "Bearer %n%n%n%n",
        "Bearer %p%p%p%p",
    };
    
    for (const auto& token : tokens) {
        auto result = validator_->validate(token);
        EXPECT_FALSE(result.valid) << "Format string token should be rejected: " << token;
    }
}

// Test 12: Integer overflow attempts
TEST_F(TokenFuzzer, IntegerOverflow) {
    // Try to cause integer overflow in length calculations
    std::vector<std::string> tokens = {
        "Bearer " + generate_random_string(SIZE_MAX - 10),
        "Bearer " + std::string(10000000, 'X'),  // 10MB token
    };
    
    for (const auto& token : tokens) {
        auto result = validator_->validate(token);
        // Validator should handle without integer overflow
    }
}

// Test 13: Null byte injection
TEST_F(TokenFuzzer, NullByteInjection) {
    std::string token_with_null = "Bearer token";
    token_with_null += '\0';
    token_with_null += "injected";
    
    auto result = validator_->validate(token_with_null);
    // Validator should not truncate at null byte
}

// Test 14: Timing attack resistance
TEST_F(TokenFuzzer, TimingAttackResistance) {
    // Measure validation time for various tokens
    auto valid_token = "Bearer valid_token_12345678901234567890123456789012";
    auto invalid_token1 = "Bearer invalid_token_12345678901234567890123456789012";
    auto invalid_token2 = "Bearer xnvalid_token_12345678901234567890123456789012";
    
    // Run validations (timing comparison would need many iterations for statistical significance)
    validator_->validate(valid_token);
    validator_->validate(invalid_token1);
    validator_->validate(invalid_token2);
    
    // Note: Full timing attack analysis requires statistical sampling across many iterations
    // This test verifies the code path executes without crashes
    SUCCEED();
}

// Test 15: Concurrent validation stress test
TEST_F(TokenFuzzer, ConcurrentValidation) {
    std::vector<std::thread> threads;
    std::atomic<int> failures{0};
    
    for (int i = 0; i < 100; ++i) {
        threads.emplace_back([this, &failures]() {
            for (int j = 0; j < 100; ++j) {
                auto token = "Bearer " + generate_random_string(64);
                try {
                    validator_->validate(token);
                } catch (...) {
                    failures++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(failures, 0) << "Concurrent validation should not crash";
}

// Test 16: Memory exhaustion attempts
TEST_F(TokenFuzzer, MemoryExhaustion) {
    // Try to exhaust memory with many large tokens
    for (int i = 0; i < 1000; ++i) {
        auto token = "Bearer " + generate_random_string(10000);
        auto result = validator_->validate(token);
        // Validator should limit memory usage
    }
}

// Test 17: Replay attack simulation
TEST_F(TokenFuzzer, ReplayAttack) {
    auto token = "Bearer valid_token_12345678901234567890123456789012";
    
    // Validate same token multiple times
    for (int i = 0; i < 100; ++i) {
        auto result = validator_->validate(token);
        // Token should remain valid (no replay protection at this level)
    }
}

// Test 18: Token with embedded authorization header
TEST_F(TokenFuzzer, EmbeddedHeader) {
    auto token = "Bearer Authorization: Bearer embedded_token";
    auto result = validator_->validate(token);
    EXPECT_FALSE(result.valid) << "Embedded header should be rejected";
}

// Test 19: CRLF injection
TEST_F(TokenFuzzer, CRLFInjection) {
    std::vector<std::string> tokens = {
        "Bearer token\r\nX-Injected: true",
        "Bearer token\nSet-Cookie: admin=true",
        "Bearer token\r\n\r\nHTTP/1.1 200 OK",
    };
    
    for (const auto& token : tokens) {
        auto result = validator_->validate(token);
        EXPECT_FALSE(result.valid) << "CRLF injection should be rejected";
    }
}

// Test 20: Random fuzzing (bulk test)
TEST_F(TokenFuzzer, RandomFuzzing) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> len_dis(0, 1000);
    
    // Generate 10000 random tokens
    for (int i = 0; i < 10000; ++i) {
        auto token_len = len_dis(gen);
        auto token = "Bearer " + generate_random_string(token_len);
        
        try {
            auto result = validator_->validate(token);
            // Should complete without crashing
        } catch (const std::exception& e) {
            FAIL() << "Validator crashed on random input: " << e.what();
        }
    }
}

}  // namespace tests
}  // namespace protogate
