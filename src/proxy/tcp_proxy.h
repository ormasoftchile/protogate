#pragma once

#include "../agent/agent_registry.h"
#include "../storage/cache.h"
#include "../models/tunnel.h"
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>

namespace protogate {
namespace proxy {

/**
 * @brief TCP stream proxy for raw byte forwarding
 * 
 * Responsibilities:
 * - Forward raw TCP streams byte-for-byte between client and agent
 * - Implement zero-copy forwarding where possible (splice/sendfile)
 * - Handle flow control with 64KB sliding window
 * - Track connection state and byte counters
 * - Implement backpressure to prevent buffer overflow
 * - Support connection timeouts and graceful shutdown
 */
class TCPProxy {
public:
    using tunnel_cache_ptr = std::shared_ptr<storage::Cache<std::string, models::Tunnel>>;
    using agent_registry_ptr = std::shared_ptr<agent::AgentRegistry>;
    using tcp_socket = boost::asio::ip::tcp::socket;
    
    /**
     * @brief TCP connection state
     */
    enum class ConnectionState {
        CONNECTING,     // Initial connection to agent
        ESTABLISHED,    // Active bidirectional data flow
        CLOSING,        // Graceful shutdown in progress
        CLOSED          // Connection terminated
    };
    
    /**
     * @brief TCP connection context
     */
    struct TCPConnection {
        std::string connection_id;      // Unique connection identifier
        std::string tunnel_id;          // Associated tunnel
        std::string client_ip;          // Client source IP address
        uint16_t target_port;           // Target port on agent side
        ConnectionState state;
        
        // Statistics
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        std::chrono::steady_clock::time_point started_at;
        std::chrono::steady_clock::time_point last_activity;
        
        // Buffers
        std::vector<uint8_t> send_buffer;
        std::vector<uint8_t> recv_buffer;
        
        // Flow control
        size_t send_window_size = 65536;   // 64KB window
        size_t recv_window_size = 65536;
    };
    
    /**
     * @brief Initialize TCP proxy
     * @param tunnel_cache Tunnel configuration cache
     * @param agent_registry Active agent connections
     */
    TCPProxy(tunnel_cache_ptr tunnel_cache, agent_registry_ptr agent_registry);
    
    /**
     * @brief Create new TCP tunnel connection
     * @param client_socket Client-side TCP socket
     * @param target_port Target port number
     * @param callback Completion callback
     */
    void create_connection(
        std::shared_ptr<tcp_socket> client_socket,
        uint16_t target_port,
        std::function<void(bool success, const std::string& error)> callback);
    
    /**
     * @brief Create new TCP tunnel connection with explicit tunnel ID
     * @param tunnel_id Target tunnel identifier
     * @param client_socket Client-side TCP socket (moved)
     * @param close_callback Callback invoked when connection closes
     */
    void create_connection(
        const std::string& tunnel_id,
        std::shared_ptr<tcp_socket> client_socket,
        std::function<void(const std::string& connection_id, const boost::system::error_code& ec)> close_callback);
    
    /**
     * @brief Close TCP connection
     * @param connection_id Connection identifier
     * @param graceful Whether to wait for pending data
     */
    void close_connection(const std::string& connection_id, bool graceful = true);
    
    /**
     * @brief Send data through TCP tunnel
     * @param connection_id Connection identifier
     * @param data Raw bytes to send
     * @param length Data length
     * @return Bytes queued for sending
     */
    size_t send_data(
        const std::string& connection_id,
        const uint8_t* data,
        size_t length);
    
    /**
     * @brief Process received data from agent
     * @param connection_id Connection identifier
     * @param data Raw bytes received
     * @param length Data length
     */
    void receive_data(
        const std::string& connection_id,
        const uint8_t* data,
        size_t length);
    
    /**
     * @brief Get connection statistics
     * @param connection_id Connection identifier
     * @return Connection context or nullptr
     */
    std::shared_ptr<TCPConnection> get_connection(const std::string& connection_id);
    
    /**
     * @brief Match port number to tunnel ID
     * @param port Target port number
     * @return Tunnel ID or empty string if not found
     */
    std::string match_tunnel_by_port(uint16_t port);
    
    /**
     * @brief Check if send buffer has capacity
     * @param connection_id Connection identifier
     * @return True if can send more data
     */
    bool can_send(const std::string& connection_id);
    
    /**
     * @brief Update flow control window
     * @param connection_id Connection identifier
     * @param bytes_consumed Bytes consumed by receiver
     */
    void update_window(const std::string& connection_id, size_t bytes_consumed);

private:
    tunnel_cache_ptr tunnel_cache_;
    agent_registry_ptr agent_registry_;
    
    // Active TCP connections
    std::unordered_map<std::string, std::shared_ptr<TCPConnection>> connections_;
    std::shared_mutex connections_mutex_;
    
    /**
     * @brief Start bidirectional forwarding
     * @param connection Connection context
     * @param client_socket Client-side socket
     */
    void start_forwarding(
        std::shared_ptr<TCPConnection> connection,
        std::shared_ptr<tcp_socket> client_socket);
    
    /**
     * @brief Start bidirectional forwarding with close callback
     * @param connection Connection context
     * @param client_socket Client-side socket
     * @param close_callback Callback when connection closes
     */
    void start_forwarding_with_callback(
        std::shared_ptr<TCPConnection> connection,
        std::shared_ptr<tcp_socket> client_socket,
        std::function<void(const std::string&, const boost::system::error_code&)> close_callback);
    
    /**
     * @brief Read from client socket and forward to agent
     * @param connection Connection context
     * @param client_socket Client-side socket
     */
    void read_from_client(
        std::shared_ptr<TCPConnection> connection,
        std::shared_ptr<tcp_socket> client_socket);
    
    /**
     * @brief Write to client socket from agent data
     * @param connection Connection context
     * @param client_socket Client-side socket
     */
    void write_to_client(
        std::shared_ptr<TCPConnection> connection,
        std::shared_ptr<tcp_socket> client_socket);
    
    /**
     * @brief Handle connection error
     * @param connection_id Connection identifier
     * @param error Error message
     */
    void handle_error(const std::string& connection_id, const std::string& error);
    
    /**
     * @brief Cleanup closed connection
     * @param connection_id Connection identifier
     */
    void cleanup_connection(const std::string& connection_id);
    
    /**
     * @brief Generate unique connection ID
     * @return UUID string
     */
    std::string generate_connection_id();
    
    /**
     * @brief Validate client IP against tunnel's allowlist
     * @param tunnel Tunnel configuration
     * @param client_ip Client IP address
     * @return True if allowed, false if blocked
     */
    bool validate_ip_allowlist(const models::Tunnel& tunnel, const std::string& client_ip);
};

}  // namespace proxy
}  // namespace protogate
