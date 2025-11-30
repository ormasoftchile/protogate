#pragma once

#include <string>
#include <chrono>
#include <map>

namespace protogate {
namespace models {

/**
 * @brief Audit event types
 */
enum class AuditEventType {
    TUNNEL_CREATED,
    TUNNEL_DELETED,
    TUNNEL_SUSPENDED,
    TUNNEL_RESUMED,
    TOKEN_GENERATED,
    TOKEN_ROTATED,
    AGENT_CONNECTED,
    AGENT_DISCONNECTED,
    AUTH_FAILED,
    IP_BLOCKED,
    RATE_LIMIT_EXCEEDED,
    REQUEST_PROXIED,
    ERROR_OCCURRED
};

/**
 * @brief Audit event severity
 */
enum class AuditSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

/**
 * @brief AuditEvent entity for security and operational logging
 */
struct AuditEvent {
    // Identity
    std::string event_id;                   // Unique event identifier
    AuditEventType event_type;
    AuditSeverity severity;
    
    // Context
    std::string tunnel_id;                  // Optional: associated tunnel
    std::string agent_id;                   // Optional: associated agent
    std::string source_ip;                  // Source IP address
    std::string user_id;                    // Optional: user identifier
    
    // Event details
    std::string message;
    std::map<std::string, std::string> metadata;
    
    // Timestamp
    std::chrono::system_clock::time_point timestamp;
    
    /**
     * @brief Create an audit event
     */
    static AuditEvent create(
        AuditEventType type,
        AuditSeverity severity,
        const std::string& message,
        const std::string& tunnel_id = "",
        const std::string& source_ip = "");
    
    /**
     * @brief Convert to JSON for logging
     */
    std::string to_json() const;
    
    /**
     * @brief Get human-readable event type string
     */
    std::string event_type_string() const;
    
    /**
     * @brief Get human-readable severity string
     */
    std::string severity_string() const;
};

}  // namespace models
}  // namespace protogate
