#include "utils/errors.h"

namespace protogate {
namespace errors {

const char* ProtoErrorCategory::name() const noexcept {
    return "protogate";
}

std::string ProtoErrorCategory::message(int ev) const {
    switch (static_cast<ErrorCode>(ev)) {
        // Configuration errors
        case ErrorCode::config_invalid:
            return "Invalid configuration";
        case ErrorCode::config_missing_required:
            return "Missing required configuration parameter";
        
        // Authentication errors
        case ErrorCode::auth_token_invalid:
            return "Invalid authentication token";
        case ErrorCode::auth_token_expired:
            return "Authentication token has expired";
        case ErrorCode::auth_token_missing:
            return "Authentication token is missing";
        case ErrorCode::auth_unauthorized:
            return "Unauthorized access";
        
        // Network errors
        case ErrorCode::network_connection_failed:
            return "Network connection failed";
        case ErrorCode::network_connection_closed:
            return "Network connection closed";
        case ErrorCode::network_timeout:
            return "Network operation timed out";
        case ErrorCode::network_tls_handshake_failed:
            return "TLS handshake failed";
        case ErrorCode::network_invalid_protocol:
            return "Invalid network protocol";
        
        // Tunnel errors
        case ErrorCode::tunnel_not_found:
            return "Tunnel not found";
        case ErrorCode::tunnel_already_exists:
            return "Tunnel already exists";
        case ErrorCode::tunnel_agent_not_connected:
            return "Tunnel agent is not connected";
        case ErrorCode::tunnel_capacity_exceeded:
            return "Maximum tunnel capacity exceeded";
        case ErrorCode::tunnel_suspended:
            return "Tunnel is suspended";
        
        // Proxy errors
        case ErrorCode::proxy_upstream_error:
            return "Upstream proxy error";
        case ErrorCode::proxy_request_too_large:
            return "Request payload too large";
        case ErrorCode::proxy_response_too_large:
            return "Response payload too large";
        case ErrorCode::proxy_invalid_request:
            return "Invalid proxy request";
        
        // Storage errors
        case ErrorCode::storage_key_vault_error:
            return "Azure Key Vault operation failed";
        case ErrorCode::storage_cache_full:
            return "Storage cache is full";
        case ErrorCode::storage_not_found:
            return "Storage item not found";
        
        // System errors
        case ErrorCode::system_resource_exhausted:
            return "System resources exhausted";
        case ErrorCode::system_internal_error:
            return "Internal server error";
        case ErrorCode::system_not_implemented:
            return "Feature not implemented";
        
        default:
            return "Unknown error";
    }
}

const std::error_category& proto_error_category() {
    static ProtoErrorCategory instance;
    return instance;
}

std::error_code make_error_code(ErrorCode e) {
    return {static_cast<int>(e), proto_error_category()};
}

}  // namespace errors
}  // namespace protogate
