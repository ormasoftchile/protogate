#include "tcp_protocol.h"
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace protogate {
namespace proxy {

// TCPOpenFrame implementation
std::vector<uint8_t> TCPOpenFrame::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(19);  // 1 + 16 + 2
    
    buffer.push_back(static_cast<uint8_t>(type));
    buffer.insert(buffer.end(), connection_id.begin(), connection_id.end());
    
    uint16_t port_be = tcp_protocol::hton16(target_port);
    buffer.push_back((port_be >> 8) & 0xFF);
    buffer.push_back(port_be & 0xFF);
    
    return buffer;
}

TCPOpenFrame TCPOpenFrame::deserialize(const uint8_t* data, size_t length) {
    if (length < 19) {
        throw std::runtime_error("TCPOpenFrame: buffer too small");
    }
    
    TCPOpenFrame frame;
    frame.type = static_cast<TCPFrameType>(data[0]);
    
    if (frame.type != TCPFrameType::TCP_OPEN) {
        throw std::runtime_error("TCPOpenFrame: invalid frame type");
    }
    
    std::copy(data + 1, data + 17, frame.connection_id.begin());
    
    uint16_t port_be = (static_cast<uint16_t>(data[17]) << 8) | data[18];
    frame.target_port = tcp_protocol::ntoh16(port_be);
    
    return frame;
}

// TCPDataFrame implementation
std::vector<uint8_t> TCPDataFrame::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(25 + data.size());  // 1 + 16 + 4 + 4 + data
    
    buffer.push_back(static_cast<uint8_t>(type));
    buffer.insert(buffer.end(), connection_id.begin(), connection_id.end());
    
    // Write sequence number in big-endian (network byte order)
    buffer.push_back((sequence_number >> 24) & 0xFF);
    buffer.push_back((sequence_number >> 16) & 0xFF);
    buffer.push_back((sequence_number >> 8) & 0xFF);
    buffer.push_back(sequence_number & 0xFF);
    
    // Write data length in big-endian (network byte order)
    uint32_t data_len = static_cast<uint32_t>(data.size());
    buffer.push_back((data_len >> 24) & 0xFF);
    buffer.push_back((data_len >> 16) & 0xFF);
    buffer.push_back((data_len >> 8) & 0xFF);
    buffer.push_back(data_len & 0xFF);
    
    buffer.insert(buffer.end(), data.begin(), data.end());
    
    return buffer;
}

TCPDataFrame TCPDataFrame::deserialize(const uint8_t* data_ptr, size_t length) {
    if (length < 25) {
        throw std::runtime_error("TCPDataFrame: buffer too small");
    }
    
    TCPDataFrame frame;
    frame.type = static_cast<TCPFrameType>(data_ptr[0]);
    
    if (frame.type != TCPFrameType::TCP_DATA) {
        throw std::runtime_error("TCPDataFrame: invalid frame type");
    }
    
    std::copy(data_ptr + 1, data_ptr + 17, frame.connection_id.begin());
    
    uint32_t seq_be = (static_cast<uint32_t>(data_ptr[17]) << 24) |
                      (static_cast<uint32_t>(data_ptr[18]) << 16) |
                      (static_cast<uint32_t>(data_ptr[19]) << 8) |
                      static_cast<uint32_t>(data_ptr[20]);
    frame.sequence_number = tcp_protocol::ntoh32(seq_be);
    
    uint32_t len_be = (static_cast<uint32_t>(data_ptr[21]) << 24) |
                      (static_cast<uint32_t>(data_ptr[22]) << 16) |
                      (static_cast<uint32_t>(data_ptr[23]) << 8) |
                      static_cast<uint32_t>(data_ptr[24]);
    uint32_t data_length = tcp_protocol::ntoh32(len_be);
    
    if (length < 25 + data_length) {
        throw std::runtime_error("TCPDataFrame: incomplete data");
    }
    
    frame.data.assign(data_ptr + 25, data_ptr + 25 + data_length);
    
    return frame;
}

// TCPCloseFrame implementation
std::vector<uint8_t> TCPCloseFrame::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(19);  // 1 + 16 + 2
    
    buffer.push_back(static_cast<uint8_t>(type));
    buffer.insert(buffer.end(), connection_id.begin(), connection_id.end());
    
    uint16_t reason_be = tcp_protocol::hton16(static_cast<uint16_t>(reason));
    buffer.push_back((reason_be >> 8) & 0xFF);
    buffer.push_back(reason_be & 0xFF);
    
    return buffer;
}

TCPCloseFrame TCPCloseFrame::deserialize(const uint8_t* data, size_t length) {
    if (length < 19) {
        throw std::runtime_error("TCPCloseFrame: buffer too small");
    }
    
    TCPCloseFrame frame;
    frame.type = static_cast<TCPFrameType>(data[0]);
    
    if (frame.type != TCPFrameType::TCP_CLOSE) {
        throw std::runtime_error("TCPCloseFrame: invalid frame type");
    }
    
    std::copy(data + 1, data + 17, frame.connection_id.begin());
    
    uint16_t reason_be = (static_cast<uint16_t>(data[17]) << 8) | data[18];
    frame.reason = static_cast<TCPCloseReason>(tcp_protocol::ntoh16(reason_be));
    
    return frame;
}

// TCPErrorFrame implementation
std::vector<uint8_t> TCPErrorFrame::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(21 + message.size());  // 1 + 16 + 2 + 2 + message
    
    buffer.push_back(static_cast<uint8_t>(type));
    buffer.insert(buffer.end(), connection_id.begin(), connection_id.end());
    
    uint16_t error_be = tcp_protocol::hton16(static_cast<uint16_t>(error_code));
    buffer.push_back((error_be >> 8) & 0xFF);
    buffer.push_back(error_be & 0xFF);
    
    uint16_t len_be = tcp_protocol::hton16(static_cast<uint16_t>(message.size()));
    buffer.push_back((len_be >> 8) & 0xFF);
    buffer.push_back(len_be & 0xFF);
    
    buffer.insert(buffer.end(), message.begin(), message.end());
    
    return buffer;
}

TCPErrorFrame TCPErrorFrame::deserialize(const uint8_t* data, size_t length) {
    if (length < 21) {
        throw std::runtime_error("TCPErrorFrame: buffer too small");
    }
    
    TCPErrorFrame frame;
    frame.type = static_cast<TCPFrameType>(data[0]);
    
    if (frame.type != TCPFrameType::TCP_ERROR) {
        throw std::runtime_error("TCPErrorFrame: invalid frame type");
    }
    
    std::copy(data + 1, data + 17, frame.connection_id.begin());
    
    uint16_t error_be = (static_cast<uint16_t>(data[17]) << 8) | data[18];
    frame.error_code = static_cast<TCPErrorCode>(tcp_protocol::ntoh16(error_be));
    
    uint16_t len_be = (static_cast<uint16_t>(data[19]) << 8) | data[20];
    uint16_t msg_length = tcp_protocol::ntoh16(len_be);
    
    if (length < 21 + msg_length) {
        throw std::runtime_error("TCPErrorFrame: incomplete message");
    }
    
    frame.message.assign(reinterpret_cast<const char*>(data + 21), msg_length);
    
    return frame;
}

// TCPAckFrame implementation
std::vector<uint8_t> TCPAckFrame::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.reserve(25);  // 1 + 16 + 4 + 4
    
    buffer.push_back(static_cast<uint8_t>(type));
    buffer.insert(buffer.end(), connection_id.begin(), connection_id.end());
    
    uint32_t ack_be = tcp_protocol::hton32(ack_sequence_number);
    buffer.push_back((ack_be >> 24) & 0xFF);
    buffer.push_back((ack_be >> 16) & 0xFF);
    buffer.push_back((ack_be >> 8) & 0xFF);
    buffer.push_back(ack_be & 0xFF);
    
    uint32_t win_be = tcp_protocol::hton32(window_size);
    buffer.push_back((win_be >> 24) & 0xFF);
    buffer.push_back((win_be >> 16) & 0xFF);
    buffer.push_back((win_be >> 8) & 0xFF);
    buffer.push_back(win_be & 0xFF);
    
    return buffer;
}

TCPAckFrame TCPAckFrame::deserialize(const uint8_t* data, size_t length) {
    if (length < 25) {
        throw std::runtime_error("TCPAckFrame: buffer too small");
    }
    
    TCPAckFrame frame;
    frame.type = static_cast<TCPFrameType>(data[0]);
    
    if (frame.type != TCPFrameType::TCP_ACK) {
        throw std::runtime_error("TCPAckFrame: invalid frame type");
    }
    
    std::copy(data + 1, data + 17, frame.connection_id.begin());
    
    uint32_t ack_be = (static_cast<uint32_t>(data[17]) << 24) |
                      (static_cast<uint32_t>(data[18]) << 16) |
                      (static_cast<uint32_t>(data[19]) << 8) |
                      static_cast<uint32_t>(data[20]);
    frame.ack_sequence_number = tcp_protocol::ntoh32(ack_be);
    
    uint32_t win_be = (static_cast<uint32_t>(data[21]) << 24) |
                      (static_cast<uint32_t>(data[22]) << 16) |
                      (static_cast<uint32_t>(data[23]) << 8) |
                      static_cast<uint32_t>(data[24]);
    frame.window_size = tcp_protocol::ntoh32(win_be);
    
    return frame;
}

// Helper functions
namespace tcp_protocol {

TCPFrameType get_frame_type(const uint8_t* data, size_t length) {
    if (length < 1) {
        throw std::runtime_error("Buffer too small to read frame type");
    }
    return static_cast<TCPFrameType>(data[0]);
}

std::array<uint8_t, 16> generate_connection_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    std::array<uint8_t, 16> id;
    
    uint64_t high = dis(gen);
    uint64_t low = dis(gen);
    
    std::memcpy(id.data(), &high, 8);
    std::memcpy(id.data() + 8, &low, 8);
    
    return id;
}

std::string connection_id_to_string(const std::array<uint8_t, 16>& id) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<int>(id[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            oss << '-';
        }
    }
    
    return oss.str();
}

std::array<uint8_t, 16> string_to_connection_id(const std::string& str) {
    std::array<uint8_t, 16> id;
    std::string cleaned;
    
    // Remove hyphens
    for (char c : str) {
        if (c != '-') {
            cleaned += c;
        }
    }
    
    if (cleaned.size() != 32) {
        throw std::runtime_error("Invalid connection ID string format");
    }
    
    for (size_t i = 0; i < 16; ++i) {
        id[i] = static_cast<uint8_t>(
            std::stoi(cleaned.substr(i * 2, 2), nullptr, 16)
        );
    }
    
    return id;
}

uint32_t hton32(uint32_t value) {
    return htonl(value);
}

uint32_t ntoh32(uint32_t value) {
    return ntohl(value);
}

uint16_t hton16(uint16_t value) {
    return htons(value);
}

uint16_t ntoh16(uint16_t value) {
    return ntohs(value);
}

}  // namespace tcp_protocol

}  // namespace proxy
}  // namespace protogate
