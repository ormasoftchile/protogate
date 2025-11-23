#pragma once

#include "../agent/agent_registry.h"
#include "../storage/cache.h"
#include "../models/tunnel.h"
#include "../models/tunnel_request.h"
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>

namespace protogate {
namespace proxy {

/**
 * @brief HTTP/HTTPS request proxy and router
 * 
 * Responsibilities:
 * - Parse incoming HTTP requests (headers + body)
 * - Extract tunnel routing information from Host header
 * - Look up tunnel configuration and active agent connection
 * - Forward request to agent via HTTP/2 or custom protocol
 * - Stream response back to client
 * - Handle request timeouts (default 30 minutes)
 * - Log request metadata for observability
 */
class HTTPProxy {
public:
    using tunnel_cache_ptr = std::shared_ptr<storage::Cache<std::string, models::Tunnel>>;
    using agent_registry_ptr = std::shared_ptr<agent::AgentRegistry>;
    using response_callback = std::function<void(const std::string& response, bool error)>;

    /**
     * @brief HTTP request representation
     */
    struct HTTPRequest {
        std::string method;          // GET, POST, PUT, DELETE, etc.
        std::string path;            // /api/users
        std::string version;         // HTTP/1.1
        std::string host;            // api.tunnel.example.com
        std::string client_ip;       // Source IP address
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        
        std::string to_string() const;
    };

    /**
     * @brief HTTP response representation
     */
    struct HTTPResponse {
        int status_code;             // 200, 404, 500, etc.
        std::string status_message;  // OK, Not Found, etc.
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        
        std::string to_string() const;
        
        static HTTPResponse error(int code, const std::string& message);
    };

    /**
     * @brief Initialize HTTP proxy
     * @param tunnel_cache Tunnel configuration cache
     * @param agent_registry Active agent connections
     */
    HTTPProxy(tunnel_cache_ptr tunnel_cache, agent_registry_ptr agent_registry);

    /**
     * @brief Handle incoming HTTP request
     * @param request Parsed HTTP request
     * @param callback Callback with HTTP response or error
     */
    void handle_request(const HTTPRequest& request, response_callback callback);

    /**
     * @brief Parse raw HTTP request from buffer
     * @param buffer Raw HTTP data
     * @return Parsed HTTPRequest or nullopt if invalid
     */
    static std::optional<HTTPRequest> parse_request(const std::string& buffer);

    /**
     * @brief Match hostname to tunnel ID
     * @param hostname Host header value (e.g., api.tunnel.example.com)
     * @return Tunnel ID or empty if not found
     */
    std::string match_tunnel(const std::string& hostname);

    /**
     * @brief Validate IP allowlist for tunnel
     * @param tunnel Tunnel configuration
     * @param client_ip Client source IP
     * @return true if IP is allowed
     */
    bool validate_ip_allowlist(const models::Tunnel& tunnel, const std::string& client_ip);

private:
    /**
     * @brief Route request to agent
     */
    void route_to_agent(const std::string& tunnel_id,
                       const HTTPRequest& request,
                       response_callback callback);

    /**
     * @brief Create audit event for request
     */
    void log_request(const std::string& tunnel_id,
                    const HTTPRequest& request,
                    int status_code,
                    size_t response_bytes);

    tunnel_cache_ptr tunnel_cache_;
    agent_registry_ptr agent_registry_;
};

}  // namespace proxy
}  // namespace protogate
