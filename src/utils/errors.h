#pragma once

#include <system_error>
#include <string>
#include <stdexcept>

namespace protogate {
namespace errors {

/**
 * @brief Error categories for Protogate-specific errors
 */
enum class ErrorCode {
    // Configuration errors (1-99)
    config_invalid = 1,
    config_missing_required = 2,
    
    // Authentication errors (100-199)
    auth_token_invalid = 100,
    auth_token_expired = 101,
    auth_token_missing = 102,
    auth_unauthorized = 103,
    
    // Network errors (200-299)
    network_connection_failed = 200,
    network_connection_closed = 201,
    network_timeout = 202,
    network_tls_handshake_failed = 203,
    network_invalid_protocol = 204,
    
    // Tunnel errors (300-399)
    tunnel_not_found = 300,
    tunnel_already_exists = 301,
    tunnel_agent_not_connected = 302,
    tunnel_capacity_exceeded = 303,
    tunnel_suspended = 304,
    
    // Proxy errors (400-499)
    proxy_upstream_error = 400,
    proxy_request_too_large = 401,
    proxy_response_too_large = 402,
    proxy_invalid_request = 403,
    
    // Storage errors (500-599)
    storage_key_vault_error = 500,
    storage_cache_full = 501,
    storage_not_found = 502,
    
    // System errors (600-699)
    system_resource_exhausted = 600,
    system_internal_error = 601,
    system_not_implemented = 602,
};

/**
 * @brief Error category for Protogate errors
 */
class ProtoError std::error_category {
public:
    const char* name() const noexcept override;
    std::string message(int ev) const override;
};

/**
 * @brief Get the global ProtoError category instance
 */
const std::error_category& proto_error_category();

/**
 * @brief Create an error_code from ProtoError
 */
std::error_code make_error_code(ErrorCode e);

/**
 * @brief Base exception class for Protogate errors
 */
class ProtogateException : public std::runtime_error {
public:
    explicit ProtogateException(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    
    ErrorCode code() const { return code_; }
    
private:
    ErrorCode code_;
};

/**
 * @brief Configuration-related exceptions
 */
class ConfigException : public ProtogateException {
public:
    explicit ConfigException(const std::string& message)
        : ProtogateException(ErrorCode::config_invalid, message) {}
};

/**
 * @brief Authentication-related exceptions
 */
class AuthException : public ProtogateException {
public:
    explicit AuthException(ErrorCode code, const std::string& message)
        : ProtogateException(code, message) {}
};

/**
 * @brief Network-related exceptions
 */
class NetworkException : public ProtogateException {
public:
    explicit NetworkException(ErrorCode code, const std::string& message)
        : ProtogateException(code, message) {}
};

/**
 * @brief Tunnel-related exceptions
 */
class TunnelException : public ProtogateException {
public:
    explicit TunnelException(ErrorCode code, const std::string& message)
        : ProtogateException(code, message) {}
};

}  // namespace errors
}  // namespace protogate

// Enable error_code integration
namespace std {
template <>
struct is_error_code_enum<protogate::errors::ErrorCode> : true_type {};
}
