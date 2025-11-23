#include "ip_allowlist.h"
#include "../observability/logger.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>

namespace protogate {
namespace security {

std::optional<IPAllowlist> IPAllowlist::from_cidr_list(const std::vector<std::string>& cidr_strings) {
    IPAllowlist allowlist;
    
    for (const auto& cidr : cidr_strings) {
        if (!allowlist.add_cidr(cidr)) {
            observability::Logger::instance().error("Invalid CIDR in allowlist", {
                {"cidr", cidr}
            });
            return std::nullopt;
        }
    }
    
    return allowlist;
}

bool IPAllowlist::add_cidr(const std::string& cidr_string) {
    auto range = parse_cidr(cidr_string);
    if (!range) {
        return false;
    }
    
    ranges_.push_back(std::move(*range));
    return true;
}

bool IPAllowlist::add_ip(const std::string& ip_string) {
    // Try IPv4 first
    auto ipv4_bytes = parse_ipv4(ip_string);
    if (ipv4_bytes) {
        ranges_.emplace_back(IPType::IPv4, std::move(*ipv4_bytes), 32);
        return true;
    }
    
    // Try IPv6
    auto ipv6_bytes = parse_ipv6(ip_string);
    if (ipv6_bytes) {
        ranges_.emplace_back(IPType::IPv6, std::move(*ipv6_bytes), 128);
        return true;
    }
    
    return false;
}

bool IPAllowlist::is_allowed(const std::string& ip_string) const {
    // Empty allowlist = allow all
    if (ranges_.empty()) {
        return true;
    }
    
    // Try to parse as IPv4
    auto ipv4_bytes = parse_ipv4(ip_string);
    if (ipv4_bytes) {
        for (const auto& range : ranges_) {
            if (range.type == IPType::IPv4 && matches_cidr(*ipv4_bytes, range)) {
                return true;
            }
        }
        return false;
    }
    
    // Try to parse as IPv6
    auto ipv6_bytes = parse_ipv6(ip_string);
    if (ipv6_bytes) {
        for (const auto& range : ranges_) {
            if (range.type == IPType::IPv6 && matches_cidr(*ipv6_bytes, range)) {
                return true;
            }
        }
        return false;
    }
    
    // Invalid IP format
    observability::Logger::instance().warning("Invalid IP format for allowlist check", {
        {"ip", ip_string}
    });
    return false;
}

std::vector<std::string> IPAllowlist::to_strings() const {
    std::vector<std::string> result;
    result.reserve(ranges_.size());
    
    for (const auto& range : ranges_) {
        result.push_back(cidr_to_string(range));
    }
    
    return result;
}

std::optional<CIDRRange> IPAllowlist::parse_cidr(const std::string& cidr_string) {
    // Find the '/' separator
    size_t slash_pos = cidr_string.find('/');
    if (slash_pos == std::string::npos) {
        // No prefix length - treat as single IP
        // Try IPv4
        auto ipv4_bytes = parse_ipv4(cidr_string);
        if (ipv4_bytes) {
            return CIDRRange(IPType::IPv4, std::move(*ipv4_bytes), 32);
        }
        
        // Try IPv6
        auto ipv6_bytes = parse_ipv6(cidr_string);
        if (ipv6_bytes) {
            return CIDRRange(IPType::IPv6, std::move(*ipv6_bytes), 128);
        }
        
        return std::nullopt;
    }
    
    // Parse IP address and prefix length
    std::string ip_part = cidr_string.substr(0, slash_pos);
    std::string prefix_part = cidr_string.substr(slash_pos + 1);
    
    // Parse prefix length
    int prefix_length;
    try {
        prefix_length = std::stoi(prefix_part);
    } catch (...) {
        return std::nullopt;
    }
    
    // Try IPv4
    auto ipv4_bytes = parse_ipv4(ip_part);
    if (ipv4_bytes) {
        if (prefix_length < 0 || prefix_length > 32) {
            return std::nullopt;
        }
        
        // Apply network mask to ensure it's a network address
        int full_bytes = prefix_length / 8;
        int remaining_bits = prefix_length % 8;
        
        // Zero out host bits
        for (size_t i = static_cast<size_t>(full_bytes); i < ipv4_bytes->size(); i++) {
            if (i == static_cast<size_t>(full_bytes) && remaining_bits > 0) {
                uint8_t mask = 0xFF << (8 - remaining_bits);
                (*ipv4_bytes)[i] &= mask;
            } else {
                (*ipv4_bytes)[i] = 0;
            }
        }
        
        return CIDRRange(IPType::IPv4, std::move(*ipv4_bytes), static_cast<uint8_t>(prefix_length));
    }
    
    // Try IPv6
    auto ipv6_bytes = parse_ipv6(ip_part);
    if (ipv6_bytes) {
        if (prefix_length < 0 || prefix_length > 128) {
            return std::nullopt;
        }
        
        // Apply network mask
        int full_bytes = prefix_length / 8;
        int remaining_bits = prefix_length % 8;
        
        for (size_t i = static_cast<size_t>(full_bytes); i < ipv6_bytes->size(); i++) {
            if (i == static_cast<size_t>(full_bytes) && remaining_bits > 0) {
                uint8_t mask = 0xFF << (8 - remaining_bits);
                (*ipv6_bytes)[i] &= mask;
            } else {
                (*ipv6_bytes)[i] = 0;
            }
        }
        
        return CIDRRange(IPType::IPv6, std::move(*ipv6_bytes), static_cast<uint8_t>(prefix_length));
    }
    
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> IPAllowlist::parse_ipv4(const std::string& ip_string) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_string.c_str(), &addr) != 1) {
        return std::nullopt;
    }
    
    std::vector<uint8_t> bytes(4);
    std::memcpy(bytes.data(), &addr, 4);
    return bytes;
}

std::optional<std::vector<uint8_t>> IPAllowlist::parse_ipv6(const std::string& ip_string) {
    struct in6_addr addr;
    if (inet_pton(AF_INET6, ip_string.c_str(), &addr) != 1) {
        return std::nullopt;
    }
    
    std::vector<uint8_t> bytes(16);
    std::memcpy(bytes.data(), &addr, 16);
    return bytes;
}

bool IPAllowlist::matches_cidr(const std::vector<uint8_t>& ip_bytes, const CIDRRange& range) {
    if (ip_bytes.size() != range.network.size()) {
        return false;
    }
    
    int full_bytes = range.prefix_length / 8;
    int remaining_bits = range.prefix_length % 8;
    
    // Compare full bytes
    for (int i = 0; i < full_bytes; i++) {
        if (ip_bytes[i] != range.network[i]) {
            return false;
        }
    }
    
    // Compare remaining bits
    if (remaining_bits > 0 && full_bytes < static_cast<int>(ip_bytes.size())) {
        uint8_t mask = 0xFF << (8 - remaining_bits);
        if ((ip_bytes[full_bytes] & mask) != (range.network[full_bytes] & mask)) {
            return false;
        }
    }
    
    return true;
}

std::string IPAllowlist::cidr_to_string(const CIDRRange& range) {
    std::ostringstream oss;
    
    if (range.type == IPType::IPv4) {
        struct in_addr addr;
        std::memcpy(&addr, range.network.data(), 4);
        
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
        oss << str << "/" << static_cast<int>(range.prefix_length);
    } else {
        struct in6_addr addr;
        std::memcpy(&addr, range.network.data(), 16);
        
        char str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr, str, INET6_ADDRSTRLEN);
        oss << str << "/" << static_cast<int>(range.prefix_length);
    }
    
    return oss.str();
}

}  // namespace security
}  // namespace protogate
