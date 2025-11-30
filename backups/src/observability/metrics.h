#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <vector>

namespace protogate {
namespace observability {

/**
 * @brief Metrics collection for monitoring and observability
 * 
 * Responsibilities:
 * - Track active tunnel connections
 * - Record throughput (bytes sent/received)
 * - Measure latency (p50, p95, p99)
 * - Count HTTP/TCP requests and errors
 * - Export metrics for Azure Monitor or Prometheus
 */
class Metrics {
public:
    /**
     * @brief Latency percentiles
     */
    struct LatencyStats {
        double p50_ms = 0.0;  // Median
        double p95_ms = 0.0;  // 95th percentile
        double p99_ms = 0.0;  // 99th percentile
        double mean_ms = 0.0; // Average
        double max_ms = 0.0;  // Maximum
    };

    /**
     * @brief Throughput statistics
     */
    struct ThroughputStats {
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t total_bytes = 0;
        double bytes_per_second = 0.0;
    };

    /**
     * @brief Connection statistics
     */
    struct ConnectionStats {
        uint32_t active_tunnels = 0;
        uint32_t active_http_connections = 0;
        uint32_t active_tcp_connections = 0;
        uint64_t total_http_requests = 0;
        uint64_t total_tcp_connections = 0;
        uint64_t http_errors = 0;
        uint64_t tcp_errors = 0;
    };

    /**
     * @brief Get singleton instance
     */
    static Metrics& instance();

    // Tunnel metrics
    void tunnel_connected(const std::string& tunnel_id);
    void tunnel_disconnected(const std::string& tunnel_id);
    uint32_t get_active_tunnel_count() const;

    // HTTP metrics
    void http_request_start(const std::string& request_id);
    void http_request_complete(const std::string& request_id, bool error = false);
    void http_bytes_transferred(uint64_t bytes_sent, uint64_t bytes_received);

    // TCP metrics
    void tcp_connection_start(const std::string& connection_id);
    void tcp_connection_complete(const std::string& connection_id, bool error = false);
    void tcp_bytes_transferred(uint64_t bytes_sent, uint64_t bytes_received);

    // Latency tracking
    void record_latency(const std::string& operation, std::chrono::microseconds latency);

    // Statistics retrieval
    ConnectionStats get_connection_stats() const;
    ThroughputStats get_throughput_stats() const;
    LatencyStats get_latency_stats(const std::string& operation) const;

    /**
     * @brief Export metrics in Prometheus format
     */
    std::string export_prometheus() const;

    /**
     * @brief Export metrics in JSON format (for Azure Monitor)
     */
    std::string export_json() const;

    /**
     * @brief Upload metrics to Azure Monitor Custom Metrics API
     * @param resource_id Azure resource ID (e.g. /subscriptions/.../resourceGroups/.../providers/Microsoft.ContainerApps/containerApps/protogate)
     * @param workspace_id Azure Monitor workspace ID for authentication
     * @param workspace_key Azure Monitor workspace shared key (base64 encoded)
     * @return true if upload successful, false otherwise
     */
    bool export_azure_monitor(const std::string& resource_id, const std::string& workspace_id, const std::string& workspace_key);

    /**
     * @brief Reset all metrics (for testing)
     */
    void reset();

private:
    Metrics() = default;
    ~Metrics() = default;
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;

    /**
     * @brief Calculate percentiles from latency samples
     */
    LatencyStats calculate_latency_stats(const std::vector<double>& samples) const;

    /**
     * @brief Get RFC1123 formatted timestamp for Azure API
     */
    std::string get_rfc1123_date() const;

    /**
     * @brief Compute HMAC-SHA256 signature for Azure API authentication
     */
    std::string compute_hmac_sha256(const std::string& key_base64, const std::string& data) const;

    // Tunnel tracking
    mutable std::shared_mutex tunnels_mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> active_tunnels_;

    // HTTP tracking
    mutable std::shared_mutex http_mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> active_http_requests_;
    std::atomic<uint64_t> total_http_requests_{0};
    std::atomic<uint64_t> http_errors_{0};
    std::atomic<uint64_t> http_bytes_sent_{0};
    std::atomic<uint64_t> http_bytes_received_{0};

    // TCP tracking
    mutable std::shared_mutex tcp_mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> active_tcp_connections_;
    std::atomic<uint64_t> total_tcp_connections_{0};
    std::atomic<uint64_t> tcp_errors_{0};
    std::atomic<uint64_t> tcp_bytes_sent_{0};
    std::atomic<uint64_t> tcp_bytes_received_{0};

    // Latency tracking (operation -> samples in milliseconds)
    mutable std::shared_mutex latency_mutex_;
    std::unordered_map<std::string, std::vector<double>> latency_samples_;
    static constexpr size_t MAX_LATENCY_SAMPLES = 1000;

    // Start time for rate calculations
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
};

}  // namespace observability
}  // namespace protogate
