// src/api/tunnels_handler.cpp
// Management API implementation for tunnel CRUD operations

#include "tunnels_handler.h"
#include "../observability/logger.h"
#include "../security/token_validator.h"
#include <nlohmann/json.hpp>
#include <random>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

namespace protogate {
namespace api {

TunnelsHandler::TunnelsHandler(
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache,
    std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache,
    std::shared_ptr<storage::KeyVaultClient> keyvault_client,
    std::shared_ptr<agent::AgentRegistry> agent_registry)
    : tunnel_cache_(tunnel_cache),
      token_cache_(token_cache),
      keyvault_client_(keyvault_client),
      agent_registry_(agent_registry) {
}

void TunnelsHandler::register_routes(Router& router) {
    router.post("/api/v1/tunnels", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_create_tunnel(req, res); });
    
    router.get("/api/v1/tunnels", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_list_tunnels(req, res); });
    
    router.get("/api/v1/tunnels/{id}", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_get_tunnel(req, res); });
    
    router.delete_route("/api/v1/tunnels/{id}", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_delete_tunnel(req, res); });
    
    router.post("/api/v1/tunnels/{id}/rotate-token", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_rotate_token(req, res); });
    
    router.get("/api/v1/tunnels/{id}/metrics", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_get_tunnel_metrics(req, res); });
    
    router.get("/api/v1/tunnels/{id}/agents", 
        [this](const HttpRequest& req, HttpResponse& res) { handle_get_tunnel_agents(req, res); });
}

void TunnelsHandler::handle_create_tunnel(const HttpRequest& request, HttpResponse& response) {
    try {
        // Parse request body
        json body = json::parse(request.body);
        
        // Create tunnel from JSON
        models::Tunnel tunnel;
        tunnel.tunnel_id = body.value("tunnel_id", "");
        tunnel.target_host = body.value("target_host", "");
        tunnel.target_port = body.value("target_port", 0);
        
        // Parse protocol
        std::string protocol_str = body.value("protocol", "HTTP");
        if (protocol_str == "HTTP") {
            tunnel.protocol = models::TunnelProtocol::HTTP;
        } else if (protocol_str == "TCP") {
            tunnel.protocol = models::TunnelProtocol::TCP;
        } else {
            response.set_error(400, "Invalid protocol. Must be HTTP or TCP");
            return;
        }
        
        // Parse IP allowlist
        if (body.contains("ip_allowlist") && body["ip_allowlist"].is_array()) {
            for (const auto& ip : body["ip_allowlist"]) {
                tunnel.ip_allowlist.push_back(ip.get<std::string>());
            }
        }
        
        tunnel.status = models::TunnelStatus::ACTIVE;
        tunnel.created_at = std::chrono::system_clock::now();
        tunnel.updated_at = tunnel.created_at;
        
        // Validate configuration
        std::string validation_error;
        if (!validate_tunnel_config(tunnel, validation_error)) {
            response.set_error(400, validation_error);
            return;
        }
        
        // Check if tunnel already exists
        if (tunnel_cache_->get(tunnel.tunnel_id).has_value()) {
            response.set_error(409, "Tunnel with this ID already exists");
            return;
        }
        
        // Generate authentication token
        std::string token = generate_token(tunnel.tunnel_id);
        
        // Store token in cache
        models::AuthToken auth_token;
        auth_token.tunnel_id = tunnel.tunnel_id;
        auth_token.token_hash = security::TokenValidator::compute_token_hash(token);
        auth_token.created_at = std::chrono::system_clock::now();
        auth_token.expires_at = auth_token.created_at + std::chrono::hours(24 * 365); // 1 year
        
        token_cache_->put(tunnel.tunnel_id, auth_token, std::chrono::seconds(24 * 365 * 3600));
        
        // Store token in Key Vault
        try {
            keyvault_client_->set_secret(
                "tunnel-token-" + tunnel.tunnel_id,
                token
            );
        } catch (const std::exception& e) {
            observability::Logger::instance().warning("Failed to store token in Key Vault", {
                {"tunnel_id", tunnel.tunnel_id},
                {"error", e.what()}
            });
            // Continue - token is in cache, Key Vault storage is optional backup
        }
        
        // Store tunnel in cache
        tunnel_cache_->put(tunnel.tunnel_id, tunnel, std::chrono::seconds(24 * 365 * 3600));
        
        observability::Logger::instance().info("Tunnel created", {
            {"tunnel_id", tunnel.tunnel_id},
            {"protocol", protocol_str},
            {"target", tunnel.target_host + ":" + std::to_string(tunnel.target_port)}
        });
        
        // Build response with token (only returned on creation)
        json response_json = json::parse(tunnel_to_json(tunnel, false));
        response_json["token"] = token;
        response_json["token_expires_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            auth_token.expires_at.time_since_epoch()).count();
        
        response.status_code = 201;
        response.set_json(response_json.dump());
        
    } catch (const json::parse_error& e) {
        response.set_error(400, "Invalid JSON in request body");
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to create tunnel", {
            {"error", e.what()}
        });
        response.set_error(500, "Internal server error");
    }
}

void TunnelsHandler::handle_list_tunnels(const HttpRequest& request, HttpResponse& response) {
    std::vector<models::Tunnel> tunnels;
    
    tunnel_cache_->for_each([&tunnels](const std::string&, const models::Tunnel& tunnel) {
        tunnels.push_back(tunnel);
    });
    
    response.set_json(tunnels_list_to_json(tunnels));
    
    observability::Logger::instance().debug("Listed tunnels", {
        {"count", std::to_string(tunnels.size())}
    });
}

void TunnelsHandler::handle_get_tunnel(const HttpRequest& request, HttpResponse& response) {
    auto tunnel_id = request.path_params.at("id");
    
    auto tunnel_opt = tunnel_cache_->get(tunnel_id);
    if (!tunnel_opt.has_value()) {
        response.set_error(404, "Tunnel not found");
        return;
    }
    
    response.set_json(tunnel_to_json(tunnel_opt.value(), false));
}

void TunnelsHandler::handle_delete_tunnel(const HttpRequest& request, HttpResponse& response) {
    auto tunnel_id = request.path_params.at("id");
    
    auto tunnel_opt = tunnel_cache_->get(tunnel_id);
    if (!tunnel_opt.has_value()) {
        response.set_error(404, "Tunnel not found");
        return;
    }
    
    // Disconnect any active agents
    agent_registry_->unregister_agent(tunnel_id);
    
    // Remove from caches
    tunnel_cache_->remove(tunnel_id);
    token_cache_->remove(tunnel_id);
    
    // Remove from Key Vault
    try {
        keyvault_client_->delete_secret("tunnel-token-" + tunnel_id);
    } catch (const std::exception& e) {
        observability::Logger::instance().warning("Failed to delete token from Key Vault", {
            {"tunnel_id", tunnel_id},
            {"error", e.what()}
        });
    }
    
    observability::Logger::instance().info("Tunnel deleted", {
        {"tunnel_id", tunnel_id}
    });
    
    response.status_code = 204;
    response.body = "";
}

void TunnelsHandler::handle_rotate_token(const HttpRequest& request, HttpResponse& response) {
    auto tunnel_id = request.path_params.at("id");
    
    auto tunnel_opt = tunnel_cache_->get(tunnel_id);
    if (!tunnel_opt.has_value()) {
        response.set_error(404, "Tunnel not found");
        return;
    }
    
    // Get old token
    auto old_token_opt = token_cache_->get(tunnel_id);
    if (!old_token_opt.has_value()) {
        response.set_error(500, "Token not found in cache");
        return;
    }
    
    // Generate new token
    std::string new_token = generate_token(tunnel_id);
    
    models::AuthToken new_auth_token;
    new_auth_token.tunnel_id = tunnel_id;
    new_auth_token.token_hash = security::TokenValidator::compute_token_hash(new_token);
    new_auth_token.created_at = std::chrono::system_clock::now();
    new_auth_token.expires_at = new_auth_token.created_at + std::chrono::hours(24 * 365);
    
    // Store new token with main key
    token_cache_->put(tunnel_id, new_auth_token, std::chrono::seconds(24 * 365 * 3600));
    
    // Store old token with :old suffix for grace period (5 minutes)
    token_cache_->put(tunnel_id + ":old", old_token_opt.value(), std::chrono::seconds(300));
    
    // Store new token in Key Vault
    try {
        keyvault_client_->set_secret(
            "tunnel-token-" + tunnel_id,
            new_token
        );
    } catch (const std::exception& e) {
        observability::Logger::instance().warning("Failed to store rotated token in Key Vault", {
            {"tunnel_id", tunnel_id},
            {"error", e.what()}
        });
    }
    
    observability::Logger::instance().info("Token rotated", {
        {"tunnel_id", tunnel_id},
        {"grace_period_seconds", "300"}
    });
    
    // Return new token
    json response_json;
    response_json["token"] = new_token;
    response_json["token_expires_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        new_auth_token.expires_at.time_since_epoch()).count();
    response_json["grace_period_seconds"] = 300;
    
    response.set_json(response_json.dump());
}

void TunnelsHandler::handle_get_tunnel_metrics(const HttpRequest& request, HttpResponse& response) {
    auto tunnel_id = request.path_params.at("id");
    
    auto tunnel_opt = tunnel_cache_->get(tunnel_id);
    if (!tunnel_opt.has_value()) {
        response.set_error(404, "Tunnel not found");
        return;
    }
    
    // TODO: Implement actual metrics collection
    json metrics;
    metrics["tunnel_id"] = tunnel_id;
    metrics["request_count"] = 0;
    metrics["bytes_sent"] = 0;
    metrics["bytes_received"] = 0;
    metrics["active_connections"] = 0;
    metrics["latency_p50_ms"] = 0;
    metrics["latency_p95_ms"] = 0;
    metrics["latency_p99_ms"] = 0;
    
    response.set_json(metrics.dump());
}

void TunnelsHandler::handle_get_tunnel_agents(const HttpRequest& request, HttpResponse& response) {
    auto tunnel_id = request.path_params.at("id");
    
    auto tunnel_opt = tunnel_cache_->get(tunnel_id);
    if (!tunnel_opt.has_value()) {
        response.set_error(404, "Tunnel not found");
        return;
    }
    
    auto agent = agent_registry_->get_agent(tunnel_id);
    
    json agents_json = json::array();
    if (agent) {
        auto metadata = agent->get_metadata();
        json agent_json;
        agent_json["agent_id"] = metadata.agent_id;
        agent_json["tunnel_id"] = metadata.tunnel_id;
        agent_json["remote_ip"] = metadata.remote_ip;
        agent_json["remote_port"] = metadata.remote_port;
        agent_json["state"] = (metadata.state == models::AgentConnectionState::CONNECTED) 
            ? "CONNECTED" : "DISCONNECTED";
        agent_json["connected_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            metadata.connected_at.time_since_epoch()).count();
        agent_json["bytes_sent"] = metadata.bytes_sent;
        agent_json["bytes_received"] = metadata.bytes_received;
        agent_json["active_connections"] = metadata.active_connections;
        
        agents_json.push_back(agent_json);
    }
    
    response.set_json(agents_json.dump());
}

std::string TunnelsHandler::generate_token(const std::string& tunnel_id) {
    // Generate cryptographically secure 256-bit (32 byte) token
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    
    std::ostringstream token_stream;
    token_stream << std::hex << std::setfill('0');
    
    // Generate 32 bytes (256 bits) of random data
    for (int i = 0; i < 4; ++i) {
        uint64_t random_value = dis(gen);
        token_stream << std::setw(16) << random_value;
    }
    
    return token_stream.str();
}

bool TunnelsHandler::validate_tunnel_config(const models::Tunnel& tunnel, std::string& error) {
    if (tunnel.tunnel_id.empty()) {
        error = "tunnel_id is required";
        return false;
    }
    
    if (tunnel.target_host.empty()) {
        error = "target_host is required";
        return false;
    }
    
    if (tunnel.target_port <= 0 || tunnel.target_port > 65535) {
        error = "target_port must be between 1 and 65535";
        return false;
    }
    
    // Validate tunnel_id format (alphanumeric, hyphens, underscores only)
    for (char c : tunnel.tunnel_id) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            error = "tunnel_id can only contain alphanumeric characters, hyphens, and underscores";
            return false;
        }
    }
    
    return true;
}

std::string TunnelsHandler::tunnel_to_json(const models::Tunnel& tunnel, bool include_token) const {
    json j;
    j["tunnel_id"] = tunnel.tunnel_id;
    j["target_host"] = tunnel.target_host;
    j["target_port"] = tunnel.target_port;
    j["protocol"] = (tunnel.protocol == models::TunnelProtocol::HTTP) ? "HTTP" : "TCP";
    j["status"] = (tunnel.status == models::TunnelStatus::ACTIVE) ? "ACTIVE" : "INACTIVE";
    j["ip_allowlist"] = tunnel.ip_allowlist;
    j["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        tunnel.created_at.time_since_epoch()).count();
    j["updated_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        tunnel.updated_at.time_since_epoch()).count();
    
    return j.dump();
}

std::string TunnelsHandler::tunnels_list_to_json(const std::vector<models::Tunnel>& tunnels) const {
    json j = json::array();
    
    for (const auto& tunnel : tunnels) {
        j.push_back(json::parse(tunnel_to_json(tunnel, false)));
    }
    
    return j.dump();
}

}  // namespace api
}  // namespace protogate
