#include "http_server.h"
#include "../observability/logger.h"

namespace protogate {
namespace server {

HTTPServer::HTTPServer(
    std::shared_ptr<IOContextPool> io_pool,
    std::shared_ptr<security::TLSManager> tls_manager,
    std::shared_ptr<proxy::HTTPProxy> http_proxy,
    unsigned short port)
    : io_pool_(io_pool),
      tls_manager_(tls_manager),
      http_proxy_(http_proxy),
      port_(port),
      running_(false),
      acceptor_(io_pool->get_io_context()) {
    
    observability::Logger::instance().info("HTTPServer initialized", {
        {"port", std::to_string(port_)}
    });
}

void HTTPServer::start() {
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
        
        observability::Logger::instance().info("HTTPServer started", {
            {"port", std::to_string(port_)}
        });
        
        do_accept();
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to start HTTPServer", {
            {"error", e.what()},
            {"port", std::to_string(port_)}
        });
        throw;
    }
}

void HTTPServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    acceptor_.close();
    
    observability::Logger::instance().info("HTTPServer stopped");
}

void HTTPServer::do_accept() {
    if (!running_) {
        return;
    }
    
    auto& io_context = io_pool_->get_io_context();
    auto ssl_context = tls_manager_->get_client_context();
    
    auto conn = std::make_shared<Connection>(io_context, *ssl_context, http_proxy_);
    
    acceptor_.async_accept(conn->socket(),
        [this, conn](const boost::system::error_code& ec) {
            if (!ec) {
                conn->start();
            } else {
                observability::Logger::instance().warning("Accept error", {
                    {"error", ec.message()}
                });
            }
            
            // Accept next connection
            do_accept();
        });
}

// Connection implementation

HTTPServer::Connection::Connection(
    boost::asio::io_context& io_context,
    boost::asio::ssl::context& ssl_context,
    std::shared_ptr<proxy::HTTPProxy> http_proxy)
    : socket_(io_context, ssl_context),
      http_proxy_(http_proxy) {
}

void HTTPServer::Connection::start() {
    // Get client IP
    try {
        auto endpoint = socket_.lowest_layer().remote_endpoint();
        client_ip_ = endpoint.address().to_string();
    } catch (const std::exception& e) {
        observability::Logger::instance().warning("Failed to get client IP", {
            {"error", e.what()}
        });
        client_ip_ = "unknown";
    }
    
    observability::Logger::instance().debug("Client connected", {
        {"client_ip", client_ip_}
    });
    
    do_handshake();
}

void HTTPServer::Connection::do_handshake() {
    auto self = shared_from_this();
    
    socket_.async_handshake(boost::asio::ssl::stream_base::server,
        [this, self](const boost::system::error_code& ec) {
            if (ec) {
                handle_error(ec);
                return;
            }
            
            observability::Logger::instance().debug("TLS handshake completed", {
                {"client_ip", client_ip_}
            });
            
            do_read();
        });
}

void HTTPServer::Connection::do_read() {
    auto self = shared_from_this();
    
    socket_.async_read_some(boost::asio::buffer(buffer_),
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                handle_error(ec);
                return;
            }
            
            // Append to request buffer
            request_buffer_.append(buffer_.data(), bytes_transferred);
            
            // Check if we have complete request (ends with \r\n\r\n)
            if (request_buffer_.find("\r\n\r\n") != std::string::npos) {
                // Parse HTTP request
                auto request_opt = proxy::HTTPProxy::parse_request(request_buffer_);
                
                if (!request_opt) {
                    observability::Logger::instance().warning("Invalid HTTP request", {
                        {"client_ip", client_ip_}
                    });
                    
                    auto response = proxy::HTTPProxy::HTTPResponse::error(400, "Bad Request");
                    do_write(response.to_string());
                    return;
                }
                
                auto& request = *request_opt;
                request.client_ip = client_ip_;
                
                // Handle request via proxy
                http_proxy_->handle_request(request,
                    [this, self](const std::string& response, bool error) {
                        if (error) {
                            observability::Logger::instance().error("Proxy error", {
                                {"client_ip", client_ip_}
                            });
                            
                            auto error_response = proxy::HTTPProxy::HTTPResponse::error(
                                500, "Internal Server Error");
                            do_write(error_response.to_string());
                        } else {
                            do_write(response);
                        }
                    });
            } else {
                // Continue reading
                do_read();
            }
        });
}

void HTTPServer::Connection::do_write(const std::string& response) {
    auto self = shared_from_this();
    
    boost::asio::async_write(socket_, boost::asio::buffer(response),
        [this, self](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                handle_error(ec);
                return;
            }
            
            // Close connection after response (HTTP/1.0 behavior for simplicity)
            boost::system::error_code shutdown_ec;
            socket_.shutdown(shutdown_ec);
            
            observability::Logger::instance().debug("Response sent, connection closed", {
                {"client_ip", client_ip_}
            });
        });
}

void HTTPServer::Connection::handle_error(const boost::system::error_code& ec) {
    if (ec != boost::asio::error::eof && 
        ec != boost::asio::error::operation_aborted) {
        observability::Logger::instance().warning("Connection error", {
            {"client_ip", client_ip_},
            {"error", ec.message()}
        });
    }
}

}  // namespace server
}  // namespace protogate
