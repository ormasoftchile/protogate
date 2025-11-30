#include "observability/metrics.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using namespace protogate::observability;

class MetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset metrics before each test
        Metrics::instance().reset();
    }
    
    void TearDown() override {
        // Clean up after each test
        Metrics::instance().reset();
    }
};

TEST_F(MetricsTest, InitialState) {
    auto conn_stats = Metrics::instance().get_connection_stats();
    
    EXPECT_EQ(conn_stats.active_tunnels, 0);
    EXPECT_EQ(conn_stats.active_http_connections, 0);
    EXPECT_EQ(conn_stats.active_tcp_connections, 0);
    EXPECT_EQ(conn_stats.total_http_requests, 0);
    EXPECT_EQ(conn_stats.total_tcp_connections, 0);
    EXPECT_EQ(conn_stats.http_errors, 0);
    EXPECT_EQ(conn_stats.tcp_errors, 0);
}

TEST_F(MetricsTest, TunnelConnections) {
    Metrics::instance().tunnel_connected("tunnel-1");
    EXPECT_EQ(Metrics::instance().get_active_tunnel_count(), 1);
    
    Metrics::instance().tunnel_connected("tunnel-2");
    EXPECT_EQ(Metrics::instance().get_active_tunnel_count(), 2);
    
    Metrics::instance().tunnel_disconnected("tunnel-1");
    EXPECT_EQ(Metrics::instance().get_active_tunnel_count(), 1);
    
    Metrics::instance().tunnel_disconnected("tunnel-2");
    EXPECT_EQ(Metrics::instance().get_active_tunnel_count(), 0);
}

TEST_F(MetricsTest, HttpMetrics) {
    Metrics::instance().http_request_start("req-1");
    auto conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_EQ(conn_stats.active_http_connections, 1);
    
    Metrics::instance().http_request_complete("req-1", false);
    conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_EQ(conn_stats.active_http_connections, 0);
    EXPECT_EQ(conn_stats.total_http_requests, 1);
    EXPECT_EQ(conn_stats.http_errors, 0);
    
    // Test error tracking
    Metrics::instance().http_request_start("req-2");
    Metrics::instance().http_request_complete("req-2", true);
    conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_EQ(conn_stats.total_http_requests, 2);
    EXPECT_EQ(conn_stats.http_errors, 1);
}

TEST_F(MetricsTest, TcpMetrics) {
    Metrics::instance().tcp_connection_start("conn-1");
    auto conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_EQ(conn_stats.active_tcp_connections, 1);
    
    Metrics::instance().tcp_connection_complete("conn-1", false);
    conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_EQ(conn_stats.active_tcp_connections, 0);
    EXPECT_EQ(conn_stats.total_tcp_connections, 1);
    EXPECT_EQ(conn_stats.tcp_errors, 0);
}

TEST_F(MetricsTest, ThroughputMetrics) {
    Metrics::instance().http_bytes_transferred(1000, 500);
    Metrics::instance().tcp_bytes_transferred(2000, 1500);
    
    auto throughput_stats = Metrics::instance().get_throughput_stats();
    EXPECT_EQ(throughput_stats.bytes_sent, 3000);
    EXPECT_EQ(throughput_stats.bytes_received, 2000);
    EXPECT_EQ(throughput_stats.total_bytes, 5000);
}

TEST_F(MetricsTest, LatencyPercentiles_EmptySamples) {
    // Test with no samples
    auto latency_stats = Metrics::instance().get_latency_stats("http_request");
    EXPECT_EQ(latency_stats.p50_ms, 0.0);
    EXPECT_EQ(latency_stats.p95_ms, 0.0);
    EXPECT_EQ(latency_stats.p99_ms, 0.0);
    EXPECT_EQ(latency_stats.mean_ms, 0.0);
    EXPECT_EQ(latency_stats.max_ms, 0.0);
}

TEST_F(MetricsTest, LatencyPercentiles_SingleSample) {
    // Test with one sample
    Metrics::instance().record_latency("http_request", std::chrono::microseconds(100000)); // 100ms
    
    auto latency_stats = Metrics::instance().get_latency_stats("http_request");
    EXPECT_DOUBLE_EQ(latency_stats.p50_ms, 100.0);
    EXPECT_DOUBLE_EQ(latency_stats.p95_ms, 100.0);
    EXPECT_DOUBLE_EQ(latency_stats.p99_ms, 100.0);
    EXPECT_DOUBLE_EQ(latency_stats.mean_ms, 100.0);
    EXPECT_DOUBLE_EQ(latency_stats.max_ms, 100.0);
}

TEST_F(MetricsTest, LatencyPercentiles_MultipleSamples) {
    // Record 100 samples with values from 1ms to 100ms
    for (int i = 1; i <= 100; i++) {
        Metrics::instance().record_latency("http_request", std::chrono::microseconds(i * 1000));
    }
    
    auto latency_stats = Metrics::instance().get_latency_stats("http_request");
    
    // p50 should be around 50ms (median of 1-100)
    EXPECT_NEAR(latency_stats.p50_ms, 50.0, 1.0);
    
    // p95 should be around 95ms
    EXPECT_NEAR(latency_stats.p95_ms, 95.0, 1.0);
    
    // p99 should be around 99ms
    EXPECT_NEAR(latency_stats.p99_ms, 99.0, 1.0);
    
    // Mean should be around 50.5ms
    EXPECT_NEAR(latency_stats.mean_ms, 50.5, 1.0);
    
    // Max should be 100ms
    EXPECT_DOUBLE_EQ(latency_stats.max_ms, 100.0);
}

TEST_F(MetricsTest, LatencyPercentiles_SkewedDistribution) {
    // Test with 90 fast requests (1-10ms) and 10 slow requests (100-200ms)
    for (int i = 1; i <= 90; i++) {
        Metrics::instance().record_latency("http_request", std::chrono::microseconds(i * 100)); // 0.1-9ms
    }
    for (int i = 1; i <= 10; i++) {
        Metrics::instance().record_latency("http_request", std::chrono::microseconds(100000 + i * 10000)); // 100-200ms
    }
    
    auto latency_stats = Metrics::instance().get_latency_stats("http_request");
    
    // p50 should be in the fast range (< 10ms)
    EXPECT_LT(latency_stats.p50_ms, 10.0);
    
    // p95 should be in the slow range (> 100ms)
    EXPECT_GT(latency_stats.p95_ms, 100.0);
    
    // p99 should be in the slow range (> 100ms)
    EXPECT_GT(latency_stats.p99_ms, 100.0);
}

TEST_F(MetricsTest, ExportPrometheus) {
    // Record some metrics
    Metrics::instance().tunnel_connected("tunnel-1");
    Metrics::instance().http_request_start("req-1");
    Metrics::instance().http_request_complete("req-1", false);
    Metrics::instance().http_bytes_transferred(1000, 500);
    
    // Export to Prometheus format
    std::string prometheus_output = Metrics::instance().export_prometheus();
    
    // Verify output contains expected metrics
    EXPECT_NE(prometheus_output.find("protogate_active_tunnels"), std::string::npos);
    EXPECT_NE(prometheus_output.find("protogate_http_requests_total"), std::string::npos);
    EXPECT_NE(prometheus_output.find("protogate_bytes_sent_total"), std::string::npos);
    EXPECT_NE(prometheus_output.find("protogate_bytes_received_total"), std::string::npos);
}

TEST_F(MetricsTest, ExportJson) {
    // Record some metrics
    Metrics::instance().tunnel_connected("tunnel-1");
    Metrics::instance().http_request_start("req-1");
    Metrics::instance().http_request_complete("req-1", false);
    Metrics::instance().http_bytes_transferred(1000, 500);
    Metrics::instance().record_latency("http_request", std::chrono::microseconds(50000)); // 50ms
    
    // Export to JSON format
    std::string json_output = Metrics::instance().export_json();
    
    // Verify output is valid JSON
    EXPECT_NO_THROW({
        auto j = nlohmann::json::parse(json_output);
        EXPECT_TRUE(j.contains("connections"));
        EXPECT_TRUE(j.contains("throughput"));
        EXPECT_TRUE(j.contains("latency"));
    });
}

TEST_F(MetricsTest, LatencySampleLimit) {
    // Record more than MAX_LATENCY_SAMPLES (1000) samples
    for (int i = 0; i < 1500; i++) {
        Metrics::instance().record_latency("http_request", std::chrono::microseconds(i * 100));
    }
    
    // Should still get valid percentile calculations (samples should be capped at 1000)
    auto latency_stats = Metrics::instance().get_latency_stats("http_request");
    EXPECT_GT(latency_stats.p50_ms, 0.0);
    EXPECT_GT(latency_stats.p95_ms, 0.0);
    EXPECT_GT(latency_stats.p99_ms, 0.0);
}

TEST_F(MetricsTest, ResetClearsAllMetrics) {
    // Record various metrics
    Metrics::instance().tunnel_connected("tunnel-1");
    Metrics::instance().http_request_start("req-1");
    Metrics::instance().http_bytes_transferred(1000, 500);
    Metrics::instance().record_latency("http_request", std::chrono::microseconds(50000));
    
    // Verify metrics are recorded
    auto conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_GT(conn_stats.active_tunnels, 0);
    
    // Reset and verify everything is cleared
    Metrics::instance().reset();
    
    conn_stats = Metrics::instance().get_connection_stats();
    EXPECT_EQ(conn_stats.active_tunnels, 0);
    EXPECT_EQ(conn_stats.active_http_connections, 0);
    EXPECT_EQ(conn_stats.total_http_requests, 0);
    
    auto throughput_stats = Metrics::instance().get_throughput_stats();
    EXPECT_EQ(throughput_stats.bytes_sent, 0);
    EXPECT_EQ(throughput_stats.bytes_received, 0);
    
    auto latency_stats = Metrics::instance().get_latency_stats("http_request");
    EXPECT_EQ(latency_stats.p50_ms, 0.0);
}
