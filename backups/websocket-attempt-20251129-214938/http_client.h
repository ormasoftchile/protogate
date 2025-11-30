#pragma once

#include <string>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace protogate {
namespace agent {

/**
 * @brief HTTP/HTTPS client for agent connections to Azure Container Apps
 * 
 * Connects to server via plain HTTPS (TLS handled by Azure ingress).
 * Sends POST /v1/agent/connect with Authorization header.
 * Receives HTTP 101 Switching Protocols response.
 * Keeps socket open for bidirectional communication.
 */
class HttpClient {
public:
    HttpClient(boost::asio::io_context& io_context, const std::string& host, unsigned short port, bool use_tls);
    ~HttpClient();
    
    /**
     * @brief Connect to server and perform HTTP upgrade
     * @param tunnel_id Tunnel identifier
     * @param token Agent authentication token (with tnl_ prefix)
     */
    void connect(const std::string& tunnel_id, const std::string& token);
    
    void disconnect();
    bool is_connected() const;
    
    // Get underlying socket for HTTP/2 communication
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& ssl_socket();
    boost::asio::ip::tcp::socket& plain_socket();
    boost::asio::io_context& io_context();
    
    bool is_using_tls() const { return use_tls_; }
    
private:
    void perform_http_upgrade(const std::string& tunnel_id, const std::string& token);
    
    std::string host_;
    unsigned short port_;
    bool use_tls_;
    bool connected_;
    
    boost::asio::io_context& io_context_;
    std::unique_ptr<boost::asio::ssl::context> ssl_context_;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ssl_socket_;
    std::unique_ptr<boost::asio::ip::tcp::socket> plain_socket_;
};

}  // namespace agent
}  // namespace protogate
