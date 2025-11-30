#pragma once

#include "../storage/cache.h"
#include "../models/auth_token.h"
#include <string>
#include <memory>
#include <chrono>
#include <array>

namespace protogate {
namespace security {

/**
 * @brief Token validation service for tunnel agent authentication
 * 
 * Responsibilities:
 * - Validate SHA-256 bearer tokens against in-memory cache
 * - Support token rotation with 5-minute grace period
 * - Cache token hashes for fast validation (<1ms)
 * - Log authentication events for security audit
 */
class TokenValidator {
public:
    /**
     * @brief Validation result with detailed error info
     */
    struct ValidationResult {
        bool valid;
        std::string tunnel_id;
        std::string error_code;
        std::string error_message;
        
        static ValidationResult success(const std::string& tunnel_id) {
            return {true, tunnel_id, "", ""};
        }
        
        static ValidationResult failure(const std::string& code, const std::string& msg) {
            return {false, "", code, msg};
        }
    };

    /**
     * @brief Initialize token validator with cache reference
     * @param token_cache Shared cache for token storage
     */
    explicit TokenValidator(std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache);

    /**
     * @brief Validate bearer token from Authorization header
     * @param authorization_header Full "Bearer tnl_..." header value
     * @return Validation result with tunnel ID if successful
     */
    ValidationResult validate(const std::string& authorization_header);

    /**
     * @brief Validate token directly (for testing)
     * @param token Raw token string (without "Bearer " prefix)
     * @return Validation result with tunnel ID if successful
     */
    ValidationResult validate_token(const std::string& token);

    /**
     * @brief Add token to cache (for registration)
     * @param tunnel_id Tunnel identifier
     * @param token AuthToken object with hash and metadata
     * @param ttl Time-to-live for token cache entry
     */
    void add_token(const std::string& tunnel_id, const models::AuthToken& token, 
                   std::chrono::seconds ttl = std::chrono::hours(24 * 90));

    /**
     * @brief Remove token from cache (for revocation)
     * @param tunnel_id Tunnel identifier
     */
    void revoke_token(const std::string& tunnel_id);

    /**
     * @brief Update token with rotation (keep old + new for grace period)
     * @param tunnel_id Tunnel identifier
     * @param new_token New AuthToken object
     * @param grace_period Duration to maintain old token (default 5 minutes)
     */
    void rotate_token(const std::string& tunnel_id, const models::AuthToken& new_token,
                     std::chrono::seconds grace_period = std::chrono::minutes(5));

    /**
     * @brief Compute SHA-256 hash of token (public static utility)
     * @return Hex string representation of hash
     */
    static std::string compute_token_hash(const std::string& token);

private:
    /**
     * @brief Extract token from Authorization header
     * @param authorization_header "Bearer tnl_..." format
     * @return Token string or empty if invalid format
     */
    std::string extract_token(const std::string& authorization_header) const;

    /**
     * @brief Check if token is expired
     */
    bool is_expired(const models::AuthToken& token) const;

    std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache_;
};

}  // namespace security
}  // namespace protogate
