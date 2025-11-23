#pragma once

#include "../proxy/http_proxy.h"
#include "../security/tls_manager.h"
#include "io_context_pool.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <string>

namespace protogate {
namespace server {

/**
 * @brief HTTPS server for client-facing connections
 * 
 * Responsibilities:
 * - Accept HTTPS connections on port 443
 * - Perform TLS handshake with SNI support
 * - Parse HTTP requests from clients
 * - Delegate to HTTPProxy for tunnel routing
 * - Stream responses back to clients
 * - Handle connection errors and timeouts
 */
class HTTPServer {
public:
    /**
     * @brief Initialize HTTP server
     * @param io_pool IO context pool for async operations
     * @param tls_manager TLS manager for certificate loading
     * @param http_proxy HTTP proxy for request routing
     * @param port Listen port (default 443)
     */
    HTTPServer(std::shared_ptr<IOContextPool> io_pool,
              std::shared_ptr<security::TLSManager> tls_manager,
              std::shared_ptr<proxy::HTTPProxy> http_proxy,
              unsigned short port = 443);

    /**
     * @brief Start accepting connections
     */
    void start();

    /**
     * @brief Stop server gracefully
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool is_running() const { return running_; }

private:
    /**
     * @brief Accept next client connection
     */
    void do_accept();

    /**
     * @brief Handle single client connection
     */
    class Connection : public std::enable_shared_from_this<Connection> {
    public:
        Connection(boost::asio::io_context& io_context,
                  boost::asio::ssl::context& ssl_context,
                  std::shared_ptr<security::TLSManager> tls_manager,
                  std::shared_ptr<proxy::HTTPProxy> http_proxy);

        auto& socket() { return socket_.lowest_layer(); }

        void start();

    private:
        void do_handshake();
        void do_read();
        void do_write(const std::string& response);
        void handle_error(const boost::system::error_code& ec);
        
        /**
         * @brief SNI callback for certificate selection
         * @return SSL_TLSEXT_ERR_OK on success
         */
        static int sni_callback(SSL* ssl, int* al, void* arg);

        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
        std::shared_ptr<security::TLSManager> tls_manager_;
        std::shared_ptr<proxy::HTTPProxy> http_proxy_;
        std::array<char, 8192> buffer_;
        std::string request_buffer_;
        std::string client_ip_;
        std::string sni_hostname_;
    };

    std::shared_ptr<IOContextPool> io_pool_;
    std::shared_ptr<security::TLSManager> tls_manager_;
    std::shared_ptr<proxy::HTTPProxy> http_proxy_;
    unsigned short port_;
    bool running_;
    
    boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace server
}  // namespace protogate
