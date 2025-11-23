#pragma once

#include "../models/audit_event.h"
#include <string>
#include <map>

namespace protogate {
namespace observability {

/**
 * @brief Audit logger for security and compliance events
 * 
 * Provides structured logging for security-relevant events such as:
 * - IP allowlist violations
 * - Rate limit violations
 * - Authentication failures
 * - Tunnel lifecycle events
 * 
 * All audit events are logged in structured JSON format suitable for
 * ingestion by Azure Log Analytics or other SIEM systems.
 */
class AuditLogger {
public:
    /**
     * @brief Get singleton instance
     */
    static AuditLogger& instance();
    
    /**
     * @brief Log IP allowlist violation
     * @param tunnel_id Tunnel identifier
     * @param source_ip Blocked source IP
     * @param protocol Protocol (HTTP/TCP)
     * @param reason Detailed reason for blocking
     */
    void log_ip_blocked(
        const std::string& tunnel_id,
        const std::string& source_ip,
        const std::string& protocol,
        const std::string& reason = "IP not in allowlist");
    
    /**
     * @brief Log rate limit violation
     * @param tunnel_id Tunnel identifier
     * @param source_ip Source IP (optional)
     * @param limit_type Type of rate limit (requests_per_second, burst)
     * @param current_rate Current request rate
     * @param limit Configured limit
     */
    void log_rate_limit_exceeded(
        const std::string& tunnel_id,
        const std::string& source_ip,
        const std::string& limit_type,
        double current_rate,
        double limit);
    
    /**
     * @brief Log authentication failure
     * @param tunnel_id Tunnel identifier (if known)
     * @param source_ip Source IP
     * @param reason Failure reason
     */
    void log_auth_failed(
        const std::string& tunnel_id,
        const std::string& source_ip,
        const std::string& reason);
    
    /**
     * @brief Log tunnel lifecycle event
     * @param event_type Event type (CREATED, DELETED, etc.)
     * @param tunnel_id Tunnel identifier
     * @param user_id User identifier (optional)
     * @param metadata Additional metadata
     */
    void log_tunnel_event(
        models::AuditEventType event_type,
        const std::string& tunnel_id,
        const std::string& user_id = "",
        const std::map<std::string, std::string>& metadata = {});
    
    /**
     * @brief Log agent connection event
     * @param event_type AGENT_CONNECTED or AGENT_DISCONNECTED
     * @param tunnel_id Tunnel identifier
     * @param agent_id Agent identifier
     * @param source_ip Agent source IP
     * @param metadata Additional metadata
     */
    void log_agent_event(
        models::AuditEventType event_type,
        const std::string& tunnel_id,
        const std::string& agent_id,
        const std::string& source_ip,
        const std::map<std::string, std::string>& metadata = {});
    
    /**
     * @brief Log generic audit event
     * @param event Event to log
     */
    void log_event(const models::AuditEvent& event);

private:
    AuditLogger() = default;
    ~AuditLogger() = default;
    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;
};

}  // namespace observability
}  // namespace protogate
