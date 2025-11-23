#include "metrics.h"
#include "logger.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace protogate {
namespace observability {

Metrics& Metrics::instance() {
    static Metrics instance;
    return instance;
}

// Tunnel metrics

void Metrics::tunnel_connected(const std::string& tunnel_id) {
    std::unique_lock lock(tunnels_mutex_);
    active_tunnels_[tunnel_id] = std::chrono::steady_clock::now();
    
    Logger::instance().info("Tunnel connected", {
        {"tunnel_id", tunnel_id},
        {"active_tunnels", std::to_string(active_tunnels_.size())}
    });
}

void Metrics::tunnel_disconnected(const std::string& tunnel_id) {
    std::unique_lock lock(tunnels_mutex_);
    active_tunnels_.erase(tunnel_id);
    
    Logger::instance().info("Tunnel disconnected", {
        {"tunnel_id", tunnel_id},
        {"active_tunnels", std::to_string(active_tunnels_.size())}
    });
}

uint32_t Metrics::get_active_tunnel_count() const {
    std::shared_lock lock(tunnels_mutex_);
    return static_cast<uint32_t>(active_tunnels_.size());
}

// HTTP metrics

void Metrics::http_request_start(const std::string& request_id) {
    std::unique_lock lock(http_mutex_);
    active_http_requests_[request_id] = std::chrono::steady_clock::now();
    total_http_requests_++;
}

void Metrics::http_request_complete(const std::string& request_id, bool error) {
    auto start_time = std::chrono::steady_clock::time_point{};
    
    {
        std::unique_lock lock(http_mutex_);
        auto it = active_http_requests_.find(request_id);
        if (it != active_http_requests_.end()) {
            start_time = it->second;
            active_http_requests_.erase(it);
        }
    }
    
    if (error) {
        http_errors_++;
    }
    
    // Record latency if we have start time
    if (start_time.time_since_epoch().count() > 0) {
        auto duration = std::chrono::steady_clock::now() - start_time;
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(duration);
        record_latency("http_request", latency);
    }
}

void Metrics::http_bytes_transferred(uint64_t bytes_sent, uint64_t bytes_received) {
    http_bytes_sent_ += bytes_sent;
    http_bytes_received_ += bytes_received;
}

// TCP metrics

void Metrics::tcp_connection_start(const std::string& connection_id) {
    std::unique_lock lock(tcp_mutex_);
    active_tcp_connections_[connection_id] = std::chrono::steady_clock::now();
    total_tcp_connections_++;
}

void Metrics::tcp_connection_complete(const std::string& connection_id, bool error) {
    auto start_time = std::chrono::steady_clock::time_point{};
    
    {
        std::unique_lock lock(tcp_mutex_);
        auto it = active_tcp_connections_.find(connection_id);
        if (it != active_tcp_connections_.end()) {
            start_time = it->second;
            active_tcp_connections_.erase(it);
        }
    }
    
    if (error) {
        tcp_errors_++;
    }
    
    // Record latency if we have start time
    if (start_time.time_since_epoch().count() > 0) {
        auto duration = std::chrono::steady_clock::now() - start_time;
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(duration);
        record_latency("tcp_connection", latency);
    }
}

void Metrics::tcp_bytes_transferred(uint64_t bytes_sent, uint64_t bytes_received) {
    tcp_bytes_sent_ += bytes_sent;
    tcp_bytes_received_ += bytes_received;
}

// Latency tracking

void Metrics::record_latency(const std::string& operation, std::chrono::microseconds latency) {
    double latency_ms = latency.count() / 1000.0;
    
    std::unique_lock lock(latency_mutex_);
    auto& samples = latency_samples_[operation];
    
    samples.push_back(latency_ms);
    
    // Keep only last MAX_LATENCY_SAMPLES samples
    if (samples.size() > MAX_LATENCY_SAMPLES) {
        samples.erase(samples.begin(), samples.begin() + (samples.size() - MAX_LATENCY_SAMPLES));
    }
}

// Statistics retrieval

Metrics::ConnectionStats Metrics::get_connection_stats() const {
    ConnectionStats stats;
    
    stats.active_tunnels = get_active_tunnel_count();
    
    {
        std::shared_lock lock(http_mutex_);
        stats.active_http_connections = static_cast<uint32_t>(active_http_requests_.size());
    }
    
    {
        std::shared_lock lock(tcp_mutex_);
        stats.active_tcp_connections = static_cast<uint32_t>(active_tcp_connections_.size());
    }
    
    stats.total_http_requests = total_http_requests_.load();
    stats.total_tcp_connections = total_tcp_connections_.load();
    stats.http_errors = http_errors_.load();
    stats.tcp_errors = tcp_errors_.load();
    
    return stats;
}

Metrics::ThroughputStats Metrics::get_throughput_stats() const {
    ThroughputStats stats;
    
    stats.bytes_sent = http_bytes_sent_.load() + tcp_bytes_sent_.load();
    stats.bytes_received = http_bytes_received_.load() + tcp_bytes_received_.load();
    stats.total_bytes = stats.bytes_sent + stats.bytes_received;
    
    // Calculate bytes per second
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    if (elapsed > 0) {
        stats.bytes_per_second = static_cast<double>(stats.total_bytes) / elapsed;
    }
    
    return stats;
}

Metrics::LatencyStats Metrics::get_latency_stats(const std::string& operation) const {
    std::shared_lock lock(latency_mutex_);
    
    auto it = latency_samples_.find(operation);
    if (it == latency_samples_.end() || it->second.empty()) {
        return LatencyStats{};
    }
    
    return calculate_latency_stats(it->second);
}

Metrics::LatencyStats Metrics::calculate_latency_stats(const std::vector<double>& samples) const {
    if (samples.empty()) {
        return LatencyStats{};
    }
    
    LatencyStats stats;
    
    // Sort for percentile calculation
    std::vector<double> sorted_samples = samples;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    // Calculate percentiles
    size_t p50_idx = sorted_samples.size() * 50 / 100;
    size_t p95_idx = sorted_samples.size() * 95 / 100;
    size_t p99_idx = sorted_samples.size() * 99 / 100;
    
    stats.p50_ms = sorted_samples[p50_idx];
    stats.p95_ms = sorted_samples[p95_idx];
    stats.p99_ms = sorted_samples[p99_idx];
    stats.max_ms = sorted_samples.back();
    
    // Calculate mean
    double sum = 0.0;
    for (double sample : samples) {
        sum += sample;
    }
    stats.mean_ms = sum / samples.size();
    
    return stats;
}

// Export functions

std::string Metrics::export_prometheus() const {
    std::ostringstream oss;
    
    auto conn_stats = get_connection_stats();
    auto throughput_stats = get_throughput_stats();
    
    // Connection metrics
    oss << "# HELP protogate_active_tunnels Number of active tunnel connections\n";
    oss << "# TYPE protogate_active_tunnels gauge\n";
    oss << "protogate_active_tunnels " << conn_stats.active_tunnels << "\n\n";
    
    oss << "# HELP protogate_active_http_connections Number of active HTTP connections\n";
    oss << "# TYPE protogate_active_http_connections gauge\n";
    oss << "protogate_active_http_connections " << conn_stats.active_http_connections << "\n\n";
    
    oss << "# HELP protogate_active_tcp_connections Number of active TCP connections\n";
    oss << "# TYPE protogate_active_tcp_connections gauge\n";
    oss << "protogate_active_tcp_connections " << conn_stats.active_tcp_connections << "\n\n";
    
    // Request counters
    oss << "# HELP protogate_http_requests_total Total HTTP requests\n";
    oss << "# TYPE protogate_http_requests_total counter\n";
    oss << "protogate_http_requests_total " << conn_stats.total_http_requests << "\n\n";
    
    oss << "# HELP protogate_http_errors_total Total HTTP errors\n";
    oss << "# TYPE protogate_http_errors_total counter\n";
    oss << "protogate_http_errors_total " << conn_stats.http_errors << "\n\n";
    
    // Throughput metrics
    oss << "# HELP protogate_bytes_sent_total Total bytes sent\n";
    oss << "# TYPE protogate_bytes_sent_total counter\n";
    oss << "protogate_bytes_sent_total " << throughput_stats.bytes_sent << "\n\n";
    
    oss << "# HELP protogate_bytes_received_total Total bytes received\n";
    oss << "# TYPE protogate_bytes_received_total counter\n";
    oss << "protogate_bytes_received_total " << throughput_stats.bytes_received << "\n\n";
    
    oss << "# HELP protogate_throughput_bytes_per_second Current throughput in bytes per second\n";
    oss << "# TYPE protogate_throughput_bytes_per_second gauge\n";
    oss << "protogate_throughput_bytes_per_second " << std::fixed << std::setprecision(2) 
        << throughput_stats.bytes_per_second << "\n\n";
    
    // Latency metrics
    auto http_latency = get_latency_stats("http_request");
    if (http_latency.p50_ms > 0) {
        oss << "# HELP protogate_http_latency_milliseconds HTTP request latency\n";
        oss << "# TYPE protogate_http_latency_milliseconds summary\n";
        oss << "protogate_http_latency_milliseconds{quantile=\"0.5\"} " << http_latency.p50_ms << "\n";
        oss << "protogate_http_latency_milliseconds{quantile=\"0.95\"} " << http_latency.p95_ms << "\n";
        oss << "protogate_http_latency_milliseconds{quantile=\"0.99\"} " << http_latency.p99_ms << "\n\n";
    }
    
    return oss.str();
}

std::string Metrics::export_json() const {
    auto conn_stats = get_connection_stats();
    auto throughput_stats = get_throughput_stats();
    auto http_latency = get_latency_stats("http_request");
    auto tcp_latency = get_latency_stats("tcp_connection");
    
    nlohmann::json j = {
        {"connections", {
            {"active_tunnels", conn_stats.active_tunnels},
            {"active_http_connections", conn_stats.active_http_connections},
            {"active_tcp_connections", conn_stats.active_tcp_connections},
            {"total_http_requests", conn_stats.total_http_requests},
            {"total_tcp_connections", conn_stats.total_tcp_connections},
            {"http_errors", conn_stats.http_errors},
            {"tcp_errors", conn_stats.tcp_errors}
        }},
        {"throughput", {
            {"bytes_sent", throughput_stats.bytes_sent},
            {"bytes_received", throughput_stats.bytes_received},
            {"total_bytes", throughput_stats.total_bytes},
            {"bytes_per_second", throughput_stats.bytes_per_second}
        }},
        {"latency", {
            {"http_request", {
                {"p50_ms", http_latency.p50_ms},
                {"p95_ms", http_latency.p95_ms},
                {"p99_ms", http_latency.p99_ms},
                {"mean_ms", http_latency.mean_ms},
                {"max_ms", http_latency.max_ms}
            }},
            {"tcp_connection", {
                {"p50_ms", tcp_latency.p50_ms},
                {"p95_ms", tcp_latency.p95_ms},
                {"p99_ms", tcp_latency.p99_ms},
                {"mean_ms", tcp_latency.mean_ms},
                {"max_ms", tcp_latency.max_ms}
            }}
        }}
    };
    
    return j.dump(2);
}

bool Metrics::export_azure_monitor(const std::string& resource_id, const std::string& workspace_id, const std::string& workspace_key) {
    try {
        // Get current metrics
        auto conn_stats = get_connection_stats();
        auto throughput_stats = get_throughput_stats();
        auto http_latency = get_latency_stats("http_request");

        // Azure Monitor Custom Metrics API format
        // See: https://docs.microsoft.com/en-us/azure/azure-monitor/essentials/metrics-custom-overview
        nlohmann::json metrics_batch = nlohmann::json::array();

        // Add connection metrics
        metrics_batch.push_back({
            {"time", get_rfc1123_date()},
            {"data", {
                {"baseData", {
                    {"metric", "protogate_active_tunnels"},
                    {"namespace", "ProtoGate"},
                    {"dimNames", nlohmann::json::array()},
                    {"series", nlohmann::json::array({
                        {
                            {"dimValues", nlohmann::json::array()},
                            {"min", conn_stats.active_tunnels},
                            {"max", conn_stats.active_tunnels},
                            {"sum", conn_stats.active_tunnels},
                            {"count", 1}
                        }
                    })}
                }}
            }}
        });

        metrics_batch.push_back({
            {"time", get_rfc1123_date()},
            {"data", {
                {"baseData", {
                    {"metric", "protogate_http_requests_total"},
                    {"namespace", "ProtoGate"},
                    {"dimNames", nlohmann::json::array()},
                    {"series", nlohmann::json::array({
                        {
                            {"dimValues", nlohmann::json::array()},
                            {"min", conn_stats.total_http_requests},
                            {"max", conn_stats.total_http_requests},
                            {"sum", conn_stats.total_http_requests},
                            {"count", 1}
                        }
                    })}
                }}
            }}
        });

        // Add latency metrics
        if (http_latency.p95_ms > 0) {
            metrics_batch.push_back({
                {"time", get_rfc1123_date()},
                {"data", {
                    {"baseData", {
                        {"metric", "protogate_http_latency_p95_ms"},
                        {"namespace", "ProtoGate"},
                        {"dimNames", nlohmann::json::array()},
                        {"series", nlohmann::json::array({
                            {
                                {"dimValues", nlohmann::json::array()},
                                {"min", http_latency.p95_ms},
                                {"max", http_latency.p95_ms},
                                {"sum", http_latency.p95_ms},
                                {"count", 1}
                            }
                        })}
                    }}
                }}
            });
        }

        // Add throughput metrics
        metrics_batch.push_back({
            {"time", get_rfc1123_date()},
            {"data", {
                {"baseData", {
                    {"metric", "protogate_bytes_per_second"},
                    {"namespace", "ProtoGate"},
                    {"dimNames", nlohmann::json::array()},
                    {"series", nlohmann::json::array({
                        {
                            {"dimValues", nlohmann::json::array()},
                            {"min", throughput_stats.bytes_per_second},
                            {"max", throughput_stats.bytes_per_second},
                            {"sum", throughput_stats.bytes_per_second},
                            {"count", 1}
                        }
                    })}
                }}
            }}
        });

        // Prepare Azure Monitor API request
        std::string json_body = metrics_batch.dump();
        std::string content_type = "application/json";
        std::string resource = "/api/logs"; // Azure Monitor Ingestion API endpoint
        std::string date = get_rfc1123_date();
        
        // Build signature string
        std::string signature_string = "POST\n" + 
                                     std::to_string(json_body.length()) + "\n" +
                                     content_type + "\n" +
                                     "x-ms-date:" + date + "\n" +
                                     resource;
        
        // Compute HMAC-SHA256 signature
        std::string signature = compute_hmac_sha256(workspace_key, signature_string);
        
        // Build Authorization header
        std::string authorization = "SharedKey " + workspace_id + ":" + signature;
        
        // TODO: Actually send the HTTP POST request to Azure Monitor
        // For now, just log that we would send it
        Logger::instance().info("Would upload metrics to Azure Monitor (HTTP POST not implemented yet)");
        Logger::instance().debug("Resource ID: " + resource_id);
        Logger::instance().debug("Authorization: " + authorization);
        Logger::instance().debug("Metrics count: " + std::to_string(metrics_batch.size()));
        
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to export metrics to Azure Monitor: " + std::string(e.what()));
        return false;
    }
}

std::string Metrics::get_rfc1123_date() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm gmt{};
    gmtime_r(&now_time, &gmt);
    
    char buffer[128];
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
    return std::string(buffer);
}

std::string Metrics::compute_hmac_sha256(const std::string& key_base64, const std::string& data) const {
    // Decode base64 workspace key
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* bmem = BIO_new_mem_buf(key_base64.data(), static_cast<int>(key_base64.length()));
    bmem = BIO_push(b64, bmem);
    
    std::vector<unsigned char> decoded_key(256);
    int decoded_len = BIO_read(bmem, decoded_key.data(), static_cast<int>(decoded_key.size()));
    BIO_free_all(bmem);
    
    if (decoded_len <= 0) {
        throw std::runtime_error("Failed to decode base64 workspace key");
    }
    
    // Compute HMAC-SHA256
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;
    
    HMAC(EVP_sha256(), decoded_key.data(), decoded_len,
         reinterpret_cast<const unsigned char*>(data.data()), data.length(),
         hmac, &hmac_len);
    
    // Encode result as base64
    BIO* b64_out = BIO_new(BIO_f_base64());
    BIO_set_flags(b64_out, BIO_FLAGS_BASE64_NO_NL);
    BIO* bmem_out = BIO_new(BIO_s_mem());
    BIO_push(b64_out, bmem_out);
    BIO_write(b64_out, hmac, static_cast<int>(hmac_len));
    BIO_flush(b64_out);
    
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64_out, &bptr);
    std::string result(bptr->data, bptr->length);
    
    BIO_free_all(b64_out);
    
    return result;
}

void Metrics::reset() {
    {
        std::unique_lock lock(tunnels_mutex_);
        active_tunnels_.clear();
    }
    
    {
        std::unique_lock lock(http_mutex_);
        active_http_requests_.clear();
    }
    
    {
        std::unique_lock lock(tcp_mutex_);
        active_tcp_connections_.clear();
    }
    
    {
        std::unique_lock lock(latency_mutex_);
        latency_samples_.clear();
    }
    
    total_http_requests_ = 0;
    http_errors_ = 0;
    http_bytes_sent_ = 0;
    http_bytes_received_ = 0;
    
    total_tcp_connections_ = 0;
    tcp_errors_ = 0;
    tcp_bytes_sent_ = 0;
    tcp_bytes_received_ = 0;
    
    start_time_ = std::chrono::steady_clock::now();
    
    Logger::instance().info("Metrics reset");
}

}  // namespace observability
}  // namespace protogate
