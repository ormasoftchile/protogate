#include "models/tunnel_request.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace protogate {
namespace models {

int64_t TunnelRequest::duration_ms() const {
    if (state == RequestState::PENDING || state == RequestState::IN_PROGRESS) {
        // Request still in progress
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - created_at);
        return duration.count();
    } else {
        // Request completed
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            completed_at - created_at);
        return duration.count();
    }
}

bool TunnelRequest::is_timed_out(uint32_t timeout_ms) const {
    if (state == RequestState::COMPLETED || state == RequestState::FAILED || state == RequestState::TIMEOUT) {
        return false;  // Already finished
    }
    
    return duration_ms() > static_cast<int64_t>(timeout_ms);
}

std::string TunnelRequest::to_json() const {
    nlohmann::json j;
    
    j["request_id"] = request_id;
    j["tunnel_id"] = tunnel_id;
    j["agent_id"] = agent_id;
    j["method"] = method;
    j["path"] = path;
    
    // State
    switch (state) {
        case RequestState::PENDING:
            j["state"] = "PENDING";
            break;
        case RequestState::IN_PROGRESS:
            j["state"] = "IN_PROGRESS";
            break;
        case RequestState::COMPLETED:
            j["state"] = "COMPLETED";
            break;
        case RequestState::FAILED:
            j["state"] = "FAILED";
            break;
        case RequestState::TIMEOUT:
            j["state"] = "TIMEOUT";
            break;
    }
    
    // Timestamps
    auto created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        created_at.time_since_epoch()).count();
    j["created_at"] = created_ms;
    
    if (state != RequestState::PENDING && state != RequestState::IN_PROGRESS) {
        auto completed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            completed_at.time_since_epoch()).count();
        j["completed_at"] = completed_ms;
    }
    
    // Metrics
    j["request_size_bytes"] = request_size_bytes;
    j["response_size_bytes"] = response_size_bytes;
    j["duration_ms"] = duration_ms();
    
    if (response_status_code > 0) {
        j["response_status_code"] = response_status_code;
    }
    
    // TCP-specific fields (only if TCP connection)
    if (!tcp_connection_id.empty()) {
        nlohmann::json tcp;
        tcp["connection_id"] = tcp_connection_id;
        tcp["bytes_sent"] = tcp_bytes_sent;
        tcp["bytes_received"] = tcp_bytes_received;
        tcp["target_port"] = tcp_target_port;
        tcp["connection_state"] = tcp_connection_state;
        
        if (tcp_last_activity.time_since_epoch().count() > 0) {
            auto last_activity_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tcp_last_activity.time_since_epoch()).count();
            tcp["last_activity"] = last_activity_ms;
        }
        
        j["tcp"] = tcp;
    }
    
    return j.dump();
}

}  // namespace models
}  // namespace protogate
