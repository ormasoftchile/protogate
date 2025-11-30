#include "agent_registry.h"
#include "../observability/logger.h"

namespace protogate {
namespace agent {

AgentRegistry::AgentRegistry(size_t max_agents)
    : max_agents_(max_agents) {
    
    observability::Logger::instance().info("AgentRegistry initialized", {
        {"max_agents", std::to_string(max_agents_)}
    });
}

bool AgentRegistry::register_agent(const std::string& tunnel_id, agent_ptr agent) {
    std::unique_lock lock(mutex_);
    
    // Check capacity
    if (agents_.size() >= max_agents_) {
        observability::Logger::instance().warning("Agent registry at capacity", {
            {"tunnel_id", tunnel_id},
            {"current_count", std::to_string(agents_.size())},
            {"max_agents", std::to_string(max_agents_)}
        });
        return false;
    }
    
    // Check for duplicate
    if (agents_.count(tunnel_id)) {
        observability::Logger::instance().warning("Agent already registered", {
            {"tunnel_id", tunnel_id}
        });
        return false;
    }
    
    // Register agent
    agents_[tunnel_id] = agent;
    
    observability::Logger::instance().info("Agent registered", {
        {"tunnel_id", tunnel_id},
        {"total_agents", std::to_string(agents_.size())}
    });
    
    return true;
}

void AgentRegistry::unregister_agent(const std::string& tunnel_id) {
    std::unique_lock lock(mutex_);
    
    auto it = agents_.find(tunnel_id);
    if (it == agents_.end()) {
        return;
    }
    
    agents_.erase(it);
    
    observability::Logger::instance().info("Agent unregistered", {
        {"tunnel_id", tunnel_id},
        {"remaining_agents", std::to_string(agents_.size())}
    });
}

AgentRegistry::agent_ptr AgentRegistry::get_agent(const std::string& tunnel_id) const {
    std::shared_lock lock(mutex_);
    
    auto it = agents_.find(tunnel_id);
    if (it == agents_.end()) {
        return nullptr;
    }
    
    return it->second;
}

bool AgentRegistry::is_connected(const std::string& tunnel_id) const {
    auto agent = get_agent(tunnel_id);
    return agent && agent->state() == AgentConnection::State::CONNECTED;
}

size_t AgentRegistry::count() const {
    std::shared_lock lock(mutex_);
    return agents_.size();
}

bool AgentRegistry::is_full() const {
    std::shared_lock lock(mutex_);
    return agents_.size() >= max_agents_;
}

std::vector<std::string> AgentRegistry::get_tunnel_ids() const {
    std::shared_lock lock(mutex_);
    
    std::vector<std::string> ids;
    ids.reserve(agents_.size());
    
    for (const auto& [tunnel_id, _] : agents_) {
        ids.push_back(tunnel_id);
    }
    
    return ids;
}

std::vector<models::TunnelAgent> AgentRegistry::get_all_agents() const {
    std::shared_lock lock(mutex_);
    
    std::vector<models::TunnelAgent> result;
    result.reserve(agents_.size());
    
    for (const auto& [tunnel_id, agent] : agents_) {
        result.push_back(agent->get_metadata());
    }
    
    return result;
}

void AgentRegistry::shutdown() {
    std::unique_lock lock(mutex_);
    
    observability::Logger::instance().info("Shutting down agent registry", {
        {"active_agents", std::to_string(agents_.size())}
    });
    
    // Close all connections
    for (auto& [tunnel_id, agent] : agents_) {
        agent->close();
    }
    
    agents_.clear();
    
    observability::Logger::instance().info("Agent registry shutdown complete");
}

void AgentRegistry::handle_agent_disconnect(const std::string& tunnel_id) {
    unregister_agent(tunnel_id);
}

}  // namespace agent
}  // namespace protogate
