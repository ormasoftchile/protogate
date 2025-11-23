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
    unsigned short port)
    : io_pool_(io_pool),
      tls_manager_(tls_manager),
      token_validator_(token_validator),
      agent_registry_(agent_registry),
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
    
    // Check if registry is at capacity
    if (agent_registry_->is_full()) {
        observability::Logger::instance().warning("Agent registry at capacity, rejecting connections");
        // TODO: Implement backpressure/retry logic
        // For now, continue accepting but will reject in handshake
    }
    
    auto& io_context = io_pool_->get_io_context();
    auto ssl_context = tls_manager_->get_agent_context();
    
    auto handshake = std::make_shared<AgentHandshake>(
        io_context, *ssl_context, token_validator_, agent_registry_);
    
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
    std::shared_ptr<agent::AgentRegistry> agent_registry)
    : socket_(io_context, ssl_context),
      token_validator_(token_validator),
      agent_registry_(agent_registry) {
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
    
    while (std::getline(stream, line) && !line.empty() && line != "\r") {
        if (line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.starts_with("Authorization: ")) {
            authorization = line.substr(15);
        } else if (line.starts_with("X-Tunnel-ID: ")) {
            tunnel_id_header = line.substr(13);
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
    auto& io_context = socket_.get_executor().context();
    auto ssl_context = socket_.native_handle(); // TODO: Get proper context
    
    // For MVP, we need to transfer ownership of socket to AgentConnection
    // This requires refactoring AgentConnection to accept existing socket
    // For now, send success response
    
    observability::Logger::instance().info("Agent authenticated successfully", {
        {"tunnel_id", tunnel_id},
        {"agent_ip", agent_ip_}
    });
    
    send_response(200, "Connection established");
    
    // TODO: Create AgentConnection and register it
    // auto agent_conn = std::make_shared<agent::AgentConnection>(...);
    // agent_registry_->register_agent(tunnel_id, agent_conn);
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
