#include "logger.h"
#include <iostream>

namespace protogate {
namespace agent {

void Logger::init(const std::string& level, const std::string& format) {
    // TODO: Initialize spdlog
}

void Logger::info(const std::string& message) {
    std::cout << "[INFO] " << message << std::endl;
}

void Logger::error(const std::string& message) {
    std::cerr << "[ERROR] " << message << std::endl;
}

}  // namespace agent
}  // namespace protogate
