#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace protogate {
namespace models {

/**
 * @brief Connection state for tunnel agent
 */
enum class AgentConnectionState {
    CONNECTING,      // Handshake in progress
    CONNECTED,       // Active and ready
    DISCONNECTING,   // Graceful shutdown in progress
    DISCONNECTED     // Not connected
};

/**
 * @brief TunnelAgent entity representing an active agent connection
 */
struct TunnelAgent {
    // Identity
    std::string agent_id;                   // Unique agent identifier
    std::string tunnel_id;                  // Associated tunnel ID
    
    // Connection details
    std::string remote_ip;                  // Agent's IP address
    uint16_t remote_port;                   // Agent's port
    AgentConnectionState state;
    
    // Metadata
    std::chrono::system_clock::time_point connected_at;
    std::chrono::system_clock::time_point last_heartbeat;
    
    // Statistics
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint32_t active_connections = 0;
    
    /**
     * @brief Check if agent connection is alive
     * @param timeout_ms Heartbeat timeout in milliseconds
     */
    bool is_alive(uint32_t timeout_ms = 30000) const;
    
    /**
     * @brief Update last heartbeat timestamp
     */
    void update_heartbeat();
    
    /**
     * @brief Convert to JSON representation
     */
    std::string to_json() const;
};

}  // namespace models
}  // namespace protogate
