// src/api/tunnels_handler.h
// Management API handlers for tunnel operations

#pragma once

#include "router.h"
#include "../storage/cache.h"
#include "../storage/keyvault_client.h"
#include "../models/tunnel.h"
#include "../models/auth_token.h"
#include "../agent/agent_registry.h"
#include <memory>
#include <string>

namespace protogate {
namespace api {

class TunnelsHandler {
public:
    TunnelsHandler(
        std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache,
        std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache,
        std::shared_ptr<storage::KeyVaultClient> keyvault_client,
        std::shared_ptr<agent::AgentRegistry> agent_registry);
    
    ~TunnelsHandler() = default;

    // Register all routes with router
    void register_routes(Router& router);

private:
    // Route handlers
    void handle_create_tunnel(const HttpRequest& request, HttpResponse& response);
    void handle_list_tunnels(const HttpRequest& request, HttpResponse& response);
    void handle_get_tunnel(const HttpRequest& request, HttpResponse& response);
    void handle_delete_tunnel(const HttpRequest& request, HttpResponse& response);
    void handle_rotate_token(const HttpRequest& request, HttpResponse& response);
    void handle_get_tunnel_metrics(const HttpRequest& request, HttpResponse& response);
    void handle_get_tunnel_agents(const HttpRequest& request, HttpResponse& response);
    
    // Helper methods
    std::string generate_token(const std::string& tunnel_id);
    bool validate_tunnel_config(const models::Tunnel& tunnel, std::string& error);
    std::string tunnel_to_json(const models::Tunnel& tunnel, bool include_token = false) const;
    std::string tunnels_list_to_json(const std::vector<models::Tunnel>& tunnels) const;
    
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache_;
    std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache_;
    std::shared_ptr<storage::KeyVaultClient> keyvault_client_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
};

}  // namespace api
}  // namespace protogate
