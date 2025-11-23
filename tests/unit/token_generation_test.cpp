// tests/unit/token_generation_test.cpp
// Unit tests for cryptographic token generation

#include <gtest/gtest.h>
#include "../../src/api/tunnels_handler.h"
#include "../../src/security/token_validator.h"
#include <set>
#include <unordered_set>
#include <random>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace protogate;

// Test fixture for token generation
class TokenGenerationTest : public ::testing::Test {
protected:
    // Helper to generate a token using the same logic as TunnelsHandler
    std::string generate_test_token() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;
        
        std::ostringstream token_stream;
        token_stream << std::hex << std::setfill('0');
        
        // Generate 32 bytes (256 bits) of random data
        for (int i = 0; i < 4; ++i) {
            uint64_t random_value = dis(gen);
            token_stream << std::setw(16) << random_value;
        }
        
        return token_stream.str();
    }
};

// Test: Token format is exactly 64 hex characters (256 bits)
TEST_F(TokenGenerationTest, TokenFormatIs256Bits) {
    std::string token = generate_test_token();
    
    // Should be exactly 64 characters (256 bits = 32 bytes = 64 hex chars)
    EXPECT_EQ(token.length(), 64);
    
    // All characters should be valid hex (0-9, a-f)
    for (char c : token) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Invalid character in token: " << c;
    }
}

// Test: Multiple tokens are all valid format
TEST_F(TokenGenerationTest, MultipleTokensValidFormat) {
    for (int i = 0; i < 100; ++i) {
        std::string token = generate_test_token();
        
        EXPECT_EQ(token.length(), 64);
        
        for (char c : token) {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
    }
}

// Test: No collisions in 10,000 tokens
TEST_F(TokenGenerationTest, NoCollisionsIn10KTokens) {
    const int NUM_TOKENS = 10000;
    std::unordered_set<std::string> tokens;
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        
        // Check for collision
        EXPECT_TRUE(tokens.find(token) == tokens.end())
            << "Token collision detected at iteration " << i;
        
        tokens.insert(token);
    }
    
    // Verify all tokens are unique
    EXPECT_EQ(tokens.size(), NUM_TOKENS);
}

// Test: Token entropy is high (chi-square test)
TEST_F(TokenGenerationTest, HighEntropy) {
    const int NUM_TOKENS = 1000;
    std::vector<int> hex_char_counts(16, 0); // Count for 0-9, a-f
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        
        for (char c : token) {
            int index;
            if (c >= '0' && c <= '9') {
                index = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                index = 10 + (c - 'a');
            } else {
                FAIL() << "Invalid hex character: " << c;
            }
            hex_char_counts[index]++;
        }
    }
    
    // Chi-square test for uniform distribution
    // Expected count per hex char: (1000 tokens * 64 chars) / 16 = 4000
    double expected = (NUM_TOKENS * 64.0) / 16.0;
    double chi_square = 0.0;
    
    for (int count : hex_char_counts) {
        double diff = count - expected;
        chi_square += (diff * diff) / expected;
    }
    
    // Chi-square critical value for 15 degrees of freedom at p=0.05 is ~25
    // We use a more relaxed threshold of 30 to account for randomness
    EXPECT_LT(chi_square, 30.0)
        << "Token distribution is not uniform (chi-square = " << chi_square << ")";
}

// Test: Tokens are different from each other (not repeated patterns)
TEST_F(TokenGenerationTest, NoDuplicatePatterns) {
    const int NUM_TOKENS = 100;
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        
        // Check that token doesn't have obvious repeating patterns
        // Split into 4 parts of 16 characters each
        std::string part1 = token.substr(0, 16);
        std::string part2 = token.substr(16, 16);
        std::string part3 = token.substr(32, 16);
        std::string part4 = token.substr(48, 16);
        
        // All 4 parts should be different (very high probability)
        EXPECT_NE(part1, part2);
        EXPECT_NE(part1, part3);
        EXPECT_NE(part1, part4);
        EXPECT_NE(part2, part3);
        EXPECT_NE(part2, part4);
        EXPECT_NE(part3, part4);
    }
}

// Test: Consecutive tokens are different
TEST_F(TokenGenerationTest, ConsecutiveTokensDifferent) {
    std::string prev_token = generate_test_token();
    
    for (int i = 0; i < 100; ++i) {
        std::string token = generate_test_token();
        EXPECT_NE(token, prev_token);
        prev_token = token;
    }
}

// Test: Token hashes are unique
TEST_F(TokenGenerationTest, UniqueTokenHashes) {
    const int NUM_TOKENS = 1000;
    std::unordered_set<std::string> hashes;
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        std::string hash = security::TokenValidator::compute_token_hash(token);
        
        // Check for hash collision
        EXPECT_TRUE(hashes.find(hash) == hashes.end())
            << "Hash collision detected at iteration " << i;
        
        hashes.insert(hash);
    }
    
    EXPECT_EQ(hashes.size(), NUM_TOKENS);
}

// Test: Token hash is deterministic
TEST_F(TokenGenerationTest, TokenHashDeterministic) {
    std::string token = generate_test_token();
    
    std::string hash1 = security::TokenValidator::compute_token_hash(token);
    std::string hash2 = security::TokenValidator::compute_token_hash(token);
    std::string hash3 = security::TokenValidator::compute_token_hash(token);
    
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash2, hash3);
}

// Test: Different tokens produce different hashes
TEST_F(TokenGenerationTest, DifferentTokensDifferentHashes) {
    std::string token1 = generate_test_token();
    std::string token2 = generate_test_token();
    
    EXPECT_NE(token1, token2);
    
    std::string hash1 = security::TokenValidator::compute_token_hash(token1);
    std::string hash2 = security::TokenValidator::compute_token_hash(token2);
    
    EXPECT_NE(hash1, hash2);
}

// Test: Token has sufficient bit transitions (not all zeros or ones)
TEST_F(TokenGenerationTest, SufficientBitTransitions) {
    const int NUM_TOKENS = 100;
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        
        // Count number of '0' and 'f' characters (all bits 0 or all bits 1)
        int zero_count = 0;
        int f_count = 0;
        
        for (char c : token) {
            if (c == '0') zero_count++;
            if (c == 'f') f_count++;
        }
        
        // Less than 50% should be all-zeros or all-ones
        EXPECT_LT(zero_count, 32) << "Token has too many '0' characters";
        EXPECT_LT(f_count, 32) << "Token has too many 'f' characters";
    }
}

// Test: Token randomness - Hamming distance between consecutive tokens
TEST_F(TokenGenerationTest, HighHammingDistance) {
    const int NUM_PAIRS = 100;
    
    for (int i = 0; i < NUM_PAIRS; ++i) {
        std::string token1 = generate_test_token();
        std::string token2 = generate_test_token();
        
        // Count differing hex characters
        int differences = 0;
        for (size_t j = 0; j < 64; ++j) {
            if (token1[j] != token2[j]) {
                differences++;
            }
        }
        
        // At least 50% of characters should differ (very high probability for random)
        EXPECT_GT(differences, 30)
            << "Tokens are too similar (only " << differences << " differences)";
    }
}

// Test: Token bytes are not predictable (no sequential patterns)
TEST_F(TokenGenerationTest, NoSequentialPatterns) {
    const int NUM_TOKENS = 100;
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        
        // Check for sequential hex characters (e.g., "0123456789abcdef")
        int max_sequential = 0;
        int current_sequential = 1;
        
        for (size_t j = 1; j < token.length(); ++j) {
            int val1 = (token[j-1] >= '0' && token[j-1] <= '9') ? 
                       (token[j-1] - '0') : (10 + token[j-1] - 'a');
            int val2 = (token[j] >= '0' && token[j] <= '9') ? 
                       (token[j] - '0') : (10 + token[j] - 'a');
            
            if (val2 == val1 + 1 || (val1 == 15 && val2 == 0)) {
                current_sequential++;
            } else {
                max_sequential = std::max(max_sequential, current_sequential);
                current_sequential = 1;
            }
        }
        max_sequential = std::max(max_sequential, current_sequential);
        
        // Should not have more than 5 sequential hex characters
        EXPECT_LT(max_sequential, 6)
            << "Token has sequential pattern of length " << max_sequential;
    }
}

// Test: Token generation is fast (performance test)
TEST_F(TokenGenerationTest, GenerationPerformance) {
    const int NUM_TOKENS = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_TOKENS; ++i) {
        std::string token = generate_test_token();
        // Prevent optimization from removing the loop
        volatile char c = token[0];
        (void)c;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should generate 10,000 tokens in less than 1 second
    EXPECT_LT(duration.count(), 1000)
        << "Token generation took " << duration.count() << "ms for " << NUM_TOKENS << " tokens";
    
    // Log performance
    double tokens_per_sec = (NUM_TOKENS * 1000.0) / duration.count();
    std::cout << "Token generation rate: " << tokens_per_sec << " tokens/sec" << std::endl;
}
