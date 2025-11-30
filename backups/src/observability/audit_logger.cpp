#include "audit_logger.h"
#include "logger.h"

namespace protogate {
namespace observability {

AuditLogger& AuditLogger::instance() {
    static AuditLogger instance;
    return instance;
}

void AuditLogger::log_ip_blocked(
    const std::string& tunnel_id,
    const std::string& source_ip,
    const std::string& protocol,
    const std::string& reason) {
    
    auto event = models::AuditEvent::create(
        models::AuditEventType::IP_BLOCKED,
        models::AuditSeverity::WARNING,
        "IP address blocked by allowlist",
        tunnel_id,
        source_ip
    );
    
    event.metadata["protocol"] = protocol;
    event.metadata["reason"] = reason;
    
    log_event(event);
}

void AuditLogger::log_rate_limit_exceeded(
    const std::string& tunnel_id,
    const std::string& source_ip,
    const std::string& limit_type,
    double current_rate,
    double limit) {
    
    auto event = models::AuditEvent::create(
        models::AuditEventType::RATE_LIMIT_EXCEEDED,
        models::AuditSeverity::WARNING,
        "Rate limit exceeded",
        tunnel_id,
        source_ip
    );
    
    event.metadata["limit_type"] = limit_type;
    event.metadata["current_rate"] = std::to_string(current_rate);
    event.metadata["limit"] = std::to_string(limit);
    
    log_event(event);
}

void AuditLogger::log_auth_failed(
    const std::string& tunnel_id,
    const std::string& source_ip,
    const std::string& reason) {
    
    auto event = models::AuditEvent::create(
        models::AuditEventType::AUTH_FAILED,
        models::AuditSeverity::WARNING,
        "Authentication failed",
        tunnel_id,
        source_ip
    );
    
    event.metadata["reason"] = reason;
    
    log_event(event);
}

void AuditLogger::log_tunnel_event(
    models::AuditEventType event_type,
    const std::string& tunnel_id,
    const std::string& user_id,
    const std::map<std::string, std::string>& metadata) {
    
    models::AuditSeverity severity;
    std::string message;
    
    switch (event_type) {
        case models::AuditEventType::TUNNEL_CREATED:
            severity = models::AuditSeverity::INFO;
            message = "Tunnel created";
            break;
        case models::AuditEventType::TUNNEL_DELETED:
            severity = models::AuditSeverity::INFO;
            message = "Tunnel deleted";
            break;
        case models::AuditEventType::TUNNEL_SUSPENDED:
            severity = models::AuditSeverity::WARNING;
            message = "Tunnel suspended";
            break;
        case models::AuditEventType::TUNNEL_RESUMED:
            severity = models::AuditSeverity::INFO;
            message = "Tunnel resumed";
            break;
        default:
            severity = models::AuditSeverity::INFO;
            message = "Tunnel event";
            break;
    }
    
    auto event = models::AuditEvent::create(
        event_type,
        severity,
        message,
        tunnel_id,
        ""
    );
    
    event.user_id = user_id;
    event.metadata = metadata;
    
    log_event(event);
}

void AuditLogger::log_agent_event(
    models::AuditEventType event_type,
    const std::string& tunnel_id,
    const std::string& agent_id,
    const std::string& source_ip,
    const std::map<std::string, std::string>& metadata) {
    
    models::AuditSeverity severity;
    std::string message;
    
    switch (event_type) {
        case models::AuditEventType::AGENT_CONNECTED:
            severity = models::AuditSeverity::INFO;
            message = "Agent connected";
            break;
        case models::AuditEventType::AGENT_DISCONNECTED:
            severity = models::AuditSeverity::INFO;
            message = "Agent disconnected";
            break;
        default:
            severity = models::AuditSeverity::INFO;
            message = "Agent event";
            break;
    }
    
    auto event = models::AuditEvent::create(
        event_type,
        severity,
        message,
        tunnel_id,
        source_ip
    );
    
    event.agent_id = agent_id;
    event.metadata = metadata;
    
    log_event(event);
}

void AuditLogger::log_event(const models::AuditEvent& event) {
    // Log as structured JSON for ingestion by Log Analytics
    std::string json = event.to_json();
    
    // Use appropriate log level based on severity
    switch (event.severity) {
        case models::AuditSeverity::INFO:
            Logger::instance().info("AUDIT", {{"audit_event", json}});
            break;
        case models::AuditSeverity::WARNING:
            Logger::instance().warning("AUDIT", {{"audit_event", json}});
            break;
        case models::AuditSeverity::ERROR:
            Logger::instance().error("AUDIT", {{"audit_event", json}});
            break;
        case models::AuditSeverity::CRITICAL:
            Logger::instance().error("AUDIT_CRITICAL", {{"audit_event", json}});
            break;
    }
}

}  // namespace observability
}  // namespace protogate
