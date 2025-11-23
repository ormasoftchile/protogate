#include "observability/logger.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <mutex>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

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
    if (log_buffer_.empty()) {
        return;
    }
    
    if (!log_analytics_enabled_ || workspace_id_.empty() || workspace_key_.empty()) {
        log_buffer_.clear();
        last_flush_ = std::chrono::steady_clock::now();
        return;
    }
    
    try {
        // Build JSON array for batch upload
        nlohmann::json batch = nlohmann::json::array();
        for (const auto& log_entry : log_buffer_) {
            batch.push_back(log_entry);
        }
        
        std::string body = batch.dump();
        
        // Build Azure Monitor Ingestion API request
        // POST https://{workspace_id}.ods.opinsights.azure.com/api/logs?api-version=2016-04-01
        std::string date = get_rfc1123_date();
        std::string content_type = "application/json";
        std::string resource = "/api/logs";
        std::string content_length = std::to_string(body.size());
        
        // Build signature string: POST\n{content-length}\n{content-type}\nx-ms-date:{date}\n{resource}
        std::ostringstream sig_builder;
        sig_builder << "POST\n"
                   << content_length << "\n"
                   << content_type << "\n"
                   << "x-ms-date:" << date << "\n"
                   << resource;
        std::string signature_string = sig_builder.str();
        
        // Sign with HMAC-SHA256
        std::string signature = compute_hmac_sha256(workspace_key_, signature_string);
        std::string authorization = "SharedKey " + workspace_id_ + ":" + signature;
        
        // TODO: Implement actual HTTP POST using libcurl or Boost.Beast
        // For now, just log that we would send it
        debug("Would send batch to Log Analytics", {
            {"workspace_id", workspace_id_},
            {"batch_size", std::to_string(log_buffer_.size())},
            {"body_bytes", std::to_string(body.size())}
        });
        
        // Clear buffer
        log_buffer_.clear();
        last_flush_ = std::chrono::steady_clock::now();
        
    } catch (const std::exception& e) {
        // Don't fail logging if Analytics upload fails
        std::cerr << "Log Analytics upload failed: " << e.what() << std::endl;
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

std::string Logger::get_rfc1123_date() const {
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&itt), "%a, %d %b %Y %H:%M:%S GMT");
    return oss.str();
}

std::string Logger::compute_hmac_sha256(const std::string& key_base64, const std::string& data) const {
    // Decode base64 key
    BIO* bio = BIO_new_mem_buf(key_base64.data(), static_cast<int>(key_base64.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    std::vector<unsigned char> key(key_base64.size());
    int decoded_len = BIO_read(bio, key.data(), static_cast<int>(key.size()));
    BIO_free_all(bio);
    
    if (decoded_len <= 0) {
        throw std::runtime_error("Failed to decode base64 key");
    }
    key.resize(decoded_len);
    
    // Compute HMAC-SHA256
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    
    HMAC(EVP_sha256(),
         key.data(), key.size(),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         hash, &hash_len);
    
    // Encode result as base64
    BIO* b64_out = BIO_new(BIO_f_base64());
    BIO_set_flags(b64_out, BIO_FLAGS_BASE64_NO_NL);
    BIO* mem = BIO_new(BIO_s_mem());
    b64_out = BIO_push(b64_out, mem);
    
    BIO_write(b64_out, hash, hash_len);
    BIO_flush(b64_out);
    
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64_out, &bptr);
    
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64_out);
    
    return result;
}

}  // namespace observability
}  // namespace protogate
