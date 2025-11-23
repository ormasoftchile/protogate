#include "models/tunnel.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>

namespace protogate {
namespace models {

void Tunnel::validate() const {
    // Validate tunnel_id format
    if (!is_valid_tunnel_id(tunnel_id)) {
        throw std::runtime_error(
            "tunnel_id must be 3-63 characters, lowercase alphanumeric with hyphens");
    }
    
    // Validate target_host (basic check)
    if (target_host.empty()) {
        throw std::runtime_error("target_host cannot be empty");
    }
    
    // Validate target_port
    if (target_port == 0) {
        throw std::runtime_error("target_port must be between 1 and 65535");
    }
    
    // Validate rate limit
    if (rate_limit_rpm > 1000000) {
        throw std::runtime_error("rate_limit_rpm cannot exceed 1,000,000");
    }
    
    // Validate CIDR ranges (basic validation)
    for (const auto& cidr : ip_allowlist) {
        if (cidr.empty() || cidr.find('/') == std::string::npos) {
            throw std::runtime_error("Invalid CIDR format in ip_allowlist: " + cidr);
        }
    }
}

bool Tunnel::is_valid_tunnel_id(const std::string& id) {
    if (id.length() < 3 || id.length() > 63) {
        return false;
    }
    
    // Must be lowercase alphanumeric with hyphens
    // Cannot start or end with hyphen
    std::regex pattern("^[a-z0-9]([a-z0-9-]*[a-z0-9])?$");
    return std::regex_match(id, pattern);
}

bool Tunnel::is_ip_allowed(const std::string& ip) const {
    // If allowlist is empty, all IPs are allowed
    if (ip_allowlist.empty()) {
        return true;
    }
    
    // TODO: Implement CIDR matching logic
    // For now, do simple string comparison
    for (const auto& cidr : ip_allowlist) {
        if (cidr == ip || cidr == "0.0.0.0/0" || cidr == "::/0") {
            return true;
        }
    }
    
    return false;
}

std::string Tunnel::to_json() const {
    nlohmann::json j;
    j["tunnel_id"] = tunnel_id;
    j["protocol"] = (protocol == TunnelProtocol::HTTP) ? "HTTP" : "TCP";
    j["target_host"] = target_host;
    j["target_port"] = target_port;
    j["ip_allowlist"] = ip_allowlist;
    j["rate_limit_rpm"] = rate_limit_rpm;
    
    // Convert timestamps
    auto created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        created_at.time_since_epoch()).count();
    auto updated_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        updated_at.time_since_epoch()).count();
    
    j["created_at"] = created_ms;
    j["updated_at"] = updated_ms;
    
    // Status
    switch (status) {
        case TunnelStatus::INACTIVE:
            j["status"] = "INACTIVE";
            break;
        case TunnelStatus::ACTIVE:
            j["status"] = "ACTIVE";
            break;
        case TunnelStatus::SUSPENDED:
            j["status"] = "SUSPENDED";
            break;
    }
    
    return j.dump();
}

Tunnel Tunnel::from_json(const std::string& json) {
    nlohmann::json j = nlohmann::json::parse(json);
    
    Tunnel tunnel;
    tunnel.tunnel_id = j["tunnel_id"];
    
    std::string protocol_str = j["protocol"];
    tunnel.protocol = (protocol_str == "HTTP") ? TunnelProtocol::HTTP : TunnelProtocol::TCP;
    
    tunnel.target_host = j["target_host"];
    tunnel.target_port = j["target_port"];
    tunnel.ip_allowlist = j["ip_allowlist"].get<std::vector<std::string>>();
    tunnel.rate_limit_rpm = j["rate_limit_rpm"];
    
    // Parse timestamps
    tunnel.created_at = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(j["created_at"].get<int64_t>()));
    tunnel.updated_at = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(j["updated_at"].get<int64_t>()));
    
    // Parse status
    std::string status_str = j["status"];
    if (status_str == "ACTIVE") {
        tunnel.status = TunnelStatus::ACTIVE;
    } else if (status_str == "SUSPENDED") {
        tunnel.status = TunnelStatus::SUSPENDED;
    } else {
        tunnel.status = TunnelStatus::INACTIVE;
    }
    
    return tunnel;
}

}  // namespace models
}  // namespace protogate
