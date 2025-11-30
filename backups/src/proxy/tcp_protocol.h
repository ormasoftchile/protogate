#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace protogate {
namespace proxy {

/**
 * @brief TCP tunnel protocol frame types
 * 
 * TCP connections are multiplexed over HTTP/2 DATA frames using a custom
 * binary protocol. Each frame has a type identifier and connection ID.
 */
enum class TCPFrameType : uint8_t {
    TCP_OPEN = 0x10,     // Open new TCP connection
    TCP_DATA = 0x11,     // Data payload
    TCP_CLOSE = 0x12,    // Close connection
    TCP_ERROR = 0x13,    // Error notification
    TCP_ACK = 0x14       // Flow control acknowledgment
};

/**
 * @brief TCP connection close reasons
 */
enum class TCPCloseReason : uint16_t {
    NORMAL = 0x0000,           // Normal closure
    TIMEOUT = 0x0001,          // Inactivity timeout
    ERROR = 0x0002,            // Generic error
    CONNECTION_REFUSED = 0x0003, // Target refused connection
    HOST_UNREACHABLE = 0x0004,   // Target host unreachable
    PROTOCOL_ERROR = 0x0005      // Protocol violation
};

/**
 * @brief TCP error codes
 */
enum class TCPErrorCode : uint16_t {
    CONNECTION_REFUSED = 0x0001,
    HOST_UNREACHABLE = 0x0002,
    TIMEOUT = 0x0003,
    PROTOCOL_ERROR = 0x0004,
    INTERNAL_ERROR = 0x0005
};

/**
 * @brief TCP_OPEN frame structure
 * 
 * Frame format:
 * +-------------------+
 * | Frame Type (1B)   | = 0x10 (TCP_OPEN)
 * +-------------------+
 * | Connection ID (16B)| = UUID of connection
 * +-------------------+
 * | Target Port (2B)  | = Port number on agent side
 * +-------------------+
 */
struct TCPOpenFrame {
    TCPFrameType type = TCPFrameType::TCP_OPEN;
    std::array<uint8_t, 16> connection_id;  // UUID as bytes
    uint16_t target_port;
    
    std::vector<uint8_t> serialize() const;
    static TCPOpenFrame deserialize(const uint8_t* data, size_t length);
};

/**
 * @brief TCP_DATA frame structure
 * 
 * Frame format:
 * +-------------------+
 * | Frame Type (1B)   | = 0x11 (TCP_DATA)
 * +-------------------+
 * | Connection ID (16B)| = UUID
 * +-------------------+
 * | Sequence Num (4B) | = Incremental per connection
 * +-------------------+
 * | Data Length (4B)  | = N bytes
 * +-------------------+
 * | Data (NB)         | = Raw TCP payload
 * +-------------------+
 */
struct TCPDataFrame {
    TCPFrameType type = TCPFrameType::TCP_DATA;
    std::array<uint8_t, 16> connection_id;
    uint32_t sequence_number;
    std::vector<uint8_t> data;
    
    std::vector<uint8_t> serialize() const;
    static TCPDataFrame deserialize(const uint8_t* data, size_t length);
};

/**
 * @brief TCP_CLOSE frame structure
 * 
 * Frame format:
 * +-------------------+
 * | Frame Type (1B)   | = 0x12 (TCP_CLOSE)
 * +-------------------+
 * | Connection ID (16B)| = UUID
 * +-------------------+
 * | Reason Code (2B)  | = Close reason
 * +-------------------+
 */
struct TCPCloseFrame {
    TCPFrameType type = TCPFrameType::TCP_CLOSE;
    std::array<uint8_t, 16> connection_id;
    TCPCloseReason reason;
    
    std::vector<uint8_t> serialize() const;
    static TCPCloseFrame deserialize(const uint8_t* data, size_t length);
};

/**
 * @brief TCP_ERROR frame structure
 * 
 * Frame format:
 * +-------------------+
 * | Frame Type (1B)   | = 0x13 (TCP_ERROR)
 * +-------------------+
 * | Connection ID (16B)| = UUID
 * +-------------------+
 * | Error Code (2B)   | = Error type
 * +-------------------+
 * | Message Len (2B)  | = N bytes
 * +-------------------+
 * | Message (NB)      | = Error description
 * +-------------------+
 */
struct TCPErrorFrame {
    TCPFrameType type = TCPFrameType::TCP_ERROR;
    std::array<uint8_t, 16> connection_id;
    TCPErrorCode error_code;
    std::string message;
    
    std::vector<uint8_t> serialize() const;
    static TCPErrorFrame deserialize(const uint8_t* data, size_t length);
};

/**
 * @brief TCP_ACK frame structure (for flow control)
 * 
 * Frame format:
 * +-------------------+
 * | Frame Type (1B)   | = 0x14 (TCP_ACK)
 * +-------------------+
 * | Connection ID (16B)| = UUID
 * +-------------------+
 * | Ack Seq Num (4B)  | = Last received sequence
 * +-------------------+
 * | Window Size (4B)  | = Available buffer space
 * +-------------------+
 */
struct TCPAckFrame {
    TCPFrameType type = TCPFrameType::TCP_ACK;
    std::array<uint8_t, 16> connection_id;
    uint32_t ack_sequence_number;
    uint32_t window_size;
    
    std::vector<uint8_t> serialize() const;
    static TCPAckFrame deserialize(const uint8_t* data, size_t length);
};

/**
 * @brief Helper functions for TCP protocol
 */
namespace tcp_protocol {

/**
 * @brief Parse frame type from buffer
 */
TCPFrameType get_frame_type(const uint8_t* data, size_t length);

/**
 * @brief Generate UUID for connection ID
 */
std::array<uint8_t, 16> generate_connection_id();

/**
 * @brief Convert UUID bytes to string representation
 */
std::string connection_id_to_string(const std::array<uint8_t, 16>& id);

/**
 * @brief Convert string UUID to bytes
 */
std::array<uint8_t, 16> string_to_connection_id(const std::string& str);

/**
 * @brief Network byte order conversion (htonl/ntohl for portability)
 */
uint32_t hton32(uint32_t value);
uint32_t ntoh32(uint32_t value);
uint16_t hton16(uint16_t value);
uint16_t ntoh16(uint16_t value);

}  // namespace tcp_protocol

}  // namespace proxy
}  // namespace protogate
