#include "agent_server.h"
#include "../observability/logger.h"
#include "../agent/agent_connection.h"

namespace protogate {
namespace server {

AgentServer::AgentServer(
    std::shared_ptr<IOContextPool> io_pool,
    std::shared_ptr<security::TLSManager> tls_manager,
    std::shared_ptr<security::TokenValidator> token_validator,
    std::shared_ptr<agent::AgentRegistry> agent_registry,
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache,
    unsigned short port)
    : io_pool_(io_pool),
      tls_manager_(tls_manager),
      token_validator_(token_validator),
      agent_registry_(agent_registry),
      tunnel_cache_(tunnel_cache),
      port_(port),
      running_(false),
      acceptor_(io_pool->get_io_context()) {
    
    observability::Logger::instance().info("AgentServer initialized", {
        {"port", std::to_string(port_)}
    });
}

void AgentServer::start() {
    if (running_) {
        return;
    }
    
    try {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port_);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
        
        running_ = true;
        
        observability::Logger::instance().info("AgentServer started", {
            {"port", std::to_string(port_)}
        });
        
        do_accept();
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to start AgentServer", {
            {"error", e.what()},
            {"port", std::to_string(port_)}
        });
        throw;
    }
}

void AgentServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    acceptor_.close();
    
    observability::Logger::instance().info("AgentServer stopped");
}

void AgentServer::do_accept() {
    if (!running_) {
        return;
    }
    
    // Check if registry is at capacity (T106: Connection limit enforcement)
    if (agent_registry_->is_full()) {
        observability::Logger::instance().warning("Agent registry at capacity, rejecting connection", {
            {"current_capacity", std::to_string(agent_registry_->count())},
            {"max_capacity", std::to_string(agent_registry_->max_capacity())}
        });
        
        // Accept but immediately send 503 Service Unavailable and close
        auto& io_context = io_pool_->get_io_context();
        auto ssl_context = tls_manager_->get_agent_context();
        auto temp_socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            io_context, *ssl_context);
        
        acceptor_.async_accept(temp_socket->lowest_layer(),
            [this, temp_socket](const boost::system::error_code& ec) {
                if (!ec) {
                    // Send 503 Service Unavailable response
                    const std::string response = 
                        "HTTP/1.1 503 Service Unavailable\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Server at capacity. Maximum concurrent agents reached (50).\r\n";
                    
                    boost::asio::async_write(*temp_socket, boost::asio::buffer(response),
                        [temp_socket](const boost::system::error_code&, std::size_t) {
                            // Close socket after sending response
                            boost::system::error_code ec;
                            temp_socket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                            temp_socket->lowest_layer().close(ec);
                        });
                }
                
                // Continue accepting (retry after rejection)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                do_accept();
            });
        return;
    }
    
    auto& io_context = io_pool_->get_io_context();
    auto ssl_context = tls_manager_->get_agent_context();
    
    auto handshake = std::make_shared<AgentHandshake>(
        io_context, *ssl_context, token_validator_, agent_registry_, tunnel_cache_);
    
    acceptor_.async_accept(handshake->socket(),
        [this, handshake](const boost::system::error_code& ec) {
            if (!ec) {
                handshake->start();
            } else {
                observability::Logger::instance().warning("Agent accept error", {
                    {"error", ec.message()}
                });
            }
            
            // Accept next connection
            do_accept();
        });
}

// AgentHandshake implementation

AgentServer::AgentHandshake::AgentHandshake(
    boost::asio::io_context& io_context,
    boost::asio::ssl::context& ssl_context,
    std::shared_ptr<security::TokenValidator> token_validator,
    std::shared_ptr<agent::AgentRegistry> agent_registry,
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache)
    : socket_(io_context, ssl_context),
      token_validator_(token_validator),
      agent_registry_(agent_registry),
      tunnel_cache_(tunnel_cache) {
}

void AgentServer::AgentHandshake::start() {
    // Get agent IP
    try {
        auto endpoint = socket_.lowest_layer().remote_endpoint();
        agent_ip_ = endpoint.address().to_string();
    } catch (const std::exception& e) {
        observability::Logger::instance().warning("Failed to get agent IP", {
            {"error", e.what()}
        });
        agent_ip_ = "unknown";
    }
    
    observability::Logger::instance().debug("Agent connecting", {
        {"agent_ip", agent_ip_}
    });
    
    do_handshake();
}

void AgentServer::AgentHandshake::do_handshake() {
    auto self = shared_from_this();
    
    socket_.async_handshake(boost::asio::ssl::stream_base::server,
        [this, self](const boost::system::error_code& ec) {
            if (ec) {
                handle_error(ec);
                return;
            }
            
            observability::Logger::instance().debug("Agent TLS handshake completed", {
                {"agent_ip", agent_ip_}
            });
            
            do_read_auth();
        });
}

void AgentServer::AgentHandshake::do_read_auth() {
    auto self = shared_from_this();
    
    // Read HTTP/2 CONNECT request or binary handshake frame
    socket_.async_read_some(boost::asio::buffer(buffer_),
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                handle_error(ec);
                return;
            }
            
            auth_buffer_.append(buffer_.data(), bytes_transferred);
            
            // For simplicity, assume HTTP-style authentication for MVP
            // TODO: Support binary protocol for TCP tunnels
            
            // Check if we have complete request (ends with \r\n\r\n)
            if (auth_buffer_.find("\r\n\r\n") != std::string::npos) {
                authenticate(auth_buffer_);
            } else {
                do_read_auth();
            }
        });
}

void AgentServer::AgentHandshake::authenticate(const std::string& auth_data) {
    // Parse HTTP headers to extract Authorization and X-Tunnel-ID
    std::istringstream stream(auth_data);
    std::string line;
    
    std::string authorization;
    std::string tunnel_id_header;
    std::string tcp_ports_str;
    
    while (std::getline(stream, line) && !line.empty() && line != "\r") {
        if (line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.compare(0, 15, "Authorization: ") == 0) {
            authorization = line.substr(15);
        } else if (line.compare(0, 13, "X-Tunnel-ID: ") == 0) {
            tunnel_id_header = line.substr(13);
        } else if (line.compare(0, 13, "X-TCP-Ports: ") == 0) {
            tcp_ports_str = line.substr(13);
        }
    }
    
    // Validate token
    auto result = token_validator_->validate(authorization);
    
    if (!result.valid) {
        observability::Logger::instance().warning("Agent authentication failed", {
            {"agent_ip", agent_ip_},
            {"error", result.error_message}
        });
        
        send_response(401, result.error_message);
        return;
    }
    
    std::string tunnel_id = result.tunnel_id;
    
    // Check if agent already connected
    if (agent_registry_->is_connected(tunnel_id)) {
        observability::Logger::instance().warning("Agent already connected", {
            {"tunnel_id", tunnel_id},
            {"agent_ip", agent_ip_}
        });
        
        send_response(409, "Tunnel already connected");
        return;
    }
    
    // Create AgentConnection
    // TODO: Pass TLS socket and io_context to AgentConnection
    // auto& io_context = socket_.get_executor().context();
    // auto ssl_context = socket_.native_handle();
    
    // For MVP, we need to transfer ownership of socket to AgentConnection
    // This requires refactoring AgentConnection to accept existing socket
    // For now, send success response
    
    observability::Logger::instance().info("Agent authenticated successfully", {
        {"tunnel_id", tunnel_id},
        {"agent_ip", agent_ip_},
        {"tcp_ports", tcp_ports_str}
    });
    
    // Register TCP tunnel configurations if provided
    if (!tcp_ports_str.empty()) {
        // Get existing tunnel to use its target configuration
        auto existing_tunnel = tunnel_cache_->get(tunnel_id);
        if (!existing_tunnel) {
            observability::Logger::instance().error("Cannot register TCP ports - tunnel not found", {
                {"tunnel_id", tunnel_id}
            });
        } else {
            // Parse comma-separated ports
            std::istringstream port_stream(tcp_ports_str);
            std::string port_str;
            while (std::getline(port_stream, port_str, ',')) {
                // Trim whitespace
                port_str.erase(0, port_str.find_first_not_of(" \t"));
                port_str.erase(port_str.find_last_not_of(" \t") + 1);
                
                if (!port_str.empty()) {
                    uint16_t server_port = static_cast<uint16_t>(std::stoi(port_str));
                    
                    // Create TCP tunnel configuration with SAME target as existing tunnel
                    // The server_port is what clients connect to, target is where agent forwards
                    std::string tcp_tunnel_key = tunnel_id + ":tcp:" + std::to_string(server_port);
                    models::Tunnel tcp_tunnel;
                    tcp_tunnel.tunnel_id = tunnel_id;
                    tcp_tunnel.protocol = models::TunnelProtocol::TCP;
                    tcp_tunnel.target_host = existing_tunnel->target_host;
                    tcp_tunnel.target_port = existing_tunnel->target_port;
                    tcp_tunnel.status = models::TunnelStatus::ACTIVE;
                    tcp_tunnel.rate_limit_rpm = 0;
                    tcp_tunnel.created_at = std::chrono::system_clock::now();
                    tcp_tunnel.updated_at = tcp_tunnel.created_at;
                    
                    // Store with unique key so it doesn't overwrite HTTP tunnel
                    auto ttl = std::chrono::hours(24);
                    tunnel_cache_->put(tcp_tunnel_key, tcp_tunnel, ttl);
                    
                    observability::Logger::instance().info("TCP tunnel registered", {
                        {"tunnel_id", tunnel_id},
                        {"server_port", std::to_string(server_port)},
                        {"target", existing_tunnel->target_host + ":" + std::to_string(existing_tunnel->target_port)},
                        {"cache_key", tcp_tunnel_key}
                    });
                }
            }
        }
    }
    
    send_response(200, "Connection established");
    
    // Create AgentConnection from authenticated socket
    auto agent_conn = std::make_shared<agent::AgentConnection>(
        std::move(socket_), tunnel_id);
    
    // Register agent and start session
    agent_registry_->register_agent(tunnel_id, agent_conn);
    
    // Start heartbeat and HTTP/2 session
    agent_conn->start([this, tunnel_id](const std::string& disconnected_tunnel) {
        agent_registry_->unregister_agent(disconnected_tunnel);
        observability::Logger::instance().info("Agent disconnected", {
            {"tunnel_id", disconnected_tunnel}
        });
    });
}

void AgentServer::AgentHandshake::send_response(int status_code, const std::string& message) {
    auto self = shared_from_this();
    
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << message << "\r\n";
    response << "Content-Length: 0\r\n";
    response << "\r\n";
    
    std::string response_str = response.str();
    
    boost::asio::async_write(socket_, boost::asio::buffer(response_str),
        [this, self](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                handle_error(ec);
                return;
            }
            
            // If authentication failed, close connection
            // If successful, keep connection open (will be transferred to AgentConnection)
        });
}

void AgentServer::AgentHandshake::handle_error(const boost::system::error_code& ec) {
    if (ec != boost::asio::error::eof && 
        ec != boost::asio::error::operation_aborted) {
        observability::Logger::instance().warning("Agent handshake error", {
            {"agent_ip", agent_ip_},
            {"error", ec.message()}
        });
    }
}

}  // namespace server
}  // namespace protogate
