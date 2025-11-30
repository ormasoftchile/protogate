#pragma once

#include <chrono>

namespace protogate {
namespace agent {

class ReconnectionManager {
public:
    ReconnectionManager(int initial_delay_ms, int max_delay_ms);
    ~ReconnectionManager();
    
    void reset();
    int get_next_delay_ms();
    bool should_reconnect() const;
    
private:
    int initial_delay_ms_;
    int max_delay_ms_;
    int current_delay_ms_;
    std::chrono::steady_clock::time_point last_attempt_;
    bool can_reconnect_;
};

}  // namespace agent
}  // namespace protogate
