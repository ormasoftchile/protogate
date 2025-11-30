#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace protogate {
namespace models {

/**
 * @brief Tunnel protocol types
 */
enum class TunnelProtocol {
    HTTP,
    TCP
};

/**
 * @brief Tunnel status
 */
enum class TunnelStatus {
    INACTIVE,   // No agent connected
    ACTIVE,     // Agent connected and operational
    SUSPENDED   // Manually suspended by administrator
};

/**
 * @brief Tunnel entity representing a logical tunnel configuration
 */
struct Tunnel {
    // Identity
    std::string tunnel_id;              // Unique identifier (3-63 chars, lowercase alphanumeric + hyphens)
    
    // Configuration
    TunnelProtocol protocol;            // HTTP or TCP
    std::string target_host;            // Local target on agent side (e.g., "localhost")
    uint16_t target_port;               // Local target port (e.g., 5000, 9100)
    
    // Security
    std::vector<std::string> ip_allowlist;  // CIDR ranges (e.g., ["10.0.0.0/24"])
    uint32_t rate_limit_rpm;            // Requests per minute (0 = unlimited)
    
    // Metadata
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    TunnelStatus status;
    
    /**
     * @brief Validate tunnel configuration
     * @throws std::runtime_error if validation fails
     */
    void validate() const;
    
    /**
     * @brief Check if tunnel ID is valid format
     */
    static bool is_valid_tunnel_id(const std::string& id);
    
    /**
     * @brief Check if a given IP is allowed by this tunnel's allowlist
     * @param ip IP address to check (IPv4 or IPv6)
     * @return true if allowed, false otherwise
     */
    bool is_ip_allowed(const std::string& ip) const;
    
    /**
     * @brief Convert to JSON representation
     */
    std::string to_json() const;
    
    /**
     * @brief Create from JSON representation
     */
    static Tunnel from_json(const std::string& json);
};

}  // namespace models
}  // namespace protogate
