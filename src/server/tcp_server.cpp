#include "server/tcp_server.h"
#include "../observability/logger.h"
#include <boost/asio/ip/tcp.hpp>

namespace protogate {
namespace server {

using boost::asio::ip::tcp;

TCPServer::TCPServer(std::shared_ptr<IOContextPool> io_pool,
                     std::shared_ptr<proxy::TCPProxy> tcp_proxy,
                     std::shared_ptr<agent::AgentRegistry> agent_registry,
                     std::shared_ptr<storage::Cache> tunnel_cache,
                     const std::vector<uint16_t>& tcp_ports)
    : io_pool_(std::move(io_pool)),
      tcp_proxy_(std::move(tcp_proxy)),
      agent_registry_(std::move(agent_registry)),
      tunnel_cache_(std::move(tunnel_cache)),
      running_(false),
      active_connection_count_(0) {

    // Create listeners for each TCP port
    for (uint16_t port : tcp_ports) {
        auto& io_context = io_pool_->get_io_context();
        auto listener = std::make_shared<PortListener>(io_context, port);
        listeners_.push_back(listener);

        observability::Logger::instance().info("TCP listener created", {
            {"port", std::to_string(port)}
        });
    }

    observability::Logger::instance().info("TCPServer initialized", {
        {"port_count", std::to_string(tcp_ports.size())}
    });
}

void TCPServer::start() {
    if (running_) {
        observability::Logger::instance().error("TCPServer already running");
        return;
    }

    observability::Logger::instance().info("Starting TCPServer");

    // Bind and start listening on all ports
    for (auto& listener : listeners_) {
        try {
            tcp::endpoint endpoint(tcp::v4(), listener->port);
            listener->acceptor.open(endpoint.protocol());
            listener->acceptor.set_option(tcp::acceptor::reuse_address(true));
            listener->acceptor.bind(endpoint);
            listener->acceptor.listen();

            observability::Logger::instance().info("TCP listener bound", {
                {"port", std::to_string(listener->port)},
                {"endpoint", endpoint.address().to_string() + ":" + std::to_string(endpoint.port())}
            });

            start_accept(listener);

        } catch (const std::exception& e) {
            observability::Logger::instance().error("Failed to bind TCP port", {
                {"port", std::to_string(listener->port)},
                {"error", e.what()}
            });
            throw;
        }
    }

    running_ = true;

    observability::Logger::instance().info("TCPServer started", {
        {"active_ports", std::to_string(listeners_.size())}
    });
}

void TCPServer::stop() {
    if (!running_) {
        return;
    }

    observability::Logger::instance().info("Stopping TCPServer");

    // Close all acceptors
    for (auto& listener : listeners_) {
        boost::system::error_code ec;
        listener->acceptor.close(ec);
        
        if (ec) {
            observability::Logger::instance().error("Error closing TCP acceptor", {
                {"port", std::to_string(listener->port)},
                {"error", ec.message()}
            });
        }
    }

    running_ = false;

    observability::Logger::instance().info("TCPServer stopped", {
        {"active_connections", std::to_string(active_connection_count_.load())}
    });
}

void TCPServer::start_accept(std::shared_ptr<PortListener> listener) {
    auto socket = std::make_shared<tcp::socket>(listener->acceptor.get_executor());

    listener->acceptor.async_accept(*socket,
        [this, listener, socket](const boost::system::error_code& error) {
            handle_accept(listener, socket, error);
        });
}

void TCPServer::handle_accept(std::shared_ptr<PortListener> listener,
                              std::shared_ptr<tcp::socket> socket,
                              const boost::system::error_code& error) {
    
    if (error) {
        observability::Logger::instance().error("TCP accept error", {
            {"port", std::to_string(listener->port)},
            {"error", error.message()}
        });

        if (running_) {
            start_accept(listener);  // Continue accepting
        }
        return;
    }

    // Get client endpoint before moving socket
    std::string client_ip = socket->remote_endpoint().address().to_string();
    uint16_t client_port = socket->remote_endpoint().port();

    observability::Logger::instance().info("TCP connection accepted", {
        {"server_port", std::to_string(listener->port)},
        {"client_ip", client_ip},
        {"client_port", std::to_string(client_port)}
    });

    // Find tunnel for this port
    std::string tunnel_id = find_tunnel_for_port(listener->port);
    
    if (tunnel_id.empty()) {
        observability::Logger::instance().error("No tunnel configured for port", {
            {"port", std::to_string(listener->port)},
            {"client_ip", client_ip}
        });
        
        // Close connection immediately
        boost::system::error_code ec;
        socket->close(ec);
        
        // Continue accepting
        if (running_) {
            start_accept(listener);
        }
        return;
    }

    // Increment connection counter
    listener->connection_count++;
    active_connection_count_++;

    observability::Logger::instance().debug("Creating TCP proxy connection", {
        {"tunnel_id", tunnel_id},
        {"port", std::to_string(listener->port)},
        {"client_ip", client_ip}
    });

    // Create TCP proxy connection
    // Pass a callback to decrement counter when connection closes
    try {
        auto weak_listener = std::weak_ptr<PortListener>(listener);
        
        tcp_proxy_->create_connection(
            tunnel_id,
            std::move(socket),
            [this, weak_listener](const std::string& conn_id, const boost::system::error_code& ec) {
                // Connection closed callback
                if (auto listener_ptr = weak_listener.lock()) {
                    handle_connection_closed(listener_ptr);
                }
            }
        );

    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to create TCP proxy connection", {
            {"tunnel_id", tunnel_id},
            {"port", std::to_string(listener->port)},
            {"error", e.what()}
        });

        // Decrement counter on error
        listener->connection_count--;
        active_connection_count_--;
    }

    // Continue accepting connections
    if (running_) {
        start_accept(listener);
    }
}

std::string TCPServer::find_tunnel_for_port(uint16_t port) const {
    // Check cached mapping first
    auto it = port_to_tunnel_.find(port);
    if (it != port_to_tunnel_.end()) {
        return it->second;
    }

    // Query tunnel cache for port mapping
    // In a full implementation, tunnels would register their TCP ports
    // For now, we'll use a simple lookup pattern:
    // The tunnel cache should have entries like "tcp:9100" -> tunnel_id
    
    std::string port_key = "tcp:" + std::to_string(port);
    
    try {
        auto tunnel_opt = tunnel_cache_->get(port_key);
        if (tunnel_opt) {
            // Cache the mapping for future lookups
            const_cast<TCPServer*>(this)->port_to_tunnel_[port] = *tunnel_opt;
            return *tunnel_opt;
        }
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Error looking up tunnel for port", {
            {"port", std::to_string(port)},
            {"error", e.what()}
        });
    }

    return "";
}

void TCPServer::handle_connection_closed(std::shared_ptr<PortListener> listener) {
    if (listener->connection_count > 0) {
        listener->connection_count--;
    }
    
    if (active_connection_count_ > 0) {
        active_connection_count_--;
    }

    observability::Logger::instance().debug("TCP connection closed", {
        {"port", std::to_string(listener->port)},
        {"remaining_connections", std::to_string(listener->connection_count)}
    });
}

}  // namespace server
}  // namespace protogate
