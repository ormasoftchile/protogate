#include "health_handler.h"
#include "../observability/logger.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <sys/resource.h>

namespace protogate {
namespace server {

HealthHandler::HealthStatus HealthHandler::get_health_status() {
    // Check for degraded conditions
    if (is_degraded()) {
        return HealthStatus::DEGRADED;
    }
    
    // Service is healthy
    return HealthStatus::HEALTHY;
}

std::string HealthHandler::get_health_json() {
    auto& metrics = observability::Metrics::instance();
    auto status = get_health_status();
    auto conn_stats = metrics.get_connection_stats();
    auto throughput_stats = metrics.get_throughput_stats();
    
    nlohmann::json health;
    
    // Overall status
    health["status"] = (status == HealthStatus::HEALTHY) ? "healthy" :
                      (status == HealthStatus::DEGRADED) ? "degraded" : "unhealthy";
    health["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Service info
    health["service"] = "protogate";
    health["version"] = "1.0.0";  // TODO: Get from build system
    
    // Metrics summary
    nlohmann::json metrics_json;
    metrics_json["active_tunnels"] = metrics.get_active_tunnel_count();
    metrics_json["active_http_connections"] = conn_stats.active_http_connections;
    metrics_json["active_tcp_connections"] = conn_stats.active_tcp_connections;
    metrics_json["total_http_requests"] = conn_stats.total_http_requests;
    metrics_json["total_tcp_connections"] = conn_stats.total_tcp_connections;
    metrics_json["http_errors"] = conn_stats.http_errors;
    metrics_json["tcp_errors"] = conn_stats.tcp_errors;
    metrics_json["bytes_sent"] = throughput_stats.bytes_sent;
    metrics_json["bytes_received"] = throughput_stats.bytes_received;
    
    health["metrics"] = metrics_json;
    
    // Memory usage
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        nlohmann::json memory;
        memory["rss_kb"] = usage.ru_maxrss / 1024;  // Convert to KB on macOS
        health["memory"] = memory;
    }
    
    // Checks
    nlohmann::json checks;
    
    // Check 1: Memory usage
    if (usage.ru_maxrss > (512 * 1024 * 1024)) {  // 512MB limit
        checks["memory"] = {
            {"status", "warning"},
            {"message", "Memory usage approaching limit"}
        };
    } else {
        checks["memory"] = {
            {"status", "pass"},
            {"message", "Memory usage normal"}
        };
    }
    
    // Check 2: Error rate
    double error_rate = 0.0;
    uint64_t total_requests = conn_stats.total_http_requests;
    if (total_requests > 0) {
        error_rate = static_cast<double>(conn_stats.http_errors) / total_requests;
    }
    
    if (error_rate > 0.05) {  // 5% error rate threshold
        checks["error_rate"] = {
            {"status", "warning"},
            {"message", "High error rate detected"},
            {"rate", error_rate}
        };
    } else {
        checks["error_rate"] = {
            {"status", "pass"},
            {"message", "Error rate normal"},
            {"rate", error_rate}
        };
    }
    
    health["checks"] = checks;
    
    return health.dump(2);
}

int HealthHandler::get_http_status_code() {
    auto status = get_health_status();
    
    if (status == HealthStatus::HEALTHY) {
        return 200;  // OK
    } else {
        return 503;  // Service Unavailable
    }
}

bool HealthHandler::is_degraded() {
    auto& metrics = observability::Metrics::instance();
    auto conn_stats = metrics.get_connection_stats();
    
    // Check 1: High error rate (>5%)
    uint64_t total_requests = conn_stats.total_http_requests;
    if (total_requests > 100) {  // Only check if we have enough samples
        double error_rate = static_cast<double>(conn_stats.http_errors) / total_requests;
        if (error_rate > 0.05) {
            observability::Logger::instance().warning("Service degraded: high error rate", {
                {"error_rate", std::to_string(error_rate)}
            });
            return true;
        }
    }
    
    // Check 2: Memory pressure
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // 512MB limit
        if (usage.ru_maxrss > (512 * 1024 * 1024)) {
            observability::Logger::instance().warning("Service degraded: high memory usage", {
                {"rss_mb", std::to_string(usage.ru_maxrss / (1024 * 1024))}
            });
            return true;
        }
    }
    
    // All checks passed
    return false;
}

}  // namespace server
}  // namespace protogate
