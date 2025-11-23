#include "utils/config.h"
#include <gtest/gtest.h>
#include <cstdlib>

using namespace protogate::utils;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear environment
        unsetenv("PORT");
        unsetenv("AGENT_PORT");
        unsetenv("KEY_VAULT_URI");
        unsetenv("DNS_ZONE");
        unsetenv("TCP_PORTS");
    }
    
    void TearDown() override {
        SetUp();  // Clean up after each test
    }
};

TEST_F(ConfigTest, DefaultValues) {
    setenv("KEY_VAULT_URI", "https://test.vault.azure.net", 1);
    setenv("DNS_ZONE", "tunnel.example.com", 1);
    
    auto config = Config::from_environment();
    
    EXPECT_EQ(config.port, 443);
    EXPECT_EQ(config.agent_port, 8443);
    EXPECT_EQ(config.max_concurrent_tunnels, 50);
}

TEST_F(ConfigTest, CustomPort) {
    setenv("KEY_VAULT_URI", "https://test.vault.azure.net", 1);
    setenv("DNS_ZONE", "tunnel.example.com", 1);
    setenv("PORT", "8080", 1);
    setenv("AGENT_PORT", "9000", 1);
    
    auto config = Config::from_environment();
    
    EXPECT_EQ(config.port, 8080);
    EXPECT_EQ(config.agent_port, 9000);
}

TEST_F(ConfigTest, MissingRequiredConfig) {
    // Missing KEY_VAULT_URI
    setenv("DNS_ZONE", "tunnel.example.com", 1);
    
    EXPECT_THROW(Config::from_environment(), std::runtime_error);
}

TEST_F(ConfigTest, InvalidKeyVaultURI) {
    setenv("KEY_VAULT_URI", "not-a-valid-uri", 1);
    setenv("DNS_ZONE", "tunnel.example.com", 1);
    
    EXPECT_THROW(Config::from_environment(), std::runtime_error);
}

TEST_F(ConfigTest, TCPPortsParsing) {
    setenv("KEY_VAULT_URI", "https://test.vault.azure.net", 1);
    setenv("DNS_ZONE", "tunnel.example.com", 1);
    setenv("TCP_PORTS", "9100,9200,9300", 1);
    
    auto config = Config::from_environment();
    
    ASSERT_EQ(config.tcp_ports.size(), 3);
    EXPECT_EQ(config.tcp_ports[0], 9100);
    EXPECT_EQ(config.tcp_ports[1], 9200);
    EXPECT_EQ(config.tcp_ports[2], 9300);
}

TEST_F(ConfigTest, PortConflictValidation) {
    setenv("KEY_VAULT_URI", "https://test.vault.azure.net", 1);
    setenv("DNS_ZONE", "tunnel.example.com", 1);
    setenv("PORT", "8080", 1);
    setenv("AGENT_PORT", "8080", 1);  // Same as PORT
    
    EXPECT_THROW(Config::from_environment(), std::runtime_error);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
