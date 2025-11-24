#include "tls_client.h"
#include "../utils/logger.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace protogate {
namespace agent {

TLSClient::TLSClient(const std::string& host, unsigned short port, bool verify_tls)
    : host_(host), port_(port), verify_tls_(verify_tls), connected_(false) {
    
    io_context_ = std::make_unique<boost::asio::io_context>();
    
    ssl_context_ = std::make_unique<boost::asio::ssl::context>(
        boost::asio::ssl::context::tls_client);
    
    if (verify_tls) {
        ssl_context_->set_default_verify_paths();
        ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer);
    } else {
        ssl_context_->set_verify_mode(boost::asio::ssl::verify_none);
    }
}

TLSClient::~TLSClient() {
    disconnect();
}

void TLSClient::connect() {
    try {
        Logger::info("Connecting to server", {
            {"host", host_},
            {"port", std::to_string(port_)}
        });
        
        // Resolve hostname
        boost::asio::ip::tcp::resolver resolver(*io_context_);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        
        // Create SSL socket
        socket_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            *io_context_, *ssl_context_);
        
        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(socket_->native_handle(), host_.c_str())) {
            throw std::runtime_error("Failed to set SNI hostname");
        }
        
        // Connect TCP socket
        boost::asio::connect(socket_->lowest_layer(), endpoints);
        
        Logger::info("TCP connected, starting TLS handshake");
        
        // TLS handshake
        socket_->handshake(boost::asio::ssl::stream_base::client);
        
        connected_ = true;
        
        Logger::info("TLS handshake complete");
        
    } catch (const std::exception& e) {
        Logger::error("Connection failed", {
            {"error", e.what()}
        });
        connected_ = false;
        throw;
    }
}

void TLSClient::disconnect() {
    if (!connected_ || !socket_) {
        return;
    }
    
    try {
        Logger::info("Disconnecting from server");
        
        boost::system::error_code ec;
        socket_->shutdown(ec);
        
        if (ec && ec != boost::asio::error::eof) {
            Logger::warning("Shutdown error", {
                {"error", ec.message()}
            });
        }
        
        socket_->lowest_layer().close(ec);
        
        if (ec) {
            Logger::warning("Close error", {
                {"error", ec.message()}
            });
        }
        
        connected_ = false;
        
    } catch (const std::exception& e) {
        Logger::error("Disconnect failed", {
            {"error", e.what()}
        });
    }
}

bool TLSClient::is_connected() const {
    return connected_ && socket_ && socket_->lowest_layer().is_open();
}

boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& TLSClient::socket() {
    if (!socket_) {
        throw std::runtime_error("Socket not initialized");
    }
    return *socket_;
}

boost::asio::io_context& TLSClient::io_context() {
    return *io_context_;
}

}  // namespace agent
}  // namespace protogate
