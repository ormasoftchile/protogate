#pragma once

#include "../agent/agent_registry.h"
#include "../security/tls_manager.h"
#include "../security/token_validator.h"
#include "io_context_pool.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <string>

namespace protogate {
namespace server {

/**
 * @brief TLS server for agent connections
 * 
 * Responsibilities:
 * - Accept agent TLS connections on port 8443
 * - Perform TLS handshake
 * - Authenticate agent via bearer token
 * - Create AgentConnection instance
 * - Register agent in AgentRegistry
 * - Enforce connection limits (max 50 agents)
 */
class AgentServer {
public:
    /**
     * @brief Initialize agent server
     * @param io_pool IO context pool for async operations
     * @param tls_manager TLS manager for certificate loading
     * @param token_validator Token validator for authentication
     * @param agent_registry Agent registry for connection management
     * @param port Listen port (default 8443)
     */
    AgentServer(std::shared_ptr<IOContextPool> io_pool,
               std::shared_ptr<security::TLSManager> tls_manager,
               std::shared_ptr<security::TokenValidator> token_validator,
               std::shared_ptr<agent::AgentRegistry> agent_registry,
               unsigned short port = 8443);

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
     * @brief Accept next agent connection
     */
    void do_accept();

    /**
     * @brief Handle agent authentication handshake
     */
    class AgentHandshake : public std::enable_shared_from_this<AgentHandshake> {
    public:
        AgentHandshake(boost::asio::io_context& io_context,
                      boost::asio::ssl::context& ssl_context,
                      std::shared_ptr<security::TokenValidator> token_validator,
                      std::shared_ptr<agent::AgentRegistry> agent_registry);

        boost::asio::ip::tcp::socket& socket() { return socket_.lowest_layer(); }

        void start();

    private:
        void do_handshake();
        void do_read_auth();
        void authenticate(const std::string& auth_data);
        void send_response(int status_code, const std::string& message);
        void handle_error(const boost::system::error_code& ec);

        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
        std::shared_ptr<security::TokenValidator> token_validator_;
        std::shared_ptr<agent::AgentRegistry> agent_registry_;
        std::array<char, 4096> buffer_;
        std::string auth_buffer_;
        std::string agent_ip_;
    };

    std::shared_ptr<IOContextPool> io_pool_;
    std::shared_ptr<security::TLSManager> tls_manager_;
    std::shared_ptr<security::TokenValidator> token_validator_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    unsigned short port_;
    bool running_;
    
    boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace server
}  // namespace protogate
