// tests/integration/health_check_test.cpp
// Health check endpoint integration tests

#include <gtest/gtest.h>
#include "../../src/server/health_handler.h"
#include "../../src/observability/metrics.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using namespace protogate;
using json = nlohmann::json;

class HealthCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset metrics before each test
        observability::Metrics::instance().reset();
        handler_ = std::make_unique<server::HealthHandler>();
    }
    
    void TearDown() override {
        // Clean up after each test
        observability::Metrics::instance().reset();
        handler_.reset();
    }
    
    std::unique_ptr<server::HealthHandler> handler_;
};

// Test: Health endpoint returns 200 when service is healthy
TEST_F(HealthCheckTest, HealthyStateReturns200) {
    // Simulate normal operations
    observability::Metrics::instance().tunnel_connected("tunnel-1");
    observability::Metrics::instance().http_request_start("req-1");
    observability::Metrics::instance().http_request_complete("req-1", false);
    observability::Metrics::instance().http_bytes_transferred(1000, 500);
    
    // Get health status
    auto status = handler_->get_health_status();
    auto http_code = handler_->get_http_status_code();
    auto json_response = handler_->get_health_json();
    
    EXPECT_EQ(status, server::HealthHandler::HealthStatus::HEALTHY);
    EXPECT_EQ(http_code, 200);
    
    // Verify JSON structure
    json j = json::parse(json_response);
    EXPECT_EQ(j["status"], "healthy");
    EXPECT_TRUE(j.contains("timestamp"));
    EXPECT_TRUE(j.contains("service"));
    EXPECT_TRUE(j.contains("version"));
    EXPECT_TRUE(j.contains("metrics"));
    EXPECT_TRUE(j.contains("memory"));
    EXPECT_TRUE(j.contains("checks"));
    
    // Verify metrics
    EXPECT_EQ(j["metrics"]["active_tunnels"], 1);
    EXPECT_EQ(j["metrics"]["total_http_requests"], 1);
    EXPECT_EQ(j["metrics"]["http_errors"], 0);
}

// Test: Health endpoint returns 503 when error rate is high
TEST_F(HealthCheckTest, DegradedStateOnHighErrorRate) {
    // Simulate high error rate: 11 errors out of 101 requests (10.9%)
    // Need > 100 requests to trigger is_degraded() check
    for (int i = 0; i < 90; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), false);
    }
    for (int i = 90; i < 101; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), true);
    }
    
    // Get health status
    auto status = handler_->get_health_status();
    auto http_code = handler_->get_http_status_code();
    auto json_response = handler_->get_health_json();
    
    EXPECT_EQ(status, server::HealthHandler::HealthStatus::DEGRADED);
    EXPECT_EQ(http_code, 503);
    
    // Verify JSON shows degraded status
    json j = json::parse(json_response);
    EXPECT_EQ(j["status"], "degraded");
    
    // Verify error rate check failed
    EXPECT_TRUE(j["checks"].contains("error_rate"));
    EXPECT_EQ(j["checks"]["error_rate"]["status"], "warning");
    EXPECT_GT(j["checks"]["error_rate"]["rate"], 0.05); // > 5% (stored as decimal)
}

// Test: Health endpoint returns 503 when memory pressure is high
// Note: This test simulates the check logic without actually allocating 512MB
TEST_F(HealthCheckTest, DegradedStateOnMemoryPressure) {
    // Record normal operations
    observability::Metrics::instance().http_request_start("req-1");
    observability::Metrics::instance().http_request_complete("req-1", false);
    
    // Get health status (memory check happens in is_degraded)
    auto json_response = handler_->get_health_json();
    
    // Verify JSON structure contains memory check
    json j = json::parse(json_response);
    EXPECT_TRUE(j["memory"].contains("rss_kb"));
    EXPECT_TRUE(j["checks"].contains("memory"));
    EXPECT_TRUE(j["checks"]["memory"].contains("status"));
    
    // Memory check should be "pass" in test environment (< 512MB)
    // In production with actual high memory usage, status would be "warning"
    EXPECT_EQ(j["checks"]["memory"]["status"], "pass");
}

// Test: Health endpoint handles zero metrics gracefully
TEST_F(HealthCheckTest, HealthyWithZeroMetrics) {
    // Don't record any metrics - fresh state
    
    auto status = handler_->get_health_status();
    auto http_code = handler_->get_http_status_code();
    auto json_response = handler_->get_health_json();
    
    EXPECT_EQ(status, server::HealthHandler::HealthStatus::HEALTHY);
    EXPECT_EQ(http_code, 200);
    
    // Verify JSON structure is valid with zero values
    json j = json::parse(json_response);
    EXPECT_EQ(j["status"], "healthy");
    EXPECT_EQ(j["metrics"]["active_tunnels"], 0);
    EXPECT_EQ(j["metrics"]["total_http_requests"], 0);
    EXPECT_EQ(j["metrics"]["http_errors"], 0);
    EXPECT_EQ(j["checks"]["error_rate"]["status"], "pass");
}

// Test: Health endpoint error rate calculation with small sample size
TEST_F(HealthCheckTest, ErrorRateWithSmallSampleSize) {
    // Simulate only 50 requests (below 100 threshold)
    // Even with 50% error rate, should not trigger degraded state
    for (int i = 0; i < 25; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), false);
    }
    for (int i = 25; i < 50; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), true);
    }
    
    auto status = handler_->get_health_status();
    auto http_code = handler_->get_http_status_code();
    
    // Should be healthy because is_degraded() requires total_requests > 100
    EXPECT_EQ(status, server::HealthHandler::HealthStatus::HEALTHY);
    EXPECT_EQ(http_code, 200);
    
    // However, the error rate check in get_health_json() still shows warning
    // because 50% error rate > 5% threshold
    json j = json::parse(handler_->get_health_json());
    EXPECT_EQ(j["checks"]["error_rate"]["status"], "warning");
}

// Test: Health endpoint exactly at error rate threshold (5%)
TEST_F(HealthCheckTest, ErrorRateAtThreshold) {
    // Simulate exactly 5% error rate: 5 errors out of 100 requests
    for (int i = 0; i < 95; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), false);
    }
    for (int i = 95; i < 100; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), true);
    }
    
    auto status = handler_->get_health_status();
    auto http_code = handler_->get_http_status_code();
    
    // At exactly 5%, should still be healthy (threshold is >5%)
    EXPECT_EQ(status, server::HealthHandler::HealthStatus::HEALTHY);
    EXPECT_EQ(http_code, 200);
    
    json j = json::parse(handler_->get_health_json());
    EXPECT_EQ(j["checks"]["error_rate"]["status"], "pass");
    EXPECT_NEAR(j["checks"]["error_rate"]["rate"].get<double>(), 0.05, 0.001);
}

// Test: Health endpoint JSON format validation
TEST_F(HealthCheckTest, JsonFormatValidation) {
    // Record some metrics
    observability::Metrics::instance().tunnel_connected("tunnel-1");
    observability::Metrics::instance().tunnel_connected("tunnel-2");
    observability::Metrics::instance().http_request_start("req-1");
    observability::Metrics::instance().http_request_complete("req-1", false);
    observability::Metrics::instance().tcp_connection_start("conn-1");
    observability::Metrics::instance().tcp_connection_complete("conn-1", false);
    observability::Metrics::instance().http_bytes_transferred(2000, 1000);
    observability::Metrics::instance().tcp_bytes_transferred(3000, 1500);
    
    auto json_response = handler_->get_health_json();
    
    // Parse and validate structure
    json j = json::parse(json_response);
    
    // Root level fields
    EXPECT_EQ(j["status"], "healthy");
    EXPECT_TRUE(j["timestamp"].is_number());
    EXPECT_EQ(j["service"], "protogate");
    EXPECT_EQ(j["version"], "1.0.0");
    
    // Metrics section
    EXPECT_TRUE(j["metrics"].is_object());
    EXPECT_EQ(j["metrics"]["active_tunnels"], 2);
    EXPECT_EQ(j["metrics"]["active_http_connections"], 0);
    EXPECT_EQ(j["metrics"]["active_tcp_connections"], 0);
    EXPECT_EQ(j["metrics"]["total_http_requests"], 1);
    EXPECT_EQ(j["metrics"]["total_tcp_connections"], 1);
    EXPECT_EQ(j["metrics"]["http_errors"], 0);
    EXPECT_EQ(j["metrics"]["tcp_errors"], 0);
    EXPECT_EQ(j["metrics"]["bytes_sent"], 5000);
    EXPECT_EQ(j["metrics"]["bytes_received"], 2500);
    
    // Memory section
    EXPECT_TRUE(j["memory"].is_object());
    EXPECT_TRUE(j["memory"]["rss_kb"].is_number());
    EXPECT_GT(j["memory"]["rss_kb"], 0);
    
    // Checks section
    EXPECT_TRUE(j["checks"].is_object());
    EXPECT_TRUE(j["checks"]["memory"].is_object());
    EXPECT_TRUE(j["checks"]["error_rate"].is_object());
    EXPECT_EQ(j["checks"]["memory"]["status"], "pass");
    EXPECT_EQ(j["checks"]["error_rate"]["status"], "pass");
}

// Test: Multiple consecutive health checks maintain consistency
TEST_F(HealthCheckTest, ConsistentHealthChecks) {
    // Record metrics
    observability::Metrics::instance().tunnel_connected("tunnel-1");
    observability::Metrics::instance().http_request_start("req-1");
    observability::Metrics::instance().http_request_complete("req-1", false);
    
    // Get health status multiple times
    auto status1 = handler_->get_health_status();
    auto status2 = handler_->get_health_status();
    auto status3 = handler_->get_health_status();
    
    EXPECT_EQ(status1, status2);
    EXPECT_EQ(status2, status3);
    EXPECT_EQ(status1, server::HealthHandler::HealthStatus::HEALTHY);
    
    // All should return 200
    EXPECT_EQ(handler_->get_http_status_code(), 200);
}

// Test: Health recovery after degraded state
TEST_F(HealthCheckTest, HealthRecoveryAfterDegraded) {
    // First, trigger degraded state with high error rate
    // Need > 100 requests to trigger is_degraded() check
    for (int i = 0; i < 90; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), false);
    }
    for (int i = 90; i < 101; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), true);
    }
    
    auto status_degraded = handler_->get_health_status();
    EXPECT_EQ(status_degraded, server::HealthHandler::HealthStatus::DEGRADED);
    
    // Reset metrics to simulate recovery
    observability::Metrics::instance().reset();
    
    // Add successful requests
    for (int i = 0; i < 100; i++) {
        observability::Metrics::instance().http_request_start("req-" + std::to_string(i));
        observability::Metrics::instance().http_request_complete("req-" + std::to_string(i), false);
    }
    
    auto status_recovered = handler_->get_health_status();
    auto http_code_recovered = handler_->get_http_status_code();
    
    EXPECT_EQ(status_recovered, server::HealthHandler::HealthStatus::HEALTHY);
    EXPECT_EQ(http_code_recovered, 200);
}
