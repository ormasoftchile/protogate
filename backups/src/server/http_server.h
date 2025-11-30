#pragma once

#include "../proxy/http_proxy.h"
#include "../security/tls_manager.h"
#include "../api/router.h"
#include "../api/tunnels_handler.h"
#include "io_context_pool.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <string>

namespace protogate {
namespace server {

/**
 * @brief HTTP/HTTPS server for client-facing connections
 * 
 * Responsibilities:
 * - Accept HTTP or HTTPS connections on specified port
 * - Perform TLS handshake with SNI support (if TLS enabled)
 * - Parse HTTP requests from clients
 * - Delegate to HTTPProxy for tunnel routing
 * - Stream responses back to clients
 * - Handle connection errors and timeouts
 * 
 * Note: When deployed on Azure Container Apps, ingress terminates TLS
 * and forwards plain HTTP to the container, so use_tls should be false.
 */
class HTTPServer {
public:
    /**
     * @brief Initialize HTTP server
     * @param io_pool IO context pool for async operations
     * @param tls_manager TLS manager for certificate loading (can be null if use_tls=false)
     * @param http_proxy HTTP proxy for request routing
     * @param router API router for Management API endpoints (can be null)
     * @param port Listen port (default 443)
     * @param use_tls Enable TLS (false for plain HTTP, true for HTTPS)
     */
    HTTPServer(std::shared_ptr<IOContextPool> io_pool,
              std::shared_ptr<security::TLSManager> tls_manager,
              std::shared_ptr<proxy::HTTPProxy> http_proxy,
              std::shared_ptr<api::Router> router,
              std::shared_ptr<api::TunnelsHandler> tunnels_handler,
              unsigned short port = 443,
              bool use_tls = false);

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
     * @brief Handle plain HTTP connection (no TLS)
     */
    void handle_plain_http(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

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
    std::shared_ptr<api::Router> router_;
    std::shared_ptr<api::TunnelsHandler> tunnels_handler_;
    unsigned short port_;
    bool running_;
    bool use_tls_;
    
    boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace server
}  // namespace protogate
