#include "utils/config.h"
#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace protogate {
namespace utils {

Config Config::from_environment() {
    Config config;
    
    // Parse PORT (optional, defaults to 443)
    if (auto port_str = get_env("PORT")) {
        try {
            int port = std::stoi(*port_str);
            if (port < 1 || port > 65535) {
                throw std::runtime_error("PORT must be between 1 and 65535");
            }
            config.port = static_cast<uint16_t>(port);
        } catch (const std::exception& e) {
            throw std::runtime_error("Invalid PORT value: " + *port_str);
        }
    }
    
    // Parse AGENT_PORT (optional, defaults to 8443)
    if (auto agent_port_str = get_env("AGENT_PORT")) {
        try {
            int port = std::stoi(*agent_port_str);
            if (port < 1 || port > 65535) {
                throw std::runtime_error("AGENT_PORT must be between 1 and 65535");
            }
            config.agent_port = static_cast<uint16_t>(port);
        } catch (const std::exception& e) {
            throw std::runtime_error("Invalid AGENT_PORT value: " + *agent_port_str);
        }
    }
    
    // KEY_VAULT_URI (required)
    if (auto kv_uri = get_env("KEY_VAULT_URI")) {
        config.key_vault_uri = *kv_uri;
    } else {
        throw std::runtime_error("KEY_VAULT_URI environment variable is required");
    }
    
    // DNS_ZONE (required)
    if (auto dns_zone = get_env("DNS_ZONE")) {
        config.dns_zone = *dns_zone;
    } else {
        throw std::runtime_error("DNS_ZONE environment variable is required");
    }
    
    // Optional: Log Analytics
    config.log_analytics_workspace_id = get_env("LOG_ANALYTICS_WORKSPACE_ID");
    config.log_analytics_key = get_env("LOG_ANALYTICS_KEY");
    config.application_insights_key = get_env("APPLICATION_INSIGHTS_KEY");
    
    // Optional: TCP ports
    if (auto tcp_ports_str = get_env("TCP_PORTS")) {
        config.tcp_ports = parse_port_list(*tcp_ports_str);
    }
    
    // Optional: Performance tuning
    if (auto max_tunnels_str = get_env("MAX_CONCURRENT_TUNNELS")) {
        config.max_concurrent_tunnels = std::stoul(*max_tunnels_str);
    }
    
    if (auto max_conn_str = get_env("MAX_CONNECTIONS_PER_TUNNEL")) {
        config.max_connections_per_tunnel = std::stoul(*max_conn_str);
    }
    
    if (auto thread_pool_str = get_env("IO_THREAD_POOL_SIZE")) {
        config.io_thread_pool_size = std::stoul(*thread_pool_str);
    }
    
    // Validate configuration
    config.validate();
    
    return config;
}

void Config::validate() const {
    // Validate Key Vault URI format
    if (key_vault_uri.empty() || 
        key_vault_uri.find("https://") != 0 ||
        key_vault_uri.find(".vault.azure.net") == std::string::npos) {
        throw std::runtime_error(
            "KEY_VAULT_URI must be in format: https://<name>.vault.azure.net");
    }
    
    // Validate DNS zone format (basic check)
    if (dns_zone.empty() || dns_zone.find('.') == std::string::npos) {
        throw std::runtime_error("DNS_ZONE must be a valid domain name");
    }
    
    // Validate ports don't conflict
    if (port == agent_port) {
        throw std::runtime_error("PORT and AGENT_PORT cannot be the same");
    }
    
    // Validate TCP ports don't conflict with main ports
    for (uint16_t tcp_port : tcp_ports) {
        if (tcp_port == port || tcp_port == agent_port) {
            throw std::runtime_error(
                "TCP_PORTS cannot include PORT or AGENT_PORT values");
        }
    }
    
    // Validate resource limits
    if (max_concurrent_tunnels == 0 || max_concurrent_tunnels > 10000) {
        throw std::runtime_error(
            "MAX_CONCURRENT_TUNNELS must be between 1 and 10000");
    }
    
    if (max_connections_per_tunnel == 0) {
        throw std::runtime_error(
            "MAX_CONNECTIONS_PER_TUNNEL must be greater than 0");
    }
}

std::optional<std::string> Config::get_env(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::vector<uint16_t> Config::parse_port_list(const std::string& ports_str) {
    std::vector<uint16_t> ports;
    std::stringstream ss(ports_str);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        if (token.empty()) {
            continue;
        }
        
        try {
            int port = std::stoi(token);
            if (port < 1 || port > 65535) {
                throw std::runtime_error("Port must be between 1 and 65535");
            }
            ports.push_back(static_cast<uint16_t>(port));
        } catch (const std::exception& e) {
            throw std::runtime_error("Invalid port in TCP_PORTS: " + token);
        }
    }
    
    return ports;
}

}  // namespace utils
}  // namespace protogate
