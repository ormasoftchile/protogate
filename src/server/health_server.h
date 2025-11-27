#pragma once

#include "io_context_pool.h"
#include "health_handler.h"
#include <boost/asio.hpp>
#include <memory>

namespace protogate {
namespace server {

/**
 * Simple HTTP server for health checks (no TLS).
 * Listens on port 8080 and responds to /health requests.
 * 
 * This is used by container orchestrators (Kubernetes, Azure Container Apps)
 * for liveness and readiness probes without TLS overhead.
 */
class HealthServer {
public:
    HealthServer(std::shared_ptr<IOContextPool> io_pool, unsigned short port = 8080);
    
    void start();
    void stop();
    
private:
    void do_accept();
    void handle_connection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    
    std::shared_ptr<IOContextPool> io_pool_;
    unsigned short port_;
    bool running_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace server
}  // namespace protogate
