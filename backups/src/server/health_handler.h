#pragma once

#include <string>
#include <memory>
#include "../observability/metrics.h"

namespace protogate {
namespace server {

/**
 * @brief Health check handler for /health endpoint
 * 
 * Returns service health status in JSON format.
 * Used by container orchestrators (K8s, Container Apps) for liveness/readiness probes.
 */
class HealthHandler {
public:
    enum class HealthStatus {
        HEALTHY,      // Service is fully operational
        DEGRADED,     // Service is operational but some issues detected
        UNHEALTHY     // Service is not operational
    };
    
    /**
     * @brief Get current health status
     * @return HealthStatus enum indicating service health
     */
    static HealthStatus get_health_status();
    
    /**
     * @brief Generate health check JSON response
     * @return JSON string with health status and metrics
     */
    static std::string get_health_json();
    
    /**
     * @brief Get HTTP status code for health
     * @return 200 for healthy, 503 for degraded/unhealthy
     */
    static int get_http_status_code();
    
private:
    /**
     * @brief Check if system is degraded based on metrics
     * @return true if service is degraded (high error rate, memory issues, etc.)
     */
    static bool is_degraded();
};

}  // namespace server
}  // namespace protogate
