#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace protogate {
namespace security {

/**
 * @brief IP address type
 */
enum class IPType {
    IPv4,
    IPv6
};

/**
 * @brief CIDR range representation
 */
struct CIDRRange {
    IPType type;
    std::vector<uint8_t> network;  // Network address (4 bytes for IPv4, 16 for IPv6)
    uint8_t prefix_length;         // CIDR prefix length (0-32 for IPv4, 0-128 for IPv6)
    
    CIDRRange(IPType t, std::vector<uint8_t> net, uint8_t prefix)
        : type(t), network(std::move(net)), prefix_length(prefix) {}
};

/**
 * @brief IP allowlist for tunnel access control
 * 
 * Supports:
 * - IPv4 and IPv6 addresses
 * - CIDR notation (192.168.1.0/24, 2001:db8::/32)
 * - Individual IP addresses
 * - Empty allowlist = allow all (default open)
 * 
 * Thread-safe for read operations, write operations should be synchronized externally
 */
class IPAllowlist {
public:
    /**
     * @brief Create an empty allowlist (allows all IPs)
     */
    IPAllowlist() = default;
    
    /**
     * @brief Create allowlist from CIDR strings
     * @param cidr_strings List of CIDR ranges (e.g., ["192.168.1.0/24", "10.0.0.0/8"])
     * @return IPAllowlist instance, or std::nullopt if parsing fails
     */
    static std::optional<IPAllowlist> from_cidr_list(const std::vector<std::string>& cidr_strings);
    
    /**
     * @brief Add CIDR range to allowlist
     * @param cidr_string CIDR notation string (e.g., "192.168.1.0/24" or "2001:db8::/32")
     * @return true if added successfully, false if invalid format
     */
    bool add_cidr(const std::string& cidr_string);
    
    /**
     * @brief Add single IP address to allowlist
     * @param ip_string IP address string (e.g., "192.168.1.1" or "2001:db8::1")
     * @return true if added successfully, false if invalid format
     */
    bool add_ip(const std::string& ip_string);
    
    /**
     * @brief Check if IP address is allowed
     * @param ip_string IP address to check
     * @return true if allowed (or allowlist is empty), false if blocked
     */
    bool is_allowed(const std::string& ip_string) const;
    
    /**
     * @brief Check if allowlist is empty (allows all)
     * @return true if no rules configured
     */
    bool is_empty() const { return ranges_.empty(); }
    
    /**
     * @brief Get number of CIDR ranges configured
     * @return Number of rules in allowlist
     */
    size_t size() const { return ranges_.size(); }
    
    /**
     * @brief Clear all allowlist rules
     */
    void clear() { ranges_.clear(); }
    
    /**
     * @brief Get all configured CIDR ranges as strings
     * @return Vector of CIDR notation strings
     */
    std::vector<std::string> to_strings() const;

private:
    /**
     * @brief Parse CIDR notation string
     * @param cidr_string CIDR notation (e.g., "192.168.1.0/24")
     * @return CIDRRange if valid, std::nullopt if invalid
     */
    static std::optional<CIDRRange> parse_cidr(const std::string& cidr_string);
    
    /**
     * @brief Parse IPv4 address string to bytes
     * @param ip_string IPv4 address (e.g., "192.168.1.1")
     * @return 4-byte vector if valid, std::nullopt if invalid
     */
    static std::optional<std::vector<uint8_t>> parse_ipv4(const std::string& ip_string);
    
    /**
     * @brief Parse IPv6 address string to bytes
     * @param ip_string IPv6 address (e.g., "2001:db8::1")
     * @return 16-byte vector if valid, std::nullopt if invalid
     */
    static std::optional<std::vector<uint8_t>> parse_ipv6(const std::string& ip_string);
    
    /**
     * @brief Check if IP matches CIDR range
     * @param ip_bytes IP address bytes
     * @param range CIDR range to check against
     * @return true if IP is within range
     */
    static bool matches_cidr(const std::vector<uint8_t>& ip_bytes, const CIDRRange& range);
    
    /**
     * @brief Convert CIDR range to string representation
     * @param range CIDR range
     * @return CIDR notation string
     */
    static std::string cidr_to_string(const CIDRRange& range);
    
    std::vector<CIDRRange> ranges_;
};

}  // namespace security
}  // namespace protogate
