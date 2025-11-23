#pragma once

#include "../models/tunnel_agent.h"
#include "../utils/async_utils.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <string>
#include <functional>
#include <chrono>

namespace protogate {
namespace agent {

/**
 * @brief Manages a single agent's TLS connection and communication
 * 
 * Responsibilities:
 * - Maintain persistent TLS connection with agent
 * - Handle HTTP/2 session for HTTP tunnels (via nghttp2)
 * - Handle binary protocol framing for TCP tunnels
 * - Send heartbeat pings every 30 seconds
 * - Detect disconnection and notify registry
 * - Forward client requests to agent and return responses
 */
class AgentConnection : public std::enable_shared_from_this<AgentConnection> {
public:
    using ssl_socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    using request_callback = std::function<void(const std::string& response, bool error)>;
    using disconnect_callback = std::function<void(const std::string& tunnel_id)>;

    /**
     * @brief Connection state
     */
    enum class State {
        CONNECTING,    // TLS handshake in progress
        AUTHENTICATING, // Validating tunnel token
        CONNECTED,     // Active and ready
        DISCONNECTING, // Graceful shutdown
        DISCONNECTED   // Closed
    };

    /**
     * @brief Create agent connection
     * @param io_context Boost.Asio IO context for async operations
     * @param ssl_context SSL context for TLS connection
     * @param tunnel_id Tunnel identifier
     */
    AgentConnection(boost::asio::io_context& io_context,
                   boost::asio::ssl::context& ssl_context,
                   const std::string& tunnel_id);

    /**
     * @brief Start TLS handshake and authentication
     * @param on_disconnect Callback when connection is lost
     */
    void start(disconnect_callback on_disconnect);

    /**
     * @brief Send HTTP request to agent and receive response
     * @param request_id Unique request identifier
     * @param http_request Full HTTP request (headers + body)
     * @param callback Callback with response or error
     * @param timeout Request timeout (default 30 minutes)
     */
    void send_http_request(const std::string& request_id,
                          const std::string& http_request,
                          request_callback callback,
                          std::chrono::seconds timeout = std::chrono::minutes(30));

    /**
     * @brief Send TCP data frame to agent
     * @param connection_id TCP connection UUID
     * @param data Raw TCP payload
     * @param callback Callback when sent or error
     */
    void send_tcp_data(const std::string& connection_id,
                      const std::string& data,
                      request_callback callback);

    /**
     * @brief Send TCP connection close frame
     * @param connection_id TCP connection UUID
     */
    void send_tcp_close(const std::string& connection_id);

    /**
     * @brief Gracefully close connection
     */
    void close();

    /**
     * @brief Get current connection state
     */
    State state() const { return state_; }

    /**
     * @brief Get tunnel ID
     */
    std::string tunnel_id() const { return tunnel_id_; }

    /**
     * @brief Get underlying socket for TLS handshake
     */
    ssl_socket& socket() { return socket_; }

    /**
     * @brief Update connection statistics
     */
    void update_stats(size_t bytes_sent, size_t bytes_received);

    /**
     * @brief Get connection metadata
     */
    models::TunnelAgent get_metadata() const;

private:
    /**
     * @brief Perform TLS handshake
     */
    void do_handshake();

    /**
     * @brief Start heartbeat timer
     */
    void start_heartbeat();

    /**
     * @brief Send heartbeat ping
     */
    void send_heartbeat();

    /**
     * @brief Start reading responses from agent
     */
    void start_read();

    /**
     * @brief Handle disconnect event
     */
    void handle_disconnect();

    boost::asio::io_context& io_context_;
    ssl_socket socket_;
    std::string tunnel_id_;
    State state_;
    disconnect_callback on_disconnect_;
    
    // Heartbeat timer
    boost::asio::steady_timer heartbeat_timer_;
    std::chrono::steady_clock::time_point last_heartbeat_;
    
    // Statistics
    size_t bytes_sent_;
    size_t bytes_received_;
    std::chrono::system_clock::time_point connected_at_;
    
    // Request tracking
    std::unordered_map<std::string, request_callback> pending_requests_;
    std::mutex requests_mutex_;
};

}  // namespace agent
}  // namespace protogate
