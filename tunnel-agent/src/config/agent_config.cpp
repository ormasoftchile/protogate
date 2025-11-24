#include "agent_config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <cstdlib>

using json = nlohmann::json;

namespace protogate {
namespace agent {

AgentConfig AgentConfig::from_json_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path);
    }
    
    json j;
    file >> j;
    
    AgentConfig config;
    
    // Parse server config
    config.server.host = j["server"]["host"].get<std::string>();
    config.server.port = j["server"]["port"].get<unsigned short>();
    config.server.verify_tls = j["server"]["verify_tls"].get<bool>();
    
    // Parse tunnel config
    config.tunnel.id = j["tunnel"]["id"].get<std::string>();
    config.tunnel.token = j["tunnel"]["token"].get<std::string>();
    
    // Parse local config
    config.local.url = j["local"]["url"].get<std::string>();
    config.local.timeout_seconds = j["local"]["timeout_seconds"].get<int>();
    
    // Parse health config
    config.health.heartbeat_interval_seconds = j["health"]["heartbeat_interval_seconds"].get<int>();
    config.health.heartbeat_timeout_seconds = j["health"]["heartbeat_timeout_seconds"].get<int>();
    
    // Parse reconnect config
    config.reconnect.initial_delay_seconds = j["reconnect"]["initial_delay_seconds"].get<int>();
    config.reconnect.max_delay_seconds = j["reconnect"]["max_delay_seconds"].get<int>();
    config.reconnect.max_attempts = j["reconnect"]["max_attempts"].get<int>();
    
    // Parse logging config
    config.logging.level = j["logging"]["level"].get<std::string>();
    config.logging.format = j["logging"]["format"].get<std::string>();
    
    return config;
}

AgentConfig AgentConfig::from_cli_args(int argc, char* argv[]) {
    AgentConfig config;
    
    // Set defaults
    config.server.host = "localhost";
    config.server.port = 8443;
    config.server.verify_tls = true;
    config.local.url = "http://localhost:3000";
    config.local.timeout_seconds = 1800;
    config.health.heartbeat_interval_seconds = 30;
    config.health.heartbeat_timeout_seconds = 60;
    config.reconnect.initial_delay_seconds = 1;
    config.reconnect.max_delay_seconds = 60;
    config.reconnect.max_attempts = 0;
    config.logging.level = "info";
    config.logging.format = "json";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--config" && i + 1 < argc) {
            return from_json_file(argv[++i]);
        } else if (arg == "--server" && i + 1 < argc) {
            std::string server = argv[++i];
            size_t colon_pos = server.find(':');
            if (colon_pos != std::string::npos) {
                config.server.host = server.substr(0, colon_pos);
                config.server.port = std::stoi(server.substr(colon_pos + 1));
            } else {
                config.server.host = server;
            }
        } else if (arg == "--token" && i + 1 < argc) {
            config.tunnel.token = argv[++i];
        } else if (arg == "--tunnel-id" && i + 1 < argc) {
            config.tunnel.id = argv[++i];
        } else if (arg == "--local-url" && i + 1 < argc) {
            config.local.url = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            throw std::runtime_error("HELP_REQUESTED");
        }
    }
    
    return config;
}

AgentConfig AgentConfig::from_environment() {
    AgentConfig config;
    
    // Set defaults
    config.server.host = "localhost";
    config.server.port = 8443;
    config.server.verify_tls = true;
    config.local.url = "http://localhost:3000";
    config.local.timeout_seconds = 1800;
    config.health.heartbeat_interval_seconds = 30;
    config.health.heartbeat_timeout_seconds = 60;
    config.reconnect.initial_delay_seconds = 1;
    config.reconnect.max_delay_seconds = 60;
    config.reconnect.max_attempts = 0;
    config.logging.level = "info";
    config.logging.format = "json";
    
    // Read environment variables
    if (const char* server = std::getenv("TUNNEL_SERVER")) {
        std::string server_str = server;
        size_t colon_pos = server_str.find(':');
        if (colon_pos != std::string::npos) {
            config.server.host = server_str.substr(0, colon_pos);
            config.server.port = std::stoi(server_str.substr(colon_pos + 1));
        } else {
            config.server.host = server_str;
        }
    }
    
    if (const char* token = std::getenv("TUNNEL_TOKEN")) {
        config.tunnel.token = token;
    }
    
    if (const char* tunnel_id = std::getenv("TUNNEL_ID")) {
        config.tunnel.id = tunnel_id;
    }
    
    if (const char* local_url = std::getenv("LOCAL_URL")) {
        config.local.url = local_url;
    }
    
    return config;
}

void AgentConfig::validate() const {
    if (server.host.empty()) {
        throw std::runtime_error("Server host is required");
    }
    
    if (server.port == 0) {
        throw std::runtime_error("Server port is required");
    }
    
    if (tunnel.id.empty()) {
        throw std::runtime_error("Tunnel ID is required");
    }
    
    if (tunnel.token.empty()) {
        throw std::runtime_error("Tunnel token is required");
    }
    
    if (local.url.empty()) {
        throw std::runtime_error("Local URL is required");
    }
    
    if (local.timeout_seconds <= 0) {
        throw std::runtime_error("Local timeout must be positive");
    }
    
    if (health.heartbeat_interval_seconds <= 0) {
        throw std::runtime_error("Heartbeat interval must be positive");
    }
    
    if (health.heartbeat_timeout_seconds <= health.heartbeat_interval_seconds) {
        throw std::runtime_error("Heartbeat timeout must be greater than interval");
    }
}

}  // namespace agent
}  // namespace protogate
