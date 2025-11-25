#pragma once

#include <string>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace protogate {
namespace agent {

class TLSClient {
public:
    TLSClient(boost::asio::io_context& io_context, const std::string& host, unsigned short port, bool verify_tls);
    ~TLSClient();
    
    void connect();
    void disconnect();
    bool is_connected() const;
    
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket();
    boost::asio::io_context& io_context();
    
    // Get negotiated ALPN protocol (empty if none)
    std::string get_alpn_protocol() const;
    
private:
    std::string host_;
    unsigned short port_;
    bool verify_tls_;
    bool connected_;
    
    boost::asio::io_context& io_context_;
    std::unique_ptr<boost::asio::ssl::context> ssl_context_;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket_;
};

}  // namespace agent
}  // namespace protogate
