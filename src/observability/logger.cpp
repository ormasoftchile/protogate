#include "observability/logger.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <mutex>

namespace protogate {
namespace observability {

static std::unique_ptr<Logger> g_logger;
static std::mutex g_logger_mutex;

void Logger::initialize(LogLevel min_level,
                       const std::string& workspace_id,
                       const std::string& workspace_key) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (!g_logger) {
        g_logger = std::unique_ptr<Logger>(new Logger());
        g_logger->min_level_ = min_level;
        g_logger->workspace_id_ = workspace_id;
        g_logger->workspace_key_ = workspace_key;
        g_logger->log_analytics_enabled_ = !workspace_id.empty() && !workspace_key.empty();
        g_logger->last_flush_ = std::chrono::steady_clock::now();
    }
}

Logger& Logger::instance() {
    if (!g_logger) {
        initialize();
    }
    return *g_logger;
}

Logger::Logger()
    : min_level_(LogLevel::INFO),
      log_analytics_enabled_(false) {}

Logger::~Logger() {
    flush();
}

void Logger::log(LogLevel level,
                const std::string& message,
                const std::map<std::string, std::string>& fields) {
    if (level < min_level_) {
        return;
    }
    
    nlohmann::json log_entry;
    log_entry["timestamp"] = get_iso8601_timestamp();
    log_entry["level"] = level_to_string(level);
    log_entry["message"] = message;
    log_entry["service"] = "protogate";
    
    // Add custom fields
    if (!fields.empty()) {
        log_entry["fields"] = nlohmann::json::object();
        for (const auto& [key, value] : fields) {
            log_entry["fields"][key] = value;
        }
    }
    
    emit_log(log_entry);
    
    // Send to Log Analytics if enabled
    if (log_analytics_enabled_) {
        send_to_log_analytics(log_entry);
    }
}

void Logger::debug(const std::string& message,
                  const std::map<std::string, std::string>& fields) {
    log(LogLevel::DEBUG, message, fields);
}

void Logger::info(const std::string& message,
                 const std::map<std::string, std::string>& fields) {
    log(LogLevel::INFO, message, fields);
}

void Logger::warning(const std::string& message,
                    const std::map<std::string, std::string>& fields) {
    log(LogLevel::WARNING, message, fields);
}

void Logger::error(const std::string& message,
                  const std::map<std::string, std::string>& fields) {
    log(LogLevel::ERROR, message, fields);
}

void Logger::critical(const std::string& message,
                     const std::map<std::string, std::string>& fields) {
    log(LogLevel::CRITICAL, message, fields);
}

void Logger::set_min_level(LogLevel level) {
    min_level_ = level;
}

void Logger::flush() {
    if (!log_buffer_.empty()) {
        // TODO: Implement batch send to Log Analytics
        // For now, just clear the buffer
        log_buffer_.clear();
        last_flush_ = std::chrono::steady_clock::now();
    }
}

void Logger::emit_log(const nlohmann::json& log_entry) {
    // Thread-safe console output
    static std::mutex cout_mutex;
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << log_entry.dump() << std::endl;
}

void Logger::send_to_log_analytics(const nlohmann::json& log_entry) {
    log_buffer_.push_back(log_entry);
    
    // Flush if batch size or time interval reached
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_flush_);
    
    if (log_buffer_.size() >= BATCH_SIZE || elapsed >= BATCH_INTERVAL) {
        flush();
    }
}

std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_iso8601_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&itt), "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

}  // namespace observability
}  // namespace protogate
