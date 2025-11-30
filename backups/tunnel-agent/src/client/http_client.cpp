#include "http_client.h"
#include "../utils/logger.h"
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <sstream>

namespace protogate {
namespace agent {

HttpClient::HttpClient(boost::asio::io_context& io_context, const std::string& host, unsigned short port, bool use_tls)
    : io_context_(io_context)
    , host_(host)
    , port_(port)
    , use_tls_(use_tls)
    , connected_(false) {
    
    if (use_tls_) {
        ssl_context_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv13_client);
        ssl_context_->set_default_verify_paths();
        ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer);
        
        ssl_socket_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            io_context_, *ssl_context_);
        
        // Set SNI hostname
        SSL_set_tlsext_host_name(ssl_socket_->native_handle(), host_.c_str());
        
        // Force HTTP/1.1 via ALPN (HTTP/2 doesn't support 101 Switching Protocols)
        // ALPN protocol list format: length-prefixed strings
        const unsigned char alpn_protos[] = {
            8, 'h', 't', 't', 'p', '/', '1', '.', '1'  // "\x08http/1.1"
        };
        SSL_set_alpn_protos(ssl_socket_->native_handle(), alpn_protos, sizeof(alpn_protos));
    } else {
        plain_socket_ = std::make_unique<boost::asio::ip::tcp::socket>(io_context_);
    }
}

HttpClient::~HttpClient() {
    disconnect();
}

void HttpClient::connect(const std::string& tunnel_id, const std::string& token) {
    try {
        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        
        if (use_tls_) {
            // Connect TLS socket
            boost::asio::connect(ssl_socket_->lowest_layer(), endpoints);
            
            Logger::info("TCP connection established, performing TLS handshake", {
                {"host", host_},
                {"port", std::to_string(port_)}
            });
            
            ssl_socket_->handshake(boost::asio::ssl::stream_base::client);
            
            // Check what ALPN protocol was negotiated
            const unsigned char* alpn_data = nullptr;
            unsigned int alpn_len = 0;
            SSL_get0_alpn_selected(ssl_socket_->native_handle(), &alpn_data, &alpn_len);
            std::string alpn_protocol;
            if (alpn_data && alpn_len > 0) {
                alpn_protocol = std::string(reinterpret_cast<const char*>(alpn_data), alpn_len);
            }
            
            Logger::info("TLS handshake complete", {
                {"alpn_negotiated", alpn_protocol.empty() ? "none" : alpn_protocol}
            });
        } else {
            // Connect plain socket
            boost::asio::connect(*plain_socket_, endpoints);
            
            Logger::info("TCP connection established", {
                {"host", host_},
                {"port", std::to_string(port_)}
            });
        }
        
        // Perform HTTP upgrade
        perform_http_upgrade(tunnel_id, token);
        
        connected_ = true;
        
        Logger::info("HTTP upgrade successful, agent connection established");
        
    } catch (const std::exception& e) {
        Logger::error("Connection failed", {{"error", e.what()}});
        throw;
    }
}

void HttpClient::perform_http_upgrade(const std::string& tunnel_id, const std::string& token) {
    // Generate WebSocket key (base64 encoded 16 random bytes)
    std::string ws_key = "dGhlIHNhbXBsZSBub25jZQ=="; // Fixed key for simplicity (real impl should generate random)
    
    // Build HTTP POST request with WebSocket upgrade headers
    std::ostringstream request;
    request << "POST /v1/agent/connect HTTP/1.1\r\n";
    request << "Host: " << host_ << "\r\n";
    request << "Authorization: Bearer " << token << "\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    request << "Sec-WebSocket-Key: " << ws_key << "\r\n";
    request << "\r\n";
    
    std::string request_str = request.str();
    
    // Log the exact request for debugging
    Logger::info("Sending HTTP upgrade request", {
        {"tunnel_id", tunnel_id},
        {"request_size", std::to_string(request_str.size())},
        {"request", request_str}
    });
    
    // Send request
    if (use_tls_) {
        boost::asio::write(*ssl_socket_, boost::asio::buffer(request_str));
    } else {
        boost::asio::write(*plain_socket_, boost::asio::buffer(request_str));
    }
    
    // Read response until \r\n\r\n
    boost::asio::streambuf response_buffer;
    
    if (use_tls_) {
        boost::asio::read_until(*ssl_socket_, response_buffer, "\r\n\r\n");
    } else {
        boost::asio::read_until(*plain_socket_, response_buffer, "\r\n\r\n");
    }
    
    // Parse response
    std::istream response_stream(&response_buffer);
    std::string http_version;
    unsigned int status_code;
    std::string status_message;
    
    response_stream >> http_version >> status_code;
    std::getline(response_stream, status_message);
    
    // Read all headers for debugging
    std::string all_headers;
    std::string line;
    while (std::getline(response_stream, line) && line != "\r" && !line.empty()) {
        all_headers += line + "\n";
    }
    
    Logger::info("Received HTTP response", {
        {"http_version", http_version},
        {"status_code", std::to_string(status_code)},
        {"status_message", status_message},
        {"headers", all_headers}
    });
    
    if (status_code != 101) {
        // Read error body
        std::string error_body;
        while (std::getline(response_stream, line)) {
            if (!line.empty() && line != "\r") {
                error_body += line + "\n";
            }
        }
        
        Logger::error("HTTP upgrade failed", {
            {"status_code", std::to_string(status_code)},
            {"status_message", status_message},
            {"headers", all_headers},
            {"body", error_body}
        });
        
        throw std::runtime_error("HTTP upgrade failed: " + std::to_string(status_code) + 
                               " - " + error_body);
    }
    
    // Verify Upgrade header in response (check the already-read headers string)
    bool upgrade_confirmed = (all_headers.find("Upgrade:") != std::string::npos || 
                             all_headers.find("upgrade:") != std::string::npos);
    
    if (!upgrade_confirmed) {
        Logger::warning("HTTP 101 received but Upgrade header not found");
    }
}

void HttpClient::disconnect() {
    if (!connected_) {
        return;
    }
    
    try {
        if (use_tls_ && ssl_socket_) {
            ssl_socket_->lowest_layer().close();
        } else if (plain_socket_) {
            plain_socket_->close();
        }
    } catch (const std::exception& e) {
        Logger::error("Error during disconnect", {{"error", e.what()}});
    }
    
    connected_ = false;
}

bool HttpClient::is_connected() const {
    return connected_;
}

boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& HttpClient::ssl_socket() {
    if (!use_tls_ || !ssl_socket_) {
        throw std::runtime_error("SSL socket not available");
    }
    return *ssl_socket_;
}

boost::asio::ip::tcp::socket& HttpClient::plain_socket() {
    if (use_tls_ || !plain_socket_) {
        throw std::runtime_error("Plain socket not available");
    }
    return *plain_socket_;
}

boost::asio::io_context& HttpClient::io_context() {
    return io_context_;
}

}  // namespace agent
}  // namespace protogate
