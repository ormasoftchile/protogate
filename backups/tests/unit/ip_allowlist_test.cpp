#include "security/ip_allowlist.h"
#include <gtest/gtest.h>

using namespace protogate::security;

class IPAllowlistTest : public ::testing::Test {
protected:
    IPAllowlist allowlist;
    
    void SetUp() override {
        // Reset allowlist for each test
        allowlist = IPAllowlist();
    }
};

// ============================================================================
// Empty Allowlist Tests
// ============================================================================

TEST_F(IPAllowlistTest, EmptyAllowlistAllowsAll) {
    // Empty allowlist should allow all IPs
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.1"));
    EXPECT_TRUE(allowlist.is_allowed("10.0.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_TRUE(allowlist.is_allowed("::1"));
}

// ============================================================================
// IPv4 CIDR Parsing Tests
// ============================================================================

TEST_F(IPAllowlistTest, IPv4_SingleHost) {
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.100/32"));
    
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.101"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.99"));
}

TEST_F(IPAllowlistTest, IPv4_Subnet24) {
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.0/24"));
    
    // Within subnet
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.1"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.254"));
    
    // Outside subnet
    EXPECT_FALSE(allowlist.is_allowed("192.168.2.1"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.0.255"));
    EXPECT_FALSE(allowlist.is_allowed("10.0.0.1"));
}

TEST_F(IPAllowlistTest, IPv4_Subnet16) {
    EXPECT_TRUE(allowlist.add_cidr("10.20.0.0/16"));
    
    // Within subnet
    EXPECT_TRUE(allowlist.is_allowed("10.20.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("10.20.255.255"));
    EXPECT_TRUE(allowlist.is_allowed("10.20.100.50"));
    
    // Outside subnet
    EXPECT_FALSE(allowlist.is_allowed("10.21.0.1"));
    EXPECT_FALSE(allowlist.is_allowed("10.19.255.255"));
}

TEST_F(IPAllowlistTest, IPv4_Subnet8) {
    EXPECT_TRUE(allowlist.add_cidr("172.0.0.0/8"));
    
    // Within subnet
    EXPECT_TRUE(allowlist.is_allowed("172.0.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("172.255.255.255"));
    EXPECT_TRUE(allowlist.is_allowed("172.16.0.1"));
    
    // Outside subnet
    EXPECT_FALSE(allowlist.is_allowed("173.0.0.1"));
    EXPECT_FALSE(allowlist.is_allowed("171.255.255.255"));
}

TEST_F(IPAllowlistTest, IPv4_NonStandardPrefix) {
    // /25 = 128 addresses
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.0/25"));
    
    // First half (0-127)
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.0"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.127"));
    
    // Second half (128-255) should be blocked
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.128"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.255"));
}

TEST_F(IPAllowlistTest, IPv4_PrefixZero) {
    // /0 should match all IPv4 addresses
    EXPECT_TRUE(allowlist.add_cidr("0.0.0.0/0"));
    
    EXPECT_TRUE(allowlist.is_allowed("1.2.3.4"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.1"));
    EXPECT_TRUE(allowlist.is_allowed("255.255.255.255"));
    
    // Should NOT match IPv6
    EXPECT_FALSE(allowlist.is_allowed("2001:db8::1"));
}

// ============================================================================
// IPv6 CIDR Parsing Tests
// ============================================================================

TEST_F(IPAllowlistTest, IPv6_SingleHost) {
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::1/128"));
    
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db8::2"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db8::"));
}

TEST_F(IPAllowlistTest, IPv6_Subnet64) {
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::/64"));
    
    // Within subnet
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::ffff:ffff:ffff:ffff"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1234:5678"));
    
    // Outside subnet
    EXPECT_FALSE(allowlist.is_allowed("2001:db8:1::1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db9::1"));
}

TEST_F(IPAllowlistTest, IPv6_Subnet32) {
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::/32"));
    
    // Within subnet
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8:ffff:ffff::1"));
    
    // Outside subnet
    EXPECT_FALSE(allowlist.is_allowed("2001:db9::1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db7:ffff:ffff::1"));
}

TEST_F(IPAllowlistTest, IPv6_PrefixZero) {
    // ::/0 should match all IPv6 addresses
    EXPECT_TRUE(allowlist.add_cidr("::/0"));
    
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_TRUE(allowlist.is_allowed("::1"));
    EXPECT_TRUE(allowlist.is_allowed("fe80::1"));
    
    // Should NOT match IPv4
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.1"));
}

TEST_F(IPAllowlistTest, IPv6_Loopback) {
    EXPECT_TRUE(allowlist.add_cidr("::1/128"));
    
    EXPECT_TRUE(allowlist.is_allowed("::1"));
    EXPECT_FALSE(allowlist.is_allowed("::2"));
}

// ============================================================================
// Single IP Tests (auto /32 or /128)
// ============================================================================

TEST_F(IPAllowlistTest, SingleIP_IPv4) {
    EXPECT_TRUE(allowlist.add_ip("192.168.1.100"));
    
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.101"));
}

TEST_F(IPAllowlistTest, SingleIP_IPv6) {
    EXPECT_TRUE(allowlist.add_ip("2001:db8::cafe"));
    
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::cafe"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db8::1"));
}

// ============================================================================
// Multiple CIDR Ranges Tests
// ============================================================================

TEST_F(IPAllowlistTest, MultipleCIDRs_IPv4) {
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.0/24"));
    EXPECT_TRUE(allowlist.add_cidr("10.0.0.0/16"));
    
    // First range
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.1"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.254"));
    
    // Second range
    EXPECT_TRUE(allowlist.is_allowed("10.0.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("10.0.255.255"));
    
    // Outside both ranges
    EXPECT_FALSE(allowlist.is_allowed("172.16.0.1"));
}

TEST_F(IPAllowlistTest, MultipleCIDRs_Mixed) {
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.0/24"));
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::/64"));
    
    // IPv4 range
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    
    // IPv6 range
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    
    // Outside ranges
    EXPECT_FALSE(allowlist.is_allowed("10.0.0.1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db9::1"));
}

// ============================================================================
// Bulk Parsing Tests (from_cidr_list)
// ============================================================================

TEST_F(IPAllowlistTest, BulkParsing_Valid) {
    std::vector<std::string> cidrs = {
        "192.168.1.0/24",
        "10.0.0.0/16",
        "2001:db8::/64"
    };
    
    auto result = IPAllowlist::from_cidr_list(cidrs);
    ASSERT_TRUE(result.has_value());
    
    auto& bulk_allowlist = result.value();
    EXPECT_TRUE(bulk_allowlist.is_allowed("192.168.1.1"));
    EXPECT_TRUE(bulk_allowlist.is_allowed("10.0.100.50"));
    EXPECT_TRUE(bulk_allowlist.is_allowed("2001:db8::1"));
    EXPECT_FALSE(bulk_allowlist.is_allowed("172.16.0.1"));
}

TEST_F(IPAllowlistTest, BulkParsing_InvalidCIDR) {
    std::vector<std::string> cidrs = {
        "192.168.1.0/24",
        "invalid-cidr",  // Invalid
        "10.0.0.0/16"
    };
    
    auto result = IPAllowlist::from_cidr_list(cidrs);
    EXPECT_FALSE(result.has_value());
}

TEST_F(IPAllowlistTest, BulkParsing_EmptyList) {
    std::vector<std::string> cidrs = {};
    
    auto result = IPAllowlist::from_cidr_list(cidrs);
    ASSERT_TRUE(result.has_value());
    
    // Empty allowlist should allow all
    auto& empty_list = result.value();
    EXPECT_TRUE(empty_list.is_allowed("192.168.1.1"));
}

// ============================================================================
// Invalid Input Tests
// ============================================================================

TEST_F(IPAllowlistTest, Invalid_MalformedIP) {
    EXPECT_FALSE(allowlist.add_cidr("999.999.999.999/24"));
    EXPECT_FALSE(allowlist.add_ip("not-an-ip"));
}

TEST_F(IPAllowlistTest, Invalid_PrefixTooLarge_IPv4) {
    EXPECT_FALSE(allowlist.add_cidr("192.168.1.0/33"));
}

TEST_F(IPAllowlistTest, Invalid_PrefixTooLarge_IPv6) {
    EXPECT_FALSE(allowlist.add_cidr("2001:db8::/129"));
}

TEST_F(IPAllowlistTest, Invalid_NegativePrefix) {
    EXPECT_FALSE(allowlist.add_cidr("192.168.1.0/-1"));
}

TEST_F(IPAllowlistTest, DefaultPrefix_NoSlash) {
    // No prefix defaults to /32 for IPv4, /128 for IPv6
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.100"));  // Becomes /32
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::1"));    // Becomes /128
    
    // IPv4 - only exact match
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.101"));
    
    // IPv6 - only exact match
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_FALSE(allowlist.is_allowed("2001:db8::2"));
}

TEST_F(IPAllowlistTest, Invalid_EmptyString) {
    EXPECT_FALSE(allowlist.add_cidr(""));
    EXPECT_FALSE(allowlist.add_ip(""));
}

TEST_F(IPAllowlistTest, Invalid_CheckMalformedIP) {
    allowlist.add_cidr("192.168.1.0/24");
    
    // Checking with invalid IP should return false (not throw)
    EXPECT_FALSE(allowlist.is_allowed("invalid-ip"));
    EXPECT_FALSE(allowlist.is_allowed("999.999.999.999"));
}

// ============================================================================
// Network Mask Normalization Tests
// ============================================================================

TEST_F(IPAllowlistTest, NetworkMaskNormalization_IPv4) {
    // Input: 192.168.1.100/24 should be normalized to 192.168.1.0/24
    EXPECT_TRUE(allowlist.add_cidr("192.168.1.100/24"));
    
    // Should match entire /24 range, not just .100
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.1"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.100"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.1.254"));
}

TEST_F(IPAllowlistTest, NetworkMaskNormalization_IPv6) {
    // Input: 2001:db8::1234/64 should be normalized to 2001:db8::/64
    EXPECT_TRUE(allowlist.add_cidr("2001:db8::1234/64"));
    
    // Should match entire /64 range
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::1234"));
    EXPECT_TRUE(allowlist.is_allowed("2001:db8::ffff:ffff:ffff:ffff"));
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(IPAllowlistTest, EdgeCase_Localhost_IPv4) {
    EXPECT_TRUE(allowlist.add_cidr("127.0.0.0/8"));
    
    EXPECT_TRUE(allowlist.is_allowed("127.0.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("127.255.255.255"));
}

TEST_F(IPAllowlistTest, EdgeCase_PrivateRanges) {
    EXPECT_TRUE(allowlist.add_cidr("10.0.0.0/8"));
    EXPECT_TRUE(allowlist.add_cidr("172.16.0.0/12"));
    EXPECT_TRUE(allowlist.add_cidr("192.168.0.0/16"));
    
    EXPECT_TRUE(allowlist.is_allowed("10.1.2.3"));
    EXPECT_TRUE(allowlist.is_allowed("172.16.0.1"));
    EXPECT_TRUE(allowlist.is_allowed("192.168.100.50"));
    
    // Public IP
    EXPECT_FALSE(allowlist.is_allowed("8.8.8.8"));
}

TEST_F(IPAllowlistTest, EdgeCase_IPv4MappedIPv6) {
    // IPv4-mapped IPv6 addresses (::ffff:192.168.1.1) are treated as IPv6
    EXPECT_TRUE(allowlist.add_cidr("::ffff:192.168.1.0/120"));  // /120 covers .0-.255
    
    EXPECT_TRUE(allowlist.is_allowed("::ffff:192.168.1.100"));
    
    // Plain IPv4 should NOT match
    EXPECT_FALSE(allowlist.is_allowed("192.168.1.100"));
}
