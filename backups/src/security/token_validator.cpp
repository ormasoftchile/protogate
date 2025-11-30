#include "token_validator.h"
#include "../observability/logger.h"
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace protogate {
namespace security {

TokenValidator::TokenValidator(
    std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache)
    : token_cache_(token_cache) {
    
    observability::Logger::instance().info("TokenValidator initialized");
}

TokenValidator::ValidationResult TokenValidator::validate(const std::string& authorization_header) {
    // Extract token from Authorization header
    std::string token = extract_token(authorization_header);
    
    if (token.empty()) {
        observability::Logger::instance().warning("Invalid authorization header format");
        return ValidationResult::failure("INVALID_FORMAT", "Authorization header must be 'Bearer tnl_...'");
    }
    
    return validate_token(token);
}

TokenValidator::ValidationResult TokenValidator::validate_token(const std::string& token) {
    // Compute token hash
    auto token_hash = compute_token_hash(token);
    
    // Search cache for matching token
    bool found = false;
    std::string matched_tunnel_id;
    
    token_cache_->for_each([&](const std::string& tunnel_id, const models::AuthToken& auth_token) {
        if (auth_token.token_hash == token_hash) {
            // Check expiration
            if (is_expired(auth_token)) {
                auto expires_time = std::chrono::system_clock::to_time_t(auth_token.expires_at);
                observability::Logger::instance().warning("Token expired", {
                    {"tunnel_id", tunnel_id},
                    {"expires_at", std::to_string(expires_time)}
                });
                return; // Continue searching
            }
            
            found = true;
            matched_tunnel_id = tunnel_id;
        }
    });
    
    if (found) {
        observability::Logger::instance().info("Token validated successfully", {
            {"tunnel_id", matched_tunnel_id}
        });
        return ValidationResult::success(matched_tunnel_id);
    }
    
    observability::Logger::instance().warning("Token validation failed: not found");
    return ValidationResult::failure("INVALID_TOKEN", "Token not found or expired");
}

void TokenValidator::add_token(const std::string& tunnel_id, const models::AuthToken& token,
                                std::chrono::seconds ttl) {
    token_cache_->put(tunnel_id, token, ttl);
    
    observability::Logger::instance().info("Token added to cache", {
        {"tunnel_id", tunnel_id},
        {"ttl_seconds", std::to_string(ttl.count())}
    });
}

void TokenValidator::revoke_token(const std::string& tunnel_id) {
    token_cache_->remove(tunnel_id);
    
    observability::Logger::instance().info("Token revoked", {
        {"tunnel_id", tunnel_id}
    });
}

void TokenValidator::rotate_token(const std::string& tunnel_id, const models::AuthToken& new_token,
                                  std::chrono::seconds grace_period) {
    // Get old token
    auto old_token = token_cache_->get(tunnel_id);
    
    // Add new token
    add_token(tunnel_id, new_token);
    
    // Keep old token with grace period (store as tunnel_id:old)
    if (old_token) {
        std::string old_key = tunnel_id + ":old";
        token_cache_->put(old_key, *old_token, grace_period);
        
        observability::Logger::instance().info("Token rotated with grace period", {
            {"tunnel_id", tunnel_id},
            {"grace_period_seconds", std::to_string(grace_period.count())}
        });
    }
}

std::string TokenValidator::extract_token(const std::string& authorization_header) const {
    // Expected format: "Bearer tnl_..."
    const std::string bearer_prefix = "Bearer ";
    
    if (authorization_header.compare(0, bearer_prefix.length(), bearer_prefix) != 0) {
        return "";
    }
    
    std::string token = authorization_header.substr(bearer_prefix.length());
    
    // Trim whitespace
    token.erase(0, token.find_first_not_of(" \t\n\r"));
    token.erase(token.find_last_not_of(" \t\n\r") + 1);
    
    // Validate token prefix
    if (token.compare(0, 4, "tnl_") != 0) {
        return "";
    }
    
    return token;
}

std::string TokenValidator::compute_token_hash(const std::string& token) {
    std::array<uint8_t, 32> hash;
    SHA256(reinterpret_cast<const unsigned char*>(token.data()), token.size(), hash.data());
    
    // Convert to hex string
    std::stringstream ss;
    for (size_t i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

bool TokenValidator::is_expired(const models::AuthToken& token) const {
    auto now = std::chrono::system_clock::now();
    return now >= token.expires_at;
}

}  // namespace security
}  // namespace protogate
