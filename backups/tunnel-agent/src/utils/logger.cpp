#include "logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <nlohmann/json.hpp>

namespace protogate {
namespace agent {

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
std::string Logger::format_ = "json";

void Logger::init(const std::string& level, const std::string& format) {
    format_ = format;
    
    logger_ = spdlog::stdout_color_mt("tunnel_agent");
    
    // Set log level
    if (level == "debug") {
        logger_->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger_->set_level(spdlog::level::info);
    } else if (level == "warning") {
        logger_->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger_->set_level(spdlog::level::err);
    }
    
    if (format == "json") {
        logger_->set_pattern("%v");
    } else {
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    }
}

void Logger::info(const std::string& message, const std::map<std::string, std::string>& fields) {
    if (!logger_) {
        init("info", "text");
    }
    
    if (format_ == "json") {
        nlohmann::json log;
        log["level"] = "INFO";
        log["message"] = message;
        log["service"] = "tunnel-agent";
        if (!fields.empty()) {
            log["fields"] = fields;
        }
        logger_->info(log.dump());
    } else {
        std::string msg = message;
        if (!fields.empty()) {
            msg += " {";
            for (const auto& [key, value] : fields) {
                msg += key + "=" + value + " ";
            }
            msg += "}";
        }
        logger_->info(msg);
    }
}

void Logger::error(const std::string& message, const std::map<std::string, std::string>& fields) {
    if (!logger_) {
        init("info", "text");
    }
    
    if (format_ == "json") {
        nlohmann::json log;
        log["level"] = "ERROR";
        log["message"] = message;
        log["service"] = "tunnel-agent";
        if (!fields.empty()) {
            log["fields"] = fields;
        }
        logger_->error(log.dump());
    } else {
        std::string msg = message;
        if (!fields.empty()) {
            msg += " {";
            for (const auto& [key, value] : fields) {
                msg += key + "=" + value + " ";
            }
            msg += "}";
        }
        logger_->error(msg);
    }
}

void Logger::warning(const std::string& message, const std::map<std::string, std::string>& fields) {
    if (!logger_) {
        init("info", "text");
    }
    
    if (format_ == "json") {
        nlohmann::json log;
        log["level"] = "WARNING";
        log["message"] = message;
        log["service"] = "tunnel-agent";
        if (!fields.empty()) {
            log["fields"] = fields;
        }
        logger_->warn(log.dump());
    } else {
        std::string msg = message;
        if (!fields.empty()) {
            msg += " {";
            for (const auto& [key, value] : fields) {
                msg += key + "=" + value + " ";
            }
            msg += "}";
        }
        logger_->warn(msg);
    }
}

void Logger::debug(const std::string& message, const std::map<std::string, std::string>& fields) {
    if (!logger_) {
        init("info", "text");
    }
    
    if (format_ == "json") {
        nlohmann::json log;
        log["level"] = "DEBUG";
        log["message"] = message;
        log["service"] = "tunnel-agent";
        if (!fields.empty()) {
            log["fields"] = fields;
        }
        logger_->debug(log.dump());
    } else {
        std::string msg = message;
        if (!fields.empty()) {
            msg += " {";
            for (const auto& [key, value] : fields) {
                msg += key + "=" + value + " ";
            }
            msg += "}";
        }
        logger_->debug(msg);
    }
}

}  // namespace agent
}  // namespace protogate
