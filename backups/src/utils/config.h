#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace protogate {
namespace utils {

/**
 * @brief Application configuration loaded from environment variables
 * 
 * Follows 12-factor app methodology - all configuration via environment.
 * No configuration files to maintain security and simplicity.
 */
struct Config {
    // Server ports
    uint16_t port = 443;                    // HTTPS ingress port
    uint16_t agent_port = 8443;             // Agent connection port
    
    // Azure integration
    std::string key_vault_uri;              // Required: Azure Key Vault URL
    std::string dns_zone;                   // Required: DNS zone for routing
    
    // Observability (optional)
    std::optional<std::string> log_analytics_workspace_id;
    std::optional<std::string> log_analytics_key;
    std::optional<std::string> application_insights_key;
    
    // TCP tunneling (optional)
    std::vector<uint16_t> tcp_ports;        // Additional TCP ports to expose
    
    // Performance tuning
    uint32_t max_concurrent_tunnels = 50;   // Maximum active tunnel agents
    uint32_t max_connections_per_tunnel = 10000;  // Per-tunnel connection limit
    uint32_t io_thread_pool_size = 0;       // 0 = auto (number of CPU cores)
    
    // Timeouts (milliseconds)
    uint32_t tunnel_establish_timeout_ms = 30000;  // 30 seconds
    uint32_t request_timeout_ms = 1800000;         // 30 minutes
    uint32_t idle_timeout_ms = 300000;             // 5 minutes
    
    /**
     * @brief Load configuration from environment variables
     * @throws std::runtime_error if required variables are missing or invalid
     */
    static Config from_environment();
    
    /**
     * @brief Validate configuration values
     * @throws std::runtime_error if validation fails
     */
    void validate() const;
    
private:
    static std::optional<std::string> get_env(const std::string& name);
    static std::vector<uint16_t> parse_port_list(const std::string& ports_str);
};

}  // namespace utils
}  // namespace protogate
