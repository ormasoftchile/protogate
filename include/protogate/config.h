#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace protogate {

// Configuration structure
struct Config {
    // Server configuration
    std::string agent_endpoint;
    uint16_t agent_port = 8080;
    uint16_t tunnel_port = 9000;
    
    // Agent configuration
    std::string server_url;
    std::string agent_id;
    
    // Common configuration
    std::string shared_secret;
    std::string log_level = "info";
    
    // Heartbeat configuration
    uint32_t heartbeat_interval = 30;  // seconds
    uint32_t heartbeat_timeout = 90;    // seconds
    
    // Tunnel configuration
    struct TunnelConfig {
        std::string id;
        std::string local_host;
        uint16_t local_port;
    };
    std::vector<TunnelConfig> tunnels;
    
    // Load configuration from file
    static Config load(const std::string& path);
    
    // Validate configuration
    void validate();
};

} // namespace protogate
