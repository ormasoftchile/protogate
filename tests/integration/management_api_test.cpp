// tests/integration/management_api_test.cpp
// Management API integration tests for tunnel CRUD operations

#include <gtest/gtest.h>
#include "../../src/api/router.h"
#include "../../src/api/tunnels_handler.h"
#include "../../src/storage/cache.h"
#include "../../src/storage/keyvault_client.h"
#include "../../src/agent/agent_registry.h"
#include "../../src/models/tunnel.h"
#include "../../src/models/auth_token.h"
#include <nlohmann/json.hpp>
#include <memory>

using namespace protogate;
using json = nlohmann::json;

class ManagementAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize components
        tunnel_cache_ = std::make_shared<storage::Cache<std::string, models::Tunnel>>(std::chrono::seconds(3600));
        token_cache_ = std::make_shared<storage::Cache<std::string, models::AuthToken>>(std::chrono::seconds(3600));
        keyvault_client_ = std::make_shared<storage::KeyVaultClient>("https://test-vault.vault.azure.net");
        agent_registry_ = std::make_shared<agent::AgentRegistry>(100);
        
        handler_ = std::make_unique<api::TunnelsHandler>(
            tunnel_cache_,
            token_cache_,
            keyvault_client_,
            agent_registry_
        );
        
        router_ = std::make_unique<api::Router>();
        handler_->register_routes(*router_);
    }
    
    void TearDown() override {
        handler_.reset();
        router_.reset();
    }
    
    api::HttpResponse execute_request(const std::string& method, const std::string& path, const std::string& body = "") {
        api::HttpRequest request;
        request.method = method;
        request.path = path;
        request.body = body;
        request.remote_ip = "127.0.0.1";
        
        api::HttpResponse response;
        router_->route(request, response);
        return response;
    }
    
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache_;
    std::shared_ptr<storage::Cache<std::string, models::AuthToken>> token_cache_;
    std::shared_ptr<storage::KeyVaultClient> keyvault_client_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    std::unique_ptr<api::TunnelsHandler> handler_;
    std::unique_ptr<api::Router> router_;
};

// Test: Create tunnel with valid configuration
TEST_F(ManagementAPITest, CreateTunnelSuccess) {
    json request_body;
    request_body["tunnel_id"] = "test-tunnel-1";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "HTTP";
    request_body["ip_allowlist"] = json::array({"192.168.1.0/24", "10.0.0.0/8"});
    
    auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    
    EXPECT_EQ(response.status_code, 201);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["tunnel_id"], "test-tunnel-1");
    EXPECT_EQ(response_json["target_host"], "localhost");
    EXPECT_EQ(response_json["target_port"], 8080);
    EXPECT_EQ(response_json["protocol"], "HTTP");
    EXPECT_TRUE(response_json.contains("token"));
    EXPECT_FALSE(response_json["token"].get<std::string>().empty());
    EXPECT_TRUE(response_json.contains("token_expires_at"));
    
    // Verify tunnel is in cache
    auto tunnel = tunnel_cache_->get("test-tunnel-1");
    ASSERT_TRUE(tunnel.has_value());
    EXPECT_EQ(tunnel->tunnel_id, "test-tunnel-1");
    EXPECT_EQ(tunnel->target_host, "localhost");
    EXPECT_EQ(tunnel->target_port, 8080);
    
    // Verify token is in cache
    auto token = token_cache_->get("test-tunnel-1");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(token->tunnel_id, "test-tunnel-1");
}

// Test: Create tunnel with TCP protocol
TEST_F(ManagementAPITest, CreateTCPTunnel) {
    json request_body;
    request_body["tunnel_id"] = "printer-tunnel";
    request_body["target_host"] = "192.168.1.100";
    request_body["target_port"] = 9100;
    request_body["protocol"] = "TCP";
    
    auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    
    EXPECT_EQ(response.status_code, 201);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["protocol"], "TCP");
    EXPECT_EQ(response_json["target_port"], 9100);
}

// Test: Create tunnel with missing required fields
TEST_F(ManagementAPITest, CreateTunnelMissingFields) {
    json request_body;
    request_body["tunnel_id"] = "incomplete-tunnel";
    // Missing target_host and target_port
    
    auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    
    EXPECT_EQ(response.status_code, 400);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json.contains("error"));
}

// Test: Create tunnel with invalid port
TEST_F(ManagementAPITest, CreateTunnelInvalidPort) {
    json request_body;
    request_body["tunnel_id"] = "invalid-port-tunnel";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 0; // Invalid port (must be 1-65535)
    request_body["protocol"] = "HTTP";
    
    auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    
    EXPECT_EQ(response.status_code, 400);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json["error"].get<std::string>().find("port") != std::string::npos);
}

// Test: Create duplicate tunnel
TEST_F(ManagementAPITest, CreateDuplicateTunnel) {
    // Create first tunnel
    json request_body;
    request_body["tunnel_id"] = "duplicate-test";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "HTTP";
    
    auto response1 = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    EXPECT_EQ(response1.status_code, 201);
    
    // Try to create duplicate
    auto response2 = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    EXPECT_EQ(response2.status_code, 409);
    
    json response_json = json::parse(response2.body);
    EXPECT_TRUE(response_json["error"].get<std::string>().find("already exists") != std::string::npos);
}

// Test: List all tunnels (empty)
TEST_F(ManagementAPITest, ListTunnelsEmpty) {
    auto response = execute_request("GET", "/api/v1/tunnels");
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json.is_array());
    EXPECT_EQ(response_json.size(), 0);
}

// Test: List all tunnels (multiple)
TEST_F(ManagementAPITest, ListMultipleTunnels) {
    // Create multiple tunnels
    for (int i = 1; i <= 3; ++i) {
        json request_body;
        request_body["tunnel_id"] = "tunnel-" + std::to_string(i);
        request_body["target_host"] = "localhost";
        request_body["target_port"] = 8080 + i;
        request_body["protocol"] = "HTTP";
        
        auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
        EXPECT_EQ(response.status_code, 201);
    }
    
    // List all tunnels
    auto response = execute_request("GET", "/api/v1/tunnels");
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json.is_array());
    EXPECT_EQ(response_json.size(), 3);
    
    // Verify each tunnel
    std::set<std::string> tunnel_ids;
    for (const auto& tunnel : response_json) {
        tunnel_ids.insert(tunnel["tunnel_id"].get<std::string>());
    }
    
    EXPECT_TRUE(tunnel_ids.count("tunnel-1") > 0);
    EXPECT_TRUE(tunnel_ids.count("tunnel-2") > 0);
    EXPECT_TRUE(tunnel_ids.count("tunnel-3") > 0);
}

// Test: Get single tunnel by ID
TEST_F(ManagementAPITest, GetTunnelByID) {
    // Create tunnel
    json request_body;
    request_body["tunnel_id"] = "get-test";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 9000;
    request_body["protocol"] = "HTTP";
    
    auto create_response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    EXPECT_EQ(create_response.status_code, 201);
    
    // Get tunnel by ID
    auto response = execute_request("GET", "/api/v1/tunnels/get-test");
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["tunnel_id"], "get-test");
    EXPECT_EQ(response_json["target_host"], "localhost");
    EXPECT_EQ(response_json["target_port"], 9000);
    EXPECT_EQ(response_json["protocol"], "HTTP");
    EXPECT_FALSE(response_json.contains("token")); // Token not returned on GET
}

// Test: Get non-existent tunnel
TEST_F(ManagementAPITest, GetNonExistentTunnel) {
    auto response = execute_request("GET", "/api/v1/tunnels/does-not-exist");
    
    EXPECT_EQ(response.status_code, 404);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json.contains("error"));
}

// Test: Delete tunnel
TEST_F(ManagementAPITest, DeleteTunnel) {
    // Create tunnel
    json request_body;
    request_body["tunnel_id"] = "delete-test";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "HTTP";
    
    auto create_response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    EXPECT_EQ(create_response.status_code, 201);
    
    // Verify tunnel exists
    EXPECT_TRUE(tunnel_cache_->get("delete-test").has_value());
    
    // Delete tunnel
    auto response = execute_request("DELETE", "/api/v1/tunnels/delete-test");
    
    EXPECT_EQ(response.status_code, 204);
    EXPECT_TRUE(response.body.empty());
    
    // Verify tunnel is removed
    EXPECT_FALSE(tunnel_cache_->get("delete-test").has_value());
    EXPECT_FALSE(token_cache_->get("delete-test").has_value());
}

// Test: Delete non-existent tunnel
TEST_F(ManagementAPITest, DeleteNonExistentTunnel) {
    auto response = execute_request("DELETE", "/api/v1/tunnels/does-not-exist");
    
    EXPECT_EQ(response.status_code, 404);
}

// Test: Invalid tunnel ID format
TEST_F(ManagementAPITest, InvalidTunnelIDFormat) {
    json request_body;
    request_body["tunnel_id"] = "invalid tunnel with spaces";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "HTTP";
    
    auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    
    EXPECT_EQ(response.status_code, 400);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json["error"].get<std::string>().find("alphanumeric") != std::string::npos);
}

// Test: Invalid protocol
TEST_F(ManagementAPITest, InvalidProtocol) {
    json request_body;
    request_body["tunnel_id"] = "invalid-protocol";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "INVALID";
    
    auto response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    
    EXPECT_EQ(response.status_code, 400);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json["error"].get<std::string>().find("protocol") != std::string::npos);
}

// Test: Malformed JSON
TEST_F(ManagementAPITest, MalformedJSON) {
    auto response = execute_request("POST", "/api/v1/tunnels", "{invalid json");
    
    EXPECT_EQ(response.status_code, 400);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json["error"].get<std::string>().find("JSON") != std::string::npos);
}

// Test: Get tunnel metrics
TEST_F(ManagementAPITest, GetTunnelMetrics) {
    // Create tunnel
    json request_body;
    request_body["tunnel_id"] = "metrics-test";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "HTTP";
    
    auto create_response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    EXPECT_EQ(create_response.status_code, 201);
    
    // Get metrics
    auto response = execute_request("GET", "/api/v1/tunnels/metrics-test/metrics");
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["tunnel_id"], "metrics-test");
    EXPECT_TRUE(response_json.contains("connections"));
    EXPECT_TRUE(response_json.contains("throughput"));
    EXPECT_TRUE(response_json["throughput"].contains("bytes_sent"));
    EXPECT_TRUE(response_json["throughput"].contains("bytes_received"));
    EXPECT_TRUE(response_json["connections"].contains("active_http_connections"));
}

// Test: Get tunnel agents (no agents connected)
TEST_F(ManagementAPITest, GetTunnelAgentsEmpty) {
    // Create tunnel
    json request_body;
    request_body["tunnel_id"] = "agents-test";
    request_body["target_host"] = "localhost";
    request_body["target_port"] = 8080;
    request_body["protocol"] = "HTTP";
    
    auto create_response = execute_request("POST", "/api/v1/tunnels", request_body.dump());
    EXPECT_EQ(create_response.status_code, 201);
    
    // Get agents
    auto response = execute_request("GET", "/api/v1/tunnels/agents-test/agents");
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json.is_array());
    EXPECT_EQ(response_json.size(), 0);
}
