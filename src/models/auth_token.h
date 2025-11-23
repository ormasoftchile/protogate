#pragma once

#include <string>
#include <chrono>
#include <array>

namespace protogate {
namespace models {

/**
 * @brief AuthToken entity for tunnel authentication
 */
struct AuthToken {
    // Identity
    std::string tunnel_id;                  // Associated tunnel
    std::string token;                      // Plain token (never logged or stored)
    std::string token_hash;                 // SHA-256 hash of token (hex string)
    
    // Metadata
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    bool is_active = true;
    
    /**
     * @brief Create a new token with secure random generation
     * @param tunnel_id Associated tunnel identifier
     * @param validity_days Token validity period in days
     * @return Generated token with hash
     */
    static AuthToken generate(const std::string& tunnel_id, uint32_t validity_days = 365);
    
    /**
     * @brief Compute SHA-256 hash of a token string
     */
    static std::array<uint8_t, 32> hash_token(const std::string& token);
    
    /**
     * @brief Verify if a given token matches this token's hash
     */
    bool verify(const std::string& token) const;
    
    /**
     * @brief Check if token is expired
     */
    bool is_expired() const;
    
    /**
     * @brief Get token as base64-encoded string (for transmission)
     */
    std::string to_base64() const;
    
    /**
     * @brief Create from base64-encoded string
     */
    static AuthToken from_base64(const std::string& encoded);
};

}  // namespace models
}  // namespace protogate
