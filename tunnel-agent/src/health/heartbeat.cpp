#include "heartbeat.h"
#include "../utils/logger.h"

namespace protogate {
namespace agent {

HeartbeatManager::HeartbeatManager(HTTP2Session& session, int interval_ms, int timeout_ms)
    : session_(session), interval_ms_(interval_ms), timeout_ms_(timeout_ms), started_(false) {
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::start() {
    last_ping_time_ = std::chrono::steady_clock::now();
    last_ack_time_ = last_ping_time_;
    started_ = true;
    
    Logger::info("Heartbeat manager started", {
        {"interval_ms", std::to_string(interval_ms_)},
        {"timeout_ms", std::to_string(timeout_ms_)}
    });
}

void HeartbeatManager::stop() {
    started_ = false;
    Logger::info("Heartbeat manager stopped");
}

bool HeartbeatManager::should_send_ping() const {
    if (!started_) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ping_time_).count();
    
    return elapsed >= interval_ms_;
}

bool HeartbeatManager::is_timed_out() const {
    if (!started_) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ack_time_).count();
    
    if (elapsed >= timeout_ms_) {
        Logger::warning("Heartbeat timeout detected", {
            {"elapsed_ms", std::to_string(elapsed)},
            {"timeout_ms", std::to_string(timeout_ms_)}
        });
        return true;
    }
    
    return false;
}

void HeartbeatManager::record_ping_ack() {
    last_ack_time_ = std::chrono::steady_clock::now();
    Logger::debug("PING ACK received");
}

}  // namespace agent
}  // namespace protogate
