#pragma once

#include <string>
#include <map>
#include <memory>
#include <array>
#include <functional>
#include <boost/asio.hpp>
#include "../utils/logger.h"

namespace protogate {
namespace agent {

// Forward declarations
struct TCPConnection;
using tcp = boost::asio::ip::tcp;

// Callback for sending TCP frames back to server
using TCPFrameSendCallback = std::function<void(const std::vector<uint8_t>& frame_data)>;

/**
 * @brief Manages TCP connections forwarded from server to local services
 * 
 * Receives TCP frames from server (TCP_OPEN, TCP_DATA, TCP_CLOSE, TCP_ERROR),
 * opens local TCP connections, forwards data bidirectionally.
 */
class TCPForwarder : public std::enable_shared_from_this<TCPForwarder> {
public:
    /**
     * @brief Initialize TCP forwarder
     * @param io_context Boost.Asio IO context for async operations
     * @param target_host Target hostname for local connections
     * @param target_port Target port for local connections
     */
    TCPForwarder(boost::asio::io_context& io_context,
                 const std::string& target_host,
                 uint16_t target_port);
    
    ~TCPForwarder();
    
    /**
     * @brief Set callback for sending TCP frames to server
     * @param callback Function to call when sending frames
     */
    void set_send_callback(TCPFrameSendCallback callback);
    
    /**
     * @brief Handle TCP_OPEN frame from server
     * @param connection_id 16-byte UUID identifying connection
     * @param target_port Target port (if different from default)
     */
    void handle_tcp_open(const std::array<uint8_t, 16>& connection_id, uint16_t target_port);
    
    /**
     * @brief Handle TCP_DATA frame from server
     * @param connection_id Connection UUID
     * @param data Payload to write to local socket
     */
    void handle_tcp_data(const std::array<uint8_t, 16>& connection_id, 
                         const std::vector<uint8_t>& data);
    
    /**
     * @brief Handle TCP_CLOSE frame from server
     * @param connection_id Connection UUID
     */
    void handle_tcp_close(const std::array<uint8_t, 16>& connection_id);
    
    /**
     * @brief Handle TCP_ERROR frame from server
     * @param connection_id Connection UUID
     * @param error_code Error code from server
     * @param message Error message
     */
    void handle_tcp_error(const std::array<uint8_t, 16>& connection_id,
                          uint16_t error_code,
                          const std::string& message);

private:
    /**
     * @brief Start reading from local socket
     */
    void start_local_read(std::shared_ptr<TCPConnection> conn);
    
    /**
     * @brief Close and cleanup connection
     */
    void close_connection(const std::array<uint8_t, 16>& connection_id);
    
    /**
     * @brief Send TCP_DATA frame to server
     */
    void send_tcp_data_frame(const std::array<uint8_t, 16>& connection_id,
                             const std::vector<uint8_t>& data,
                             uint32_t sequence_number);
    
    /**
     * @brief Send TCP_CLOSE frame to server
     */
    void send_tcp_close_frame(const std::array<uint8_t, 16>& connection_id,
                              uint16_t reason_code);
    
    /**
     * @brief Convert connection ID to string for logging
     */
    std::string connection_id_to_string(const std::array<uint8_t, 16>& id) const;
    
    boost::asio::io_context& io_context_;
    std::string target_host_;
    uint16_t target_port_;
    
    // Connection map: connection_id -> TCPConnection
    std::map<std::array<uint8_t, 16>, std::shared_ptr<TCPConnection>> connections_;
    
    // Callback for sending frames to server
    TCPFrameSendCallback send_callback_;
};

/**
 * @brief Represents a single TCP connection being forwarded
 */
struct TCPConnection {
    std::array<uint8_t, 16> connection_id;
    std::unique_ptr<tcp::socket> local_socket;
    std::array<uint8_t, 8192> read_buffer;
    uint32_t sequence_number = 0;
    bool closing = false;
    bool connected = false;  // Socket ready for writing
    std::vector<std::vector<uint8_t>> pending_data;  // Data received before connection ready
};

}  // namespace agent
}  // namespace protogate
