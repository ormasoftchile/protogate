#pragma once

#include <string>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace protogate {
namespace agent {

class TLSClient {
public:
    TLSClient(const std::string& host, unsigned short port, bool verify_tls);
    ~TLSClient();
    
    void connect();
    void disconnect();
    bool is_connected() const;
    
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket();
    boost::asio::io_context& io_context();
    
private:
    std::string host_;
    unsigned short port_;
    bool verify_tls_;
    bool connected_;
    
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ssl::context> ssl_context_;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket_;
};

}  // namespace agent
}  // namespace protogate
