#include "audit_event.h"
#include "../observability/logger.h"
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>

namespace protogate {
namespace models {

AuditEvent AuditEvent::create(
    AuditEventType type,
    AuditSeverity severity,
    const std::string& message,
    const std::string& tunnel_id,
    const std::string& source_ip) {
    
    AuditEvent event;
    
    // Generate event ID (timestamp-based)
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    event.event_id = "audit_" + std::to_string(ms);
    
    event.event_type = type;
    event.severity = severity;
    event.message = message;
    event.tunnel_id = tunnel_id;
    event.source_ip = source_ip;
    event.timestamp = now;
    
    return event;
}

std::string AuditEvent::to_json() const {
    nlohmann::json j;
    
    j["event_id"] = event_id;
    j["event_type"] = event_type_string();
    j["severity"] = severity_string();
    j["message"] = message;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    
    // Optional fields
    if (!tunnel_id.empty()) {
        j["tunnel_id"] = tunnel_id;
    }
    
    if (!agent_id.empty()) {
        j["agent_id"] = agent_id;
    }
    
    if (!source_ip.empty()) {
        j["source_ip"] = source_ip;
    }
    
    if (!user_id.empty()) {
        j["user_id"] = user_id;
    }
    
    // Metadata
    if (!metadata.empty()) {
        nlohmann::json meta;
        for (const auto& [key, value] : metadata) {
            meta[key] = value;
        }
        j["metadata"] = meta;
    }
    
    return j.dump();
}

std::string AuditEvent::event_type_string() const {
    switch (event_type) {
        case AuditEventType::TUNNEL_CREATED: return "TUNNEL_CREATED";
        case AuditEventType::TUNNEL_DELETED: return "TUNNEL_DELETED";
        case AuditEventType::TUNNEL_SUSPENDED: return "TUNNEL_SUSPENDED";
        case AuditEventType::TUNNEL_RESUMED: return "TUNNEL_RESUMED";
        case AuditEventType::TOKEN_GENERATED: return "TOKEN_GENERATED";
        case AuditEventType::TOKEN_ROTATED: return "TOKEN_ROTATED";
        case AuditEventType::AGENT_CONNECTED: return "AGENT_CONNECTED";
        case AuditEventType::AGENT_DISCONNECTED: return "AGENT_DISCONNECTED";
        case AuditEventType::AUTH_FAILED: return "AUTH_FAILED";
        case AuditEventType::IP_BLOCKED: return "IP_BLOCKED";
        case AuditEventType::RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
        case AuditEventType::REQUEST_PROXIED: return "REQUEST_PROXIED";
        case AuditEventType::ERROR_OCCURRED: return "ERROR_OCCURRED";
        default: return "UNKNOWN";
    }
}

std::string AuditEvent::severity_string() const {
    switch (severity) {
        case AuditSeverity::INFO: return "INFO";
        case AuditSeverity::WARNING: return "WARNING";
        case AuditSeverity::ERROR: return "ERROR";
        case AuditSeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

}  // namespace models
}  // namespace protogate
