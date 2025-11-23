#pragma once

#include <string>
#include <chrono>
#include <map>
#include <cstdint>

namespace protogate {
namespace models {

/**
 * @brief Request state tracking
 */
enum class RequestState {
    PENDING,        // Waiting to be forwarded
    IN_PROGRESS,    // Being processed by agent
    COMPLETED,      // Successfully completed
    FAILED,         // Failed with error
    TIMEOUT         // Timed out
};

/**
 * @brief TunnelRequest entity for tracking in-flight requests
 */
struct TunnelRequest {
    // Identity
    std::string request_id;                 // Unique request identifier
    std::string tunnel_id;                  // Associated tunnel
    std::string agent_id;                   // Handling agent
    
    // Request details
    std::string method;                     // HTTP method or "TCP"
    std::string path;                       // HTTP path or TCP descriptor
    std::map<std::string, std::string> headers;  // HTTP headers
    
    // State
    RequestState state;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point completed_at;
    
    // Metrics
    uint64_t request_size_bytes = 0;
    uint64_t response_size_bytes = 0;
    uint32_t response_status_code = 0;      // HTTP status or 0 for TCP
    
    /**
     * @brief Get request duration in milliseconds
     */
    int64_t duration_ms() const;
    
    /**
     * @brief Check if request has timed out
     */
    bool is_timed_out(uint32_t timeout_ms) const;
    
    /**
     * @brief Convert to JSON representation
     */
    std::string to_json() const;
};

}  // namespace models
}  // namespace protogate
