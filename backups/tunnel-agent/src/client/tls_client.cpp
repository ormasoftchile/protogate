#include "tls_client.h"
#include "../utils/logger.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <openssl/ssl.h>

namespace protogate {
namespace agent {

TLSClient::TLSClient(boost::asio::io_context& io_context, const std::string& host, unsigned short port, bool verify_tls)
    : io_context_(io_context), host_(host), port_(port), verify_tls_(verify_tls), connected_(false) {
    
    ssl_context_ = std::make_unique<boost::asio::ssl::context>(
        boost::asio::ssl::context::tls_client);
    
    // Set TLS options to match server requirements
    ssl_context_->set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::no_tlsv1 |
        boost::asio::ssl::context::no_tlsv1_1
    );
    
    // Set minimum TLS version (TLS 1.2)
    SSL_CTX_set_min_proto_version(ssl_context_->native_handle(), TLS1_2_VERSION);
    
    // Set cipher suites - use HIGH for maximum compatibility
    SSL_CTX_set_cipher_list(ssl_context_->native_handle(), "HIGH:!aNULL:!eNULL:!EXPORT:!DES:!MD5:!PSK:!RC4");
    
    // For local testing, disable certificate verification
    ssl_context_->set_verify_mode(boost::asio::ssl::verify_none);
    
    // Set ALPN protocols - prefer HTTP/2, fallback to HTTP/1.1
    // Note: Server needs ALPN support too for proper HTTP/2 negotiation
    const unsigned char alpn_protos[] = {
        2, 'h', '2',           // HTTP/2
        8, 'h', 't', 't', 'p', '/', '1', '.', '1'  // HTTP/1.1 fallback
    };
    SSL_CTX_set_alpn_protos(ssl_context_->native_handle(), alpn_protos, sizeof(alpn_protos));
    
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
        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        
        // Create SSL socket
        socket_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            io_context_, *ssl_context_);
        
        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(socket_->native_handle(), host_.c_str())) {
            throw std::runtime_error("Failed to set SNI hostname");
        }
        
        // Connect TCP socket
        boost::asio::connect(socket_->lowest_layer(), endpoints);
        
        Logger::info("TCP connected, starting TLS handshake");
        
        // TLS handshake (synchronous for simplicity during connection phase)
        socket_->handshake(boost::asio::ssl::stream_base::client);
        
        Logger::info("TLS handshake complete");
        
        // Set socket to non-blocking mode for async operations
        socket_->lowest_layer().non_blocking(true);
        
        // Check if data is already available (HTTP/2 preface from server)
        auto available = socket_->lowest_layer().available();
        Logger::info("Bytes available after handshake", {{"count", std::to_string(available)}});
        
        connected_ = true;
        
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
    return io_context_;
}

std::string TLSClient::get_alpn_protocol() const {
    if (!socket_) {
        return "";
    }
    
    const unsigned char* alpn_data = nullptr;
    unsigned int alpn_len = 0;
    
    SSL_get0_alpn_selected(socket_->native_handle(), &alpn_data, &alpn_len);
    
    if (alpn_data && alpn_len > 0) {
        return std::string(reinterpret_cast<const char*>(alpn_data), alpn_len);
    }
    
    return "";
}

}  // namespace agent
}  // namespace protogate
