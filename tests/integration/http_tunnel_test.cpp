#include <gtest/gtest.h>
#include "proxy/http_proxy.h"
#include "agent/agent_registry.h"
#include "storage/cache.h"
#include "models/tunnel.h"
#include <memory>

using namespace protogate;

class HTTPTunnelTest : public ::testing::Test {
protected:
    void SetUp() override {
        tunnel_cache_ = std::make_shared<storage::Cache<std::string, models::Tunnel>>();
        agent_registry_ = std::make_shared<agent::AgentRegistry>(50);
        http_proxy_ = std::make_shared<proxy::HTTPProxy>(tunnel_cache_, agent_registry_);
        
        // Setup test tunnel with tunnel_id "api" to match hostname "api.tunnel.test.com"
        models::Tunnel tunnel;
        tunnel.tunnel_id = "api";
        tunnel.protocol = models::TunnelProtocol::HTTP;
        tunnel.target_host = "localhost";
        tunnel.target_port = 8080;
        tunnel.status = models::TunnelStatus::ACTIVE;
        
        tunnel_cache_->put("api", tunnel, std::chrono::hours(1));
    }

    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    std::shared_ptr<proxy::HTTPProxy> http_proxy_;
};

// Test HTTP GET request through tunnel
TEST_F(HTTPTunnelTest, HTTPGetRequestRoutedCorrectly) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/api/users";
    request.version = "HTTP/1.1";
    request.host = "api.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    request.headers["content-type"] = "application/json";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request, 
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    // Response will be 503 because no agent is connected
    EXPECT_TRUE(response_data.find("503") != std::string::npos);
}

// Test HTTP POST request through tunnel
TEST_F(HTTPTunnelTest, HTTPPostRequestWithBody) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "POST";
    request.path = "/api/users";
    request.version = "HTTP/1.1";
    request.host = "api.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    request.headers["content-type"] = "application/json";
    request.headers["content-length"] = "27";
    request.body = R"({"name":"John","age":30})";
    
    bool callback_invoked = false;
    
    http_proxy_->handle_request(request,
        [&callback_invoked](const std::string& response, bool error) {
            callback_invoked = true;
        });
    
    EXPECT_TRUE(callback_invoked);
}

// Test HTTP PUT request through tunnel
TEST_F(HTTPTunnelTest, HTTPPutRequestWithBody) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "PUT";
    request.path = "/api/users/123";
    request.version = "HTTP/1.1";
    request.host = "api.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    request.headers["content-type"] = "application/json";
    request.body = R"({"name":"Jane","age":28})";
    
    bool callback_invoked = false;
    
    http_proxy_->handle_request(request,
        [&callback_invoked](const std::string& response, bool error) {
            callback_invoked = true;
        });
    
    EXPECT_TRUE(callback_invoked);
}

// Test HTTP DELETE request through tunnel
TEST_F(HTTPTunnelTest, HTTPDeleteRequest) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "DELETE";
    request.path = "/api/users/123";
    request.version = "HTTP/1.1";
    request.host = "api.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    
    bool callback_invoked = false;
    
    http_proxy_->handle_request(request,
        [&callback_invoked](const std::string& response, bool error) {
            callback_invoked = true;
        });
    
    EXPECT_TRUE(callback_invoked);
}

// Test request to non-existent tunnel returns 404
TEST_F(HTTPTunnelTest, NonExistentTunnelReturns404) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/";
    request.version = "HTTP/1.1";
    request.host = "nonexistent.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request,
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    EXPECT_TRUE(response_data.find("404") != std::string::npos);
}

// Test request without agent connected returns 503
TEST_F(HTTPTunnelTest, NoAgentConnectedReturns503) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/";
    request.version = "HTTP/1.1";
    request.host = "api.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request,
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    EXPECT_TRUE(response_data.find("503") != std::string::npos);
    EXPECT_TRUE(response_data.find("Agent not connected") != std::string::npos);
}

// Test HTTP request parsing
TEST_F(HTTPTunnelTest, HTTPRequestParsing) {
    std::string raw_request = 
        "GET /api/users HTTP/1.1\r\n"
        "Host: api.tunnel.test.com\r\n"
        "Content-Type: application/json\r\n"
        "User-Agent: TestClient/1.0\r\n"
        "\r\n";
    
    auto parsed = proxy::HTTPProxy::parse_request(raw_request);
    
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->method, "GET");
    EXPECT_EQ(parsed->path, "/api/users");
    EXPECT_EQ(parsed->version, "HTTP/1.1");
    EXPECT_EQ(parsed->host, "api.tunnel.test.com");
    EXPECT_EQ(parsed->headers["content-type"], "application/json");
}

// Test HTTP request parsing with body
TEST_F(HTTPTunnelTest, HTTPRequestParsingWithBody) {
    std::string raw_request = 
        "POST /api/users HTTP/1.1\r\n"
        "Host: api.tunnel.test.com\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 27\r\n"
        "\r\n"
        R"({"name":"John","age":30})";
    
    auto parsed = proxy::HTTPProxy::parse_request(raw_request);
    
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->method, "POST");
    EXPECT_EQ(parsed->body, R"({"name":"John","age":30})");
}

// Test hostname to tunnel ID matching
TEST_F(HTTPTunnelTest, HostnameToTunnelIDMatching) {
    std::string tunnel_id = http_proxy_->match_tunnel("api.tunnel.test.com");
    EXPECT_EQ(tunnel_id, "api");
}

// Test IP allowlist validation - allowed IP
TEST_F(HTTPTunnelTest, AllowedIPAccepted) {
    models::Tunnel tunnel;
    tunnel.tunnel_id = "secure_api";
    tunnel.ip_allowlist = {"192.168.1.100/32", "10.0.0.0/8"};
    
    bool allowed = http_proxy_->validate_ip_allowlist(tunnel, "192.168.1.100");
    EXPECT_TRUE(allowed);
}

// Test IP allowlist validation - blocked IP
TEST_F(HTTPTunnelTest, BlockedIPRejected) {
    models::Tunnel tunnel;
    tunnel.tunnel_id = "secure_api";
    tunnel.ip_allowlist = {"192.168.1.100/32"};
    
    bool allowed = http_proxy_->validate_ip_allowlist(tunnel, "192.168.1.200");
    EXPECT_FALSE(allowed);
}

// Test empty IP allowlist allows all
TEST_F(HTTPTunnelTest, EmptyAllowlistAllowsAll) {
    models::Tunnel tunnel;
    tunnel.tunnel_id = "public_api";
    tunnel.ip_allowlist = {};
    
    bool allowed = http_proxy_->validate_ip_allowlist(tunnel, "1.2.3.4");
    EXPECT_TRUE(allowed);
}
