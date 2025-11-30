#include <gtest/gtest.h>
#include "proxy/http_proxy.h"
#include "proxy/tcp_proxy.h"
#include "agent/agent_registry.h"
#include "storage/cache.h"
#include "models/tunnel.h"
#include "security/ip_allowlist.h"
#include <memory>
#include <string>

using namespace protogate;

class IPAllowlistTest : public ::testing::Test {
protected:
    void SetUp() override {
        tunnel_cache_ = std::make_shared<storage::Cache<std::string, models::Tunnel>>();
        agent_registry_ = std::make_shared<agent::AgentRegistry>(50);
        http_proxy_ = std::make_shared<proxy::HTTPProxy>(tunnel_cache_, agent_registry_);
        
        // Setup test tunnel with IP allowlist
        models::Tunnel tunnel_with_allowlist;
        tunnel_with_allowlist.tunnel_id = "restricted_api";
        tunnel_with_allowlist.protocol = models::TunnelProtocol::HTTP;
        tunnel_with_allowlist.target_host = "localhost";
        tunnel_with_allowlist.target_port = 8080;
        tunnel_with_allowlist.status = models::TunnelStatus::ACTIVE;
        
        // Allowlist: specific IP and CIDR range
        tunnel_with_allowlist.ip_allowlist = {
            "192.168.1.100",           // Single IP
            "10.0.0.0/24"              // CIDR range
        };
        
        tunnel_cache_->put("restricted_api", tunnel_with_allowlist, std::chrono::hours(1));
        
        // Setup tunnel without allowlist (all IPs allowed)
        models::Tunnel tunnel_no_allowlist;
        tunnel_no_allowlist.tunnel_id = "public_api";
        tunnel_no_allowlist.protocol = models::TunnelProtocol::HTTP;
        tunnel_no_allowlist.target_host = "localhost";
        tunnel_no_allowlist.target_port = 8080;
        tunnel_no_allowlist.status = models::TunnelStatus::ACTIVE;
        // Empty ip_allowlist = all IPs allowed
        
        tunnel_cache_->put("public_api", tunnel_no_allowlist, std::chrono::hours(1));
    }

    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    std::shared_ptr<proxy::HTTPProxy> http_proxy_;
};

//
// HTTP Proxy Tests
//

TEST_F(IPAllowlistTest, HTTPAllowedIPPasses) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/api/data";
    request.version = "HTTP/1.1";
    request.host = "restricted_api.tunnel.test.com";
    request.client_ip = "192.168.1.100";  // In allowlist
    request.headers["content-type"] = "application/json";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request, 
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    // Should NOT be 403 Forbidden (will be 503 since no agent)
    EXPECT_TRUE(response_data.find("403") == std::string::npos);
    EXPECT_TRUE(response_data.find("IP not allowed") == std::string::npos);
}

TEST_F(IPAllowlistTest, HTTPAllowedIPInCIDRPasses) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/api/data";
    request.version = "HTTP/1.1";
    request.host = "restricted_api.tunnel.test.com";
    request.client_ip = "10.0.0.50";  // In 10.0.0.0/24 range
    request.headers["content-type"] = "application/json";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request, 
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    // Should NOT be 403 Forbidden
    EXPECT_TRUE(response_data.find("403") == std::string::npos);
    EXPECT_TRUE(response_data.find("IP not allowed") == std::string::npos);
}

TEST_F(IPAllowlistTest, HTTPBlockedIPReturns403) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/api/data";
    request.version = "HTTP/1.1";
    request.host = "restricted_api.tunnel.test.com";
    request.client_ip = "203.0.113.45";  // NOT in allowlist
    request.headers["content-type"] = "application/json";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request, 
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    // Should be 403 Forbidden with JSON body
    EXPECT_TRUE(response_data.find("403") != std::string::npos);
    EXPECT_TRUE(response_data.find("Forbidden") != std::string::npos);
    EXPECT_TRUE(response_data.find("\"error\"") != std::string::npos);
}

TEST_F(IPAllowlistTest, HTTPBlockedIPJSONResponse) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "POST";
    request.path = "/api/data";
    request.version = "HTTP/1.1";
    request.host = "restricted_api.tunnel.test.com";
    request.client_ip = "172.16.0.1";  // NOT in allowlist
    request.headers["content-type"] = "application/json";
    request.body = R"({"test":"data"})";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request, 
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    EXPECT_TRUE(response_data.find("403 Forbidden") != std::string::npos);
    // Check JSON body structure - format is {"error": {"code": 403, "message": "Forbidden", "reason": "..."}}
    EXPECT_TRUE(response_data.find("\"error\"") != std::string::npos);
    EXPECT_TRUE(response_data.find("\"code\": 403") != std::string::npos);
    EXPECT_TRUE(response_data.find("\"message\": \"Forbidden\"") != std::string::npos);
    EXPECT_TRUE(response_data.find("\"reason\"") != std::string::npos);
}

TEST_F(IPAllowlistTest, HTTPNoAllowlistAllowsAllIPs) {
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/api/data";
    request.version = "HTTP/1.1";
    request.host = "public_api.tunnel.test.com";
    request.client_ip = "203.0.113.99";  // Any IP should work
    request.headers["content-type"] = "application/json";
    
    bool callback_invoked = false;
    std::string response_data;
    
    http_proxy_->handle_request(request, 
        [&callback_invoked, &response_data](const std::string& response, bool error) {
            callback_invoked = true;
            response_data = response;
        });
    
    EXPECT_TRUE(callback_invoked);
    // Should NOT be 403 Forbidden (will be 503 since no agent)
    EXPECT_TRUE(response_data.find("403") == std::string::npos);
    EXPECT_TRUE(response_data.find("IP not allowed") == std::string::npos);
}

//
// IPAllowlist Direct Unit Tests
//

TEST_F(IPAllowlistTest, AllowlistAllowsSingleIPv4) {
    security::IPAllowlist allowlist;
    EXPECT_TRUE(allowlist.add_ip("192.168.1.100"));
    
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.101"));
}

TEST_F(IPAllowlistTest, AllowlistAllowsCIDRRange) {
    security::IPAllowlist allowlist;
    EXPECT_TRUE(allowlist.add_cidr("10.0.0.0/24"));
    
    EXPECT_TRUE(allowlist.is_allowed("10.0.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("10.0.0.255"));
    EXPECT_FALSE(allowlist.is_allowed("10.0.1.1"));
}

TEST_F(IPAllowlistTest, AllowlistAllowsIPv6) {
    security::IPAllowlist allowlist;
    EXPECT_TRUE(allowlist.add_ip("2001:db8::1"));
    
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db8::2"));
}

TEST_F(IPAllowlistTest, AllowlistAllowsIPv6CIDR) {
    security::IPAllowlist allowlist;
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::/32"));
    
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8:1::1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db9::1"));
}

TEST_F(IPAllowlistTest, EmptyAllowlistAllowsAll) {
    security::IPAllowlist allowlist;
    
    // Empty allowlist = allow all
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_TRUE(allowlist.is_allowed("10.0.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
}

TEST_F(IPAllowlistTest, AllowlistFromCIDRList) {
    std::vector<std::string> cidrs = {"192.168.1.0/24", "10.0.0.0/8"};
    auto allowlist_opt = security::IPAllowlist::from_cidr_list(cidrs);
    
    ASSERT_TRUE(allowlist_opt.has_value());
    
    EXPECT_TRUE(allowlist_opt->is_allowed("192.168.1.50"));
    EXPECT_TRUE(allowlist_opt->is_allowed("10.5.10.20"));
    EXPECT_FALSE(allowlist_opt->is_allowed("172.16.0.1"));
}

