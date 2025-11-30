#pragma once

#include <chrono>
#include "../client/http2_session.h"

namespace protogate {
namespace agent {

class HeartbeatManager {
public:
    HeartbeatManager(HTTP2Session& session, int interval_ms, int timeout_ms);
    ~HeartbeatManager();
    
    void start();
    void stop();
    bool should_send_ping() const;
    bool is_timed_out() const;
    void record_ping_ack();
    
private:
    HTTP2Session& session_;
    int interval_ms_;
    int timeout_ms_;
    
    std::chrono::steady_clock::time_point last_ping_time_;
    std::chrono::steady_clock::time_point last_ack_time_;
    bool started_;
};

}  // namespace agent
}  // namespace protogate
