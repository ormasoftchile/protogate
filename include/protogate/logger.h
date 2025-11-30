#pragma once
#include <string>

namespace protogate {

// Logger wrapper class
class Logger {
public:
    enum class Level {
        Debug,
        Info,
        Warn,
        Error
    };
    
    // Initialize logger with level
    static void init(const std::string& level);
    
    // Log functions (will be implemented in Phase 2)
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
};

// Convenience macros (will be enhanced in Phase 2)
#define LOG_DEBUG(msg) protogate::Logger::debug(msg)
#define LOG_INFO(msg) protogate::Logger::info(msg)
#define LOG_WARN(msg) protogate::Logger::warn(msg)
#define LOG_ERROR(msg) protogate::Logger::error(msg)

} // namespace protogate
