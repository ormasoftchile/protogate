#pragma once

#include "agent_connection.h"
#include "../storage/cache.h"
#include <memory>
#include <string>
#include <shared_mutex>
#include <unordered_map>

namespace protogate {
namespace agent {

/**
 * @brief Thread-safe registry of active tunnel agent connections
 * 
 * Responsibilities:
 * - Register new agent connections
 * - Lookup agent by tunnel ID
 * - Remove disconnected agents
 * - Enforce connection limits (max 50 concurrent agents per instance)
 * - Provide agent statistics for observability
 */
class AgentRegistry {
public:
    using agent_ptr = std::shared_ptr<AgentConnection>;

    /**
     * @brief Initialize agent registry
     * @param max_agents Maximum concurrent agents (default 50)
     */
    explicit AgentRegistry(size_t max_agents = 50);

    /**
     * @brief Register new agent connection
     * @param tunnel_id Tunnel identifier
     * @param agent Agent connection instance
     * @return true if registered, false if duplicate or limit reached
     */
    bool register_agent(const std::string& tunnel_id, agent_ptr agent);

    /**
     * @brief Unregister agent connection
     * @param tunnel_id Tunnel identifier
     */
    void unregister_agent(const std::string& tunnel_id);

    /**
     * @brief Get agent connection by tunnel ID
     * @param tunnel_id Tunnel identifier
     * @return Agent connection or nullptr if not found
     */
    agent_ptr get_agent(const std::string& tunnel_id) const;

    /**
     * @brief Check if agent is connected
     * @param tunnel_id Tunnel identifier
     * @return true if agent is registered and connected
     */
    bool is_connected(const std::string& tunnel_id) const;

    /**
     * @brief Get count of active agents
     */
    size_t count() const;

    /**
     * @brief Check if registry is at capacity
     */
    bool is_full() const;

    /**
     * @brief Get maximum capacity
     */
    size_t max_capacity() const { return max_agents_; }

    /**
     * @brief Get list of all connected tunnel IDs
     */
    std::vector<std::string> get_tunnel_ids() const;

    /**
     * @brief Get metadata for all agents
     */
    std::vector<models::TunnelAgent> get_all_agents() const;

    /**
     * @brief Close all connections gracefully
     */
    void shutdown();

private:
    /**
     * @brief Cleanup handler for disconnected agents
     */
    void handle_agent_disconnect(const std::string& tunnel_id);

    size_t max_agents_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, agent_ptr> agents_;
};

}  // namespace agent
}  // namespace protogate
