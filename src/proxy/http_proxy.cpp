#include "http_proxy.h"
#include "../observability/logger.h"
#include "../observability/audit_logger.h"
#include "../security/ip_allowlist.h"
#include <sstream>
#include <algorithm>

namespace protogate {
namespace proxy {

HTTPProxy::HTTPProxy(tunnel_cache_ptr tunnel_cache, agent_registry_ptr agent_registry)
    : tunnel_cache_(tunnel_cache), agent_registry_(agent_registry) {
    
    observability::Logger::instance().info("HTTPProxy initialized");
}

void HTTPProxy::handle_request(const HTTPRequest& request, response_callback callback) {
    // Match hostname to tunnel
    std::string tunnel_id = match_tunnel(request.host);
    
    if (tunnel_id.empty()) {
        observability::Logger::instance().warning("No tunnel found for hostname", {
            {"host", request.host},
            {"client_ip", request.client_ip}
        });
        
        auto response = HTTPResponse::error(404, "Tunnel not found");
        callback(response.to_string(), false);
        return;
    }
    
    // Get tunnel configuration
    auto tunnel = tunnel_cache_->get(tunnel_id);
    if (!tunnel) {
        observability::Logger::instance().error("Tunnel configuration not found", {
            {"tunnel_id", tunnel_id}
        });
        
        auto response = HTTPResponse::error(500, "Tunnel configuration error");
        callback(response.to_string(), false);
        return;
    }
    
    // Validate IP allowlist
    if (!validate_ip_allowlist(*tunnel, request.client_ip)) {
        observability::Logger::instance().warning("IP blocked by allowlist", {
            {"tunnel_id", tunnel_id},
            {"client_ip", request.client_ip}
        });
        
        // Audit log security event
        observability::AuditLogger::instance().log_ip_blocked(
            tunnel_id,
            request.client_ip,
            "HTTP",
            "IP not in tunnel allowlist"
        );
        
        auto response = HTTPResponse::forbidden("IP address not allowed");
        callback(response.to_string(), false);
        log_request(tunnel_id, request, 403, 0);
        return;
    }
    
    // Route to agent
    route_to_agent(tunnel_id, request, callback);
}

std::optional<HTTPProxy::HTTPRequest> HTTPProxy::parse_request(const std::string& buffer) {
    HTTPRequest request;
    std::istringstream stream(buffer);
    std::string line;
    
    // Parse request line
    if (!std::getline(stream, line)) {
        return std::nullopt;
    }
    
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    
    // Parse: METHOD PATH VERSION
    std::istringstream request_line(line);
    request_line >> request.method >> request.path >> request.version;
    
    // Parse headers
    while (std::getline(stream, line) && !line.empty() && line != "\r") {
        if (line.back() == '\r') {
            line.pop_back();
        }
        
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Convert header name to lowercase
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        
        request.headers[name] = value;
        
        // Extract special headers
        if (name == "host") {
            request.host = value;
        }
    }
    
    // Read body (rest of buffer)
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    request.body = body_stream.str();
    
    return request;
}

std::string HTTPProxy::match_tunnel(const std::string& hostname) {
    // Try exact match first using tunnel_id as subdomain
    std::string matched_id;
    
    tunnel_cache_->for_each([&](const std::string& tunnel_id, const models::Tunnel& tunnel) {
        // Check if tunnel_id matches hostname prefix
        // Tunnel ID: "api", hostname: "api.tunnel.example.com"
        std::string prefix = tunnel_id + ".";
        if (hostname.compare(0, prefix.size(), prefix) == 0) {
            matched_id = tunnel_id;
        }
        // Also check direct match (if tunnel_id is full hostname)
        else if (tunnel_id == hostname) {
            matched_id = tunnel_id;
        }
    });
    
    return matched_id;
}

bool HTTPProxy::validate_ip_allowlist(const models::Tunnel& tunnel, const std::string& client_ip) {
    // If no allowlist configured, allow all
    if (tunnel.ip_allowlist.empty()) {
        return true;
    }
    
    // Create IPAllowlist from tunnel's CIDR list
    auto allowlist_opt = security::IPAllowlist::from_cidr_list(tunnel.ip_allowlist);
    if (!allowlist_opt) {
        // Failed to parse allowlist - log error and deny access
        observability::Logger::instance().error("Failed to parse IP allowlist", {
            {"tunnel_id", tunnel.tunnel_id}
        });
        return false;
    }
    
    // Check if client IP is allowed
    return allowlist_opt->is_allowed(client_ip);
}

void HTTPProxy::route_to_agent(
    const std::string& tunnel_id,
    const HTTPRequest& request,
    response_callback callback) {
    
    // Check if agent is connected
    if (!agent_registry_->is_connected(tunnel_id)) {
        observability::Logger::instance().warning("Agent not connected", {
            {"tunnel_id", tunnel_id}
        });
        
        auto response = HTTPResponse::error(503, "Agent not connected");
        callback(response.to_string(), false);
        log_request(tunnel_id, request, 503, 0);
        return;
    }
    
    // Get agent connection
    auto agent = agent_registry_->get_agent(tunnel_id);
    if (!agent) {
        auto response = HTTPResponse::error(500, "Agent connection error");
        callback(response.to_string(), false);
        return;
    }
    
    // Generate request ID
    std::string request_id = "req_" + std::to_string(std::time(nullptr));
    
    // Forward request to agent
    agent->send_http_request(request_id, request.to_string(),
        [this, tunnel_id, request, callback](const std::string& response, bool error) {
            if (error) {
                observability::Logger::instance().error("Agent request failed", {
                    {"tunnel_id", tunnel_id}
                });
                
                auto error_response = HTTPResponse::error(502, "Bad Gateway");
                callback(error_response.to_string(), false);
                log_request(tunnel_id, request, 502, 0);
            } else {
                // Forward response to client
                callback(response, false);
                log_request(tunnel_id, request, 200, response.size());
            }
        });
    
    observability::Logger::instance().info("Request routed to agent", {
        {"tunnel_id", tunnel_id},
        {"request_id", request_id},
        {"method", request.method},
        {"path", request.path}
    });
}

void HTTPProxy::log_request(
    const std::string& tunnel_id,
    const HTTPRequest& request,
    int status_code,
    size_t response_bytes) {
    
    observability::Logger::instance().info("HTTP request completed", {
        {"tunnel_id", tunnel_id},
        {"method", request.method},
        {"path", request.path},
        {"client_ip", request.client_ip},
        {"status", std::to_string(status_code)},
        {"response_bytes", std::to_string(response_bytes)}
    });
}

// HTTPRequest methods

std::string HTTPProxy::HTTPRequest::to_string() const {
    std::ostringstream oss;
    
    // Request line
    oss << method << " " << path << " " << version << "\r\n";
    
    // Headers
    for (const auto& [name, value] : headers) {
        oss << name << ": " << value << "\r\n";
    }
    
    oss << "\r\n";
    
    // Body
    oss << body;
    
    return oss.str();
}

// HTTPResponse methods

std::string HTTPProxy::HTTPResponse::to_string() const {
    std::ostringstream oss;
    
    // Status line
    oss << "HTTP/1.1 " << status_code << " " << status_message << "\r\n";
    
    // Headers
    for (const auto& [name, value] : headers) {
        oss << name << ": " << value << "\r\n";
    }
    
    // Content-Length (if not present)
    if (headers.find("content-length") == headers.end() && !body.empty()) {
        oss << "Content-Length: " << body.size() << "\r\n";
    }
    
    oss << "\r\n";
    
    // Body
    oss << body;
    
    return oss.str();
}

HTTPProxy::HTTPResponse HTTPProxy::HTTPResponse::error(int code, const std::string& message) {
    HTTPResponse response;
    response.status_code = code;
    response.status_message = message;
    response.headers["Content-Type"] = "text/plain";
    response.body = message + "\n";
    return response;
}

HTTPProxy::HTTPResponse HTTPProxy::HTTPResponse::forbidden(const std::string& reason) {
    HTTPResponse response;
    response.status_code = 403;
    response.status_message = "Forbidden";
    response.headers["Content-Type"] = "application/json";
    
    // JSON error body with structured information
    nlohmann::json error_body = {
        {"error", {
            {"code", 403},
            {"message", "Forbidden"},
            {"reason", reason}
        }}
    };
    
    response.body = error_body.dump(2) + "\n";
    return response;
}

}  // namespace proxy
}  // namespace protogate
