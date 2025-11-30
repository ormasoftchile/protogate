#pragma once

#include <string>
#include <optional>

namespace protogate {
namespace agent {

struct ServerConfig {
    std::string host;
    unsigned short port;
    bool verify_tls;
};

struct TunnelConfig {
    std::string id;
    std::string token;
};

struct LocalConfig {
    std::string url;
    int timeout_seconds;
};

struct HealthConfig {
    int heartbeat_interval_seconds;
    int heartbeat_timeout_seconds;
};

struct ReconnectConfig {
    int initial_delay_seconds;
    int max_delay_seconds;
    int max_attempts;
};

struct LoggingConfig {
    std::string level;
    std::string format;
};

struct AgentConfig {
    ServerConfig server;
    TunnelConfig tunnel;
    LocalConfig local;
    HealthConfig health;
    ReconnectConfig reconnect;
    LoggingConfig logging;
    
    static AgentConfig from_json_file(const std::string& path);
    static AgentConfig from_cli_args(int argc, char* argv[]);
    static AgentConfig from_environment();
    
    void validate() const;
};

}  // namespace agent
}  // namespace protogate
