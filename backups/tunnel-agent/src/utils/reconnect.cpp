#include "reconnect.h"
#include "logger.h"

namespace protogate {
namespace agent {

ReconnectionManager::ReconnectionManager(int initial_delay_ms, int max_delay_ms)
    : initial_delay_ms_(initial_delay_ms), 
      max_delay_ms_(max_delay_ms),
      current_delay_ms_(initial_delay_ms),
      can_reconnect_(true) {
}

ReconnectionManager::~ReconnectionManager() {
}

void ReconnectionManager::reset() {
    current_delay_ms_ = initial_delay_ms_;
    can_reconnect_ = true;
    
    Logger::debug("Reconnection manager reset");
}

int ReconnectionManager::get_next_delay_ms() {
    int delay = current_delay_ms_;
    
    // Exponential backoff: double the delay
    current_delay_ms_ = std::min(current_delay_ms_ * 2, max_delay_ms_);
    
    Logger::info("Reconnection delay calculated", {
        {"delay_ms", std::to_string(delay)},
        {"next_delay_ms", std::to_string(current_delay_ms_)}
    });
    
    return delay;
}

bool ReconnectionManager::should_reconnect() const {
    return can_reconnect_;
}

}  // namespace agent
}  // namespace protogate
