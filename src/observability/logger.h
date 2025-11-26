#pragma once

#include <string>
#include <map>
#include <memory>
#include <sstream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace protogate {
namespace observability {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

/**
 * @brief Structured JSON logger for stdout and Azure Log Analytics
 * 
 * All logs are emitted as JSON to stdout for container runtime capture.
 * Logs are also batched and sent to Azure Log Analytics if configured.
 */
class Logger {
public:
    /**
     * @brief Initialize logger with optional Log Analytics integration
     */
    static void initialize(
        LogLevel min_level = LogLevel::INFO,
        const std::string& workspace_id = "",
        const std::string& workspace_key = "");
    
    /**
     * @brief Get the singleton logger instance
     */
    static Logger& instance();
    
    /**
     * @brief Log a message with structured fields
     */
    void log(LogLevel level,
             const std::string& message,
             const std::map<std::string, std::string>& fields = {});
    
    /**
     * @brief Log debug message
     */
    void debug(const std::string& message,
               const std::map<std::string, std::string>& fields = {});
    
    /**
     * @brief Log info message
     */
    void info(const std::string& message,
              const std::map<std::string, std::string>& fields = {});
    
    /**
     * @brief Log warning message
     */
    void warning(const std::string& message,
                 const std::map<std::string, std::string>& fields = {});
    
    /**
     * @brief Log error message
     */
    void error(const std::string& message,
               const std::map<std::string, std::string>& fields = {});
    
    /**
     * @brief Log critical message
     */
    void critical(const std::string& message,
                  const std::map<std::string, std::string>& fields = {});
    
    /**
     * @brief Set minimum log level
     */
    void set_min_level(LogLevel level);
    
    /**
     * @brief Flush any pending logs to Log Analytics
     */
    void flush();

private:
    friend class std::default_delete<Logger>;
    
    Logger();
    ~Logger();
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void emit_log(const nlohmann::json& log_entry);
    void send_to_log_analytics(const nlohmann::json& log_entry);
    
    std::string level_to_string(LogLevel level) const;
    std::string get_iso8601_timestamp() const;
    std::string get_rfc1123_date() const;
    std::string compute_hmac_sha256(const std::string& key_base64, const std::string& data) const;
    
    LogLevel min_level_;
    std::string workspace_id_;
    std::string workspace_key_;
    bool log_analytics_enabled_;
    
    // Batch buffer for Log Analytics
    std::vector<nlohmann::json> log_buffer_;
    std::chrono::steady_clock::time_point last_flush_;
    static constexpr size_t BATCH_SIZE = 100;
    static constexpr auto BATCH_INTERVAL = std::chrono::seconds(10);
};

// Convenience macros for logging
// Convenience macros for logging
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define LOG_DEBUG(msg, ...) \
    protogate::observability::Logger::instance().debug(msg, ##__VA_ARGS__)

#define LOG_INFO(msg, ...) \
    protogate::observability::Logger::instance().info(msg, ##__VA_ARGS__)

#define LOG_WARNING(msg, ...) \
    protogate::observability::Logger::instance().warning(msg, ##__VA_ARGS__)

#define LOG_ERROR(msg, ...) \
    protogate::observability::Logger::instance().error(msg, ##__VA_ARGS__)

#define LOG_CRITICAL(msg, ...) \
    protogate::observability::Logger::instance().critical(msg, ##__VA_ARGS__)

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

}  // namespace observability
}  // namespace protogate
