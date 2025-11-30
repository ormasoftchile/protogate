#include "protogate/logger.h"
#include <iostream>

namespace protogate {

void Logger::init(const std::string& level) {
    // Stub implementation for Phase 1
    // Will be implemented with spdlog in Phase 2 (T019)
    std::cout << "Logger initialized with level: " << level << std::endl;
}

void Logger::debug(const std::string& message) {
    std::cout << "[DEBUG] " << message << std::endl;
}

void Logger::info(const std::string& message) {
    std::cout << "[INFO] " << message << std::endl;
}

void Logger::warn(const std::string& message) {
    std::cout << "[WARN] " << message << std::endl;
}

void Logger::error(const std::string& message) {
    std::cout << "[ERROR] " << message << std::endl;
}

} // namespace protogate
