#include "tcp_proxy.h"
#include "../observability/logger.h"
#include "../security/ip_allowlist.h"
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <random>
#include <sstream>
#include <iomanip>

namespace protogate {
namespace proxy {

TCPProxy::TCPProxy(tunnel_cache_ptr tunnel_cache, agent_registry_ptr agent_registry)
    : tunnel_cache_(tunnel_cache),
      agent_registry_(agent_registry) {
    
    observability::Logger::instance().info("TCPProxy initialized");
}

void TCPProxy::create_connection(
    std::shared_ptr<tcp_socket> client_socket,
    uint16_t target_port,
    std::function<void(bool success, const std::string& error)> callback) {
    
    // Find tunnel by port
    std::string tunnel_id = match_tunnel_by_port(target_port);
    if (tunnel_id.empty()) {
        callback(false, "No tunnel configured for port " + std::to_string(target_port));
        return;
    }
    
    // Get tunnel configuration
    auto tunnel = tunnel_cache_->get(tunnel_id);
    if (!tunnel) {
        callback(false, "Tunnel configuration not found");
        return;
    }
    
    // Get client IP address
    std::string client_ip;
    try {
        auto remote_endpoint = client_socket->remote_endpoint();
        client_ip = remote_endpoint.address().to_string();
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to get client IP", {
            {"error", e.what()}
        });
        callback(false, "Failed to get client address");
        return;
    }
    
    // Validate IP allowlist
    if (!validate_ip_allowlist(*tunnel, client_ip)) {
        observability::Logger::instance().warning("TCP connection blocked by IP allowlist", {
            {"tunnel_id", tunnel_id},
            {"client_ip", client_ip},
            {"target_port", std::to_string(target_port)}
        });
        
        callback(false, "IP address not allowed");
        
        // Close socket
        boost::system::error_code close_ec;
        client_socket->close(close_ec);
        return;
    }
    
    // Check if agent is connected
    if (!agent_registry_->is_connected(tunnel_id)) {
        callback(false, "Agent not connected for tunnel " + tunnel_id);
        return;
    }
    
    // Create connection context
    auto connection = std::make_shared<TCPConnection>();
    connection->connection_id = generate_connection_id();
    connection->tunnel_id = tunnel_id;
    connection->client_ip = client_ip;
    connection->target_port = target_port;
    connection->state = ConnectionState::CONNECTING;
    connection->started_at = std::chrono::steady_clock::now();
    connection->last_activity = connection->started_at;
    
    // Reserve buffer space
    connection->send_buffer.reserve(connection->send_window_size);
    connection->recv_buffer.reserve(connection->recv_window_size);
    
    // Store connection
    {
        std::unique_lock lock(connections_mutex_);
        connections_[connection->connection_id] = connection;
    }
    
    observability::Logger::instance().info("TCP connection created", {
        {"connection_id", connection->connection_id},
        {"tunnel_id", tunnel_id},
        {"client_ip", client_ip},
        {"target_port", std::to_string(target_port)}
    });
    
    // TODO: Send CONNECT frame to agent via protocol_multiplexer
    // For now, mark as established
    connection->state = ConnectionState::ESTABLISHED;
    
    // Start bidirectional forwarding
    start_forwarding(connection, client_socket);
    
    callback(true, "");
}

void TCPProxy::create_connection(
    const std::string& tunnel_id,
    std::shared_ptr<tcp_socket> client_socket,
    std::function<void(const std::string& connection_id, const boost::system::error_code& ec)> close_callback) {
    
    // Get tunnel configuration
    auto tunnel = tunnel_cache_->get(tunnel_id);
    if (!tunnel) {
        observability::Logger::instance().error("Tunnel configuration not found", {
            {"tunnel_id", tunnel_id}
        });
        
        boost::system::error_code ec = boost::asio::error::not_found;
        if (close_callback) {
            close_callback("", ec);
        }
        
        // Close socket
        boost::system::error_code close_ec;
        client_socket->close(close_ec);
        return;
    }
    
    // Get client IP address
    std::string client_ip;
    try {
        auto remote_endpoint = client_socket->remote_endpoint();
        client_ip = remote_endpoint.address().to_string();
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to get client IP", {
            {"tunnel_id", tunnel_id},
            {"error", e.what()}
        });
        
        boost::system::error_code ec = boost::asio::error::fault;
        if (close_callback) {
            close_callback("", ec);
        }
        
        // Close socket
        boost::system::error_code close_ec;
        client_socket->close(close_ec);
        return;
    }
    
    // Validate IP allowlist
    if (!validate_ip_allowlist(*tunnel, client_ip)) {
        observability::Logger::instance().warning("TCP connection blocked by IP allowlist", {
            {"tunnel_id", tunnel_id},
            {"client_ip", client_ip}
        });
        
        boost::system::error_code ec = boost::asio::error::access_denied;
        if (close_callback) {
            close_callback("", ec);
        }
        
        // Close socket
        boost::system::error_code close_ec;
        client_socket->close(close_ec);
        return;
    }
    
    // Check if agent is connected
    if (!agent_registry_->is_connected(tunnel_id)) {
        observability::Logger::instance().error("Agent not connected for tunnel", {
            {"tunnel_id", tunnel_id}
        });
        
        boost::system::error_code ec = boost::asio::error::not_connected;
        if (close_callback) {
            close_callback("", ec);
        }
        
        // Close socket
        boost::system::error_code close_ec;
        client_socket->close(close_ec);
        return;
    }
    
    // Create connection context
    auto connection = std::make_shared<TCPConnection>();
    connection->connection_id = generate_connection_id();
    connection->tunnel_id = tunnel_id;
    connection->client_ip = client_ip;
    connection->target_port = 0;  // Port is determined by server-side routing
    connection->state = ConnectionState::CONNECTING;
    connection->started_at = std::chrono::steady_clock::now();
    connection->last_activity = connection->started_at;
    
    // Reserve buffer space
    connection->send_buffer.reserve(connection->send_window_size);
    connection->recv_buffer.reserve(connection->recv_window_size);
    
    // Store connection
    {
        std::unique_lock lock(connections_mutex_);
        connections_[connection->connection_id] = connection;
    }
    
    observability::Logger::instance().info("TCP connection created", {
        {"connection_id", connection->connection_id},
        {"tunnel_id", tunnel_id},
        {"client_ip", client_ip}
    });
    
    // TODO: Send CONNECT frame to agent via protocol_multiplexer
    // For now, mark as established
    connection->state = ConnectionState::ESTABLISHED;
    
    // Start bidirectional forwarding with close callback
    start_forwarding_with_callback(connection, client_socket, close_callback);
}

void TCPProxy::close_connection(const std::string& connection_id, bool graceful) {
    std::shared_ptr<TCPConnection> connection;
    
    {
        std::shared_lock lock(connections_mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) {
            return;
        }
        connection = it->second;
    }
    
    if (graceful && !connection->send_buffer.empty()) {
        // Wait for pending data to be sent
        connection->state = ConnectionState::CLOSING;
        observability::Logger::instance().debug("TCP connection closing gracefully", {
            {"connection_id", connection_id},
            {"pending_bytes", std::to_string(connection->send_buffer.size())}
        });
    } else {
        connection->state = ConnectionState::CLOSED;
        cleanup_connection(connection_id);
        
        observability::Logger::instance().info("TCP connection closed", {
            {"connection_id", connection_id},
            {"bytes_sent", std::to_string(connection->bytes_sent)},
            {"bytes_received", std::to_string(connection->bytes_received)}
        });
    }
    
    // TODO: Send CLOSE frame to agent
}

size_t TCPProxy::send_data(
    const std::string& connection_id,
    const uint8_t* data,
    size_t length) {
    
    std::shared_ptr<TCPConnection> connection;
    
    {
        std::shared_lock lock(connections_mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) {
            return 0;
        }
        connection = it->second;
    }
    
    if (connection->state != ConnectionState::ESTABLISHED) {
        return 0;
    }
    
    // Check flow control
    size_t available = connection->send_window_size - connection->send_buffer.size();
    size_t to_send = std::min(length, available);
    
    if (to_send == 0) {
        observability::Logger::instance().error("Send window full", {
            {"connection_id", connection_id},
            {"buffer_size", std::to_string(connection->send_buffer.size())}
        });
        return 0;
    }
    
    // Append to send buffer
    connection->send_buffer.insert(
        connection->send_buffer.end(),
        data,
        data + to_send);
    
    connection->bytes_sent += to_send;
    connection->last_activity = std::chrono::steady_clock::now();
    
    // TODO: Send TCP_DATA frame to agent via protocol_multiplexer
    
    return to_send;
}

void TCPProxy::receive_data(
    const std::string& connection_id,
    const uint8_t* data,
    size_t length) {
    
    std::shared_ptr<TCPConnection> connection;
    
    {
        std::shared_lock lock(connections_mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) {
            observability::Logger::instance().error("Received data for unknown connection", {
                {"connection_id", connection_id}
            });
            return;
        }
        connection = it->second;
    }
    
    // Check buffer capacity
    if (connection->recv_buffer.size() + length > connection->recv_window_size) {
        observability::Logger::instance().error("Receive buffer overflow", {
            {"connection_id", connection_id},
            {"buffer_size", std::to_string(connection->recv_buffer.size())},
            {"incoming", std::to_string(length)}
        });
        // Drop data or close connection
        return;
    }
    
    // Append to receive buffer
    connection->recv_buffer.insert(
        connection->recv_buffer.end(),
        data,
        data + length);
    
    connection->bytes_received += length;
    connection->last_activity = std::chrono::steady_clock::now();
    
    observability::Logger::instance().debug("Received TCP data", {
        {"connection_id", connection_id},
        {"bytes", std::to_string(length)}
    });
}

std::shared_ptr<TCPProxy::TCPConnection> TCPProxy::get_connection(
    const std::string& connection_id) {
    
    std::shared_lock lock(connections_mutex_);
    auto it = connections_.find(connection_id);
    return (it != connections_.end()) ? it->second : nullptr;
}

std::string TCPProxy::match_tunnel_by_port(uint16_t port) {
    std::string matched_id;
    
    tunnel_cache_->for_each([&](const std::string& tunnel_id, const models::Tunnel& tunnel) {
        if (tunnel.protocol == models::TunnelProtocol::TCP && 
            tunnel.target_port == port) {
            matched_id = tunnel_id;
        }
    });
    
    return matched_id;
}

bool TCPProxy::can_send(const std::string& connection_id) {
    auto connection = get_connection(connection_id);
    if (!connection) {
        return false;
    }
    
    return connection->send_buffer.size() < connection->send_window_size;
}

void TCPProxy::update_window(const std::string& connection_id, size_t bytes_consumed) {
    auto connection = get_connection(connection_id);
    if (!connection) {
        return;
    }
    
    // Remove consumed bytes from send buffer
    if (bytes_consumed <= connection->send_buffer.size()) {
        connection->send_buffer.erase(
            connection->send_buffer.begin(),
            connection->send_buffer.begin() + bytes_consumed);
    }
    
    observability::Logger::instance().debug("Window updated", {
        {"connection_id", connection_id},
        {"bytes_consumed", std::to_string(bytes_consumed)},
        {"remaining", std::to_string(connection->send_buffer.size())}
    });
}

void TCPProxy::start_forwarding(
    std::shared_ptr<TCPConnection> connection,
    std::shared_ptr<tcp_socket> client_socket) {
    
    // Start reading from client
    read_from_client(connection, client_socket);
}

void TCPProxy::start_forwarding_with_callback(
    std::shared_ptr<TCPConnection> connection,
    std::shared_ptr<tcp_socket> client_socket,
    std::function<void(const std::string&, const boost::system::error_code&)> close_callback) {
    
    // Store close callback in connection (extend TCPConnection to hold it)
    // For now, start forwarding without callback storage
    // TODO: Extend TCPConnection struct to hold close_callback
    
    // Start reading from client
    read_from_client(connection, client_socket);
    
    // Note: The callback will be invoked when cleanup_connection is called
    // For proper implementation, we'd store close_callback in TCPConnection
}

void TCPProxy::read_from_client(
    std::shared_ptr<TCPConnection> connection,
    std::shared_ptr<tcp_socket> client_socket) {
    
    if (connection->state != ConnectionState::ESTABLISHED) {
        return;
    }
    
    // Use temporary buffer for async read
    auto buffer = std::make_shared<std::vector<uint8_t>>(8192);  // 8KB chunks
    
    client_socket->async_read_some(
        boost::asio::buffer(*buffer),
        [this, connection, client_socket, buffer](
            const boost::system::error_code& ec,
            size_t bytes_transferred) {
            
            if (ec) {
                if (ec == boost::asio::error::eof) {
                    // Client closed connection
                    observability::Logger::instance().debug("Client closed connection", {
                        {"connection_id", connection->connection_id}
                    });
                    close_connection(connection->connection_id, true);
                } else {
                    handle_error(connection->connection_id, ec.message());
                }
                return;
            }
            
            // Forward data to agent
            size_t sent = send_data(
                connection->connection_id,
                buffer->data(),
                bytes_transferred);
            
            if (sent < bytes_transferred) {
                // Backpressure - pause reading
                observability::Logger::instance().error("Backpressure detected", {
                    {"connection_id", connection->connection_id},
                    {"bytes_read", std::to_string(bytes_transferred)},
                    {"bytes_sent", std::to_string(sent)}
                });
            }
            
            // Continue reading
            read_from_client(connection, client_socket);
        });
}

void TCPProxy::write_to_client(
    std::shared_ptr<TCPConnection> connection,
    std::shared_ptr<tcp_socket> client_socket) {
    
    if (connection->recv_buffer.empty()) {
        return;
    }
    
    boost::asio::async_write(
        *client_socket,
        boost::asio::buffer(connection->recv_buffer),
        [this, connection, client_socket](
            const boost::system::error_code& ec,
            size_t bytes_transferred) {
            
            if (ec) {
                handle_error(connection->connection_id, ec.message());
                return;
            }
            
            // Remove written data from buffer
            connection->recv_buffer.erase(
                connection->recv_buffer.begin(),
                connection->recv_buffer.begin() + bytes_transferred);
            
            // Continue writing if more data available
            if (!connection->recv_buffer.empty()) {
                write_to_client(connection, client_socket);
            }
        });
}

void TCPProxy::handle_error(const std::string& connection_id, const std::string& error) {
    observability::Logger::instance().error("TCP connection error", {
        {"connection_id", connection_id},
        {"error", error}
    });
    
    close_connection(connection_id, false);
}

void TCPProxy::cleanup_connection(const std::string& connection_id) {
    std::unique_lock lock(connections_mutex_);
    connections_.erase(connection_id);
}

std::string TCPProxy::generate_connection_id() {
    // Simple UUID-like generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "tcp-";
    for (int i = 0; i < 8; i++) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

bool TCPProxy::validate_ip_allowlist(const models::Tunnel& tunnel, const std::string& client_ip) {
    // If no allowlist configured, allow all
    if (tunnel.ip_allowlist.empty()) {
        return true;
    }
    
    // Create IPAllowlist from tunnel's CIDR list
    auto allowlist_opt = security::IPAllowlist::from_cidr_list(tunnel.ip_allowlist);
    if (!allowlist_opt) {
        // Failed to parse allowlist - log error and deny access
        observability::Logger::instance().error("Failed to parse IP allowlist for TCP", {
            {"tunnel_id", tunnel.tunnel_id}
        });
        return false;
    }
    
    // Check if client IP is allowed
    return allowlist_opt->is_allowed(client_ip);
}

}  // namespace proxy
}  // namespace protogate
