#pragma once

#include "../proxy/tcp_proxy.h"
#include "../agent/agent_registry.h"
#include "../storage/cache.h"
#include "io_context_pool.h"
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

namespace protogate {
namespace server {

/**
 * @brief TCP server for raw TCP tunneling (e.g., printer protocols)
 * 
 * Responsibilities:
 * - Accept raw TCP connections on configurable ports
 * - Route connections by port number to appropriate tunnel
 * - Create TCPProxy instances for connection handling
 * - Manage connection lifecycle and cleanup
 * - Support multiple simultaneous TCP ports (9100, 515, etc.)
 */
class TCPServer {
public:
    /**
     * @brief Initialize TCP server
     * @param io_pool IO context pool for async operations
     * @param tcp_proxy TCP proxy for connection forwarding
     * @param agent_registry Agent registry for tunnel lookup
     * @param tunnel_cache Tunnel cache for port-to-tunnel mapping
     * @param tcp_ports List of ports to listen on
     */
    TCPServer(std::shared_ptr<IOContextPool> io_pool,
             std::shared_ptr<proxy::TCPProxy> tcp_proxy,
             std::shared_ptr<agent::AgentRegistry> agent_registry,
             std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache,
             const std::vector<uint16_t>& tcp_ports);

    /**
     * @brief Start accepting connections on all configured ports
     */
    void start();

    /**
     * @brief Stop all TCP servers gracefully
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool is_running() const { return running_; }

    /**
     * @brief Get number of active TCP connections
     */
    size_t active_connections() const { return active_connection_count_; }

private:
    /**
     * @brief Per-port acceptor and state
     */
    struct PortListener {
        uint16_t port;
        boost::asio::ip::tcp::acceptor acceptor;
        size_t connection_count = 0;

        PortListener(boost::asio::io_context& io_context, uint16_t p)
            : port(p), acceptor(io_context) {}
    };

    /**
     * @brief Start accepting on a specific port
     * @param listener Port listener to start
     */
    void start_accept(std::shared_ptr<PortListener> listener);

    /**
     * @brief Handle new TCP connection
     * @param listener Port listener that accepted the connection
     * @param socket New client socket
     * @param error Error code from accept operation
     */
    void handle_accept(std::shared_ptr<PortListener> listener,
                      std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                      const boost::system::error_code& error);

    /**
     * @brief Find tunnel ID for a given port
     * @param port TCP port number
     * @return tunnel_id if found, empty string otherwise
     */
    std::string find_tunnel_for_port(uint16_t port) const;

    /**
     * @brief Handle connection closure
     * @param listener Port listener to decrement counter
     */
    void handle_connection_closed(std::shared_ptr<PortListener> listener);

    std::shared_ptr<IOContextPool> io_pool_;
    std::shared_ptr<proxy::TCPProxy> tcp_proxy_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache_;
    
    std::vector<std::shared_ptr<PortListener>> listeners_;
    std::unordered_map<uint16_t, std::string> port_to_tunnel_;  // Cache port -> tunnel_id mapping
    
    bool running_;
    std::atomic<size_t> active_connection_count_;
};

}  // namespace server
}  // namespace protogate
