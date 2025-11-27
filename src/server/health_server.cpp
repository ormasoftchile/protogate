#include "health_server.h"
#include "../observability/logger.h"
#include <sstream>

namespace protogate {
namespace server {

HealthServer::HealthServer(
    std::shared_ptr<IOContextPool> io_pool,
    unsigned short port)
    : io_pool_(io_pool),
      port_(port),
      running_(false),
      acceptor_(io_pool->get_io_context()) {
    
    observability::Logger::instance().info("HealthServer initialized", {
        {"port", std::to_string(port_)}
    });
}

void HealthServer::start() {
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
        
        observability::Logger::instance().info("HealthServer started", {
            {"port", std::to_string(port_)}
        });
        
        do_accept();
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to start HealthServer", {
            {"error", e.what()},
            {"port", std::to_string(port_)}
        });
        throw;
    }
}

void HealthServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    acceptor_.close();
    
    observability::Logger::instance().info("HealthServer stopped");
}

void HealthServer::do_accept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_pool_->get_io_context());
    
    acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
        if (!ec && running_) {
            handle_connection(socket);
        }
        
        if (running_) {
            do_accept();
        }
    });
}

void HealthServer::handle_connection(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    try {
        // Read HTTP request (we don't care about the content, just respond)
        boost::asio::streambuf buffer;
        boost::asio::read_until(*socket, buffer, "\r\n\r\n");
        
        // Get health status
        auto health_json = HealthHandler::get_health_json();
        int status_code = HealthHandler::get_http_status_code();
        std::string status_text = (status_code == 200) ? "OK" : "Service Unavailable";
        
        // Build HTTP response
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << health_json.size() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << health_json;
        
        std::string response_str = response.str();
        
        // Send response
        boost::asio::write(*socket, boost::asio::buffer(response_str));
        
        observability::Logger::instance().debug("Health check served", {
            {"status_code", std::to_string(status_code)},
            {"remote_ip", socket->remote_endpoint().address().to_string()}
        });
        
    } catch (const std::exception& e) {
        observability::Logger::instance().warning("Health check connection error", {
            {"error", e.what()}
        });
    }
}

}  // namespace server
}  // namespace protogate
