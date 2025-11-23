// src/api/router.h
// HTTP request router for management API endpoints

#pragma once

#include <boost/asio.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include <regex>

namespace protogate {
namespace api {

// Forward declarations
struct HttpRequest;
struct HttpResponse;

// HTTP request structure
struct HttpRequest {
    std::string method;           // GET, POST, PUT, DELETE
    std::string path;             // /api/v1/tunnels
    std::string query_string;     // ?page=1&limit=10
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> path_params; // Captured route parameters
    std::string body;
    std::string remote_ip;
};

// HTTP response structure
struct HttpResponse {
    int status_code{200};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    
    // Helper methods
    void set_json(const std::string& json_body);
    void set_error(int code, const std::string& message);
};

// Route handler callback
using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

// HTTP method router
class Router {
public:
    Router();
    ~Router() = default;

    // Register route handlers
    void get(const std::string& pattern, RouteHandler handler);
    void post(const std::string& pattern, RouteHandler handler);
    void put(const std::string& pattern, RouteHandler handler);
    void delete_route(const std::string& pattern, RouteHandler handler);
    
    // Route request to handler
    bool route(const HttpRequest& request, HttpResponse& response) const;
    
    // Parse raw HTTP request into HttpRequest structure
    static HttpRequest parse_request(const std::string& raw_request, const std::string& remote_ip);
    
    // Serialize HttpResponse to raw HTTP response
    static std::string serialize_response(const HttpResponse& response);

private:
    struct Route {
        std::string method;
        std::regex pattern;
        std::vector<std::string> param_names;
        RouteHandler handler;
    };
    
    std::vector<Route> routes_;
    
    // Convert route pattern to regex (e.g., /tunnels/{id} -> /tunnels/([^/]+))
    std::pair<std::regex, std::vector<std::string>> compile_pattern(const std::string& pattern);
    
    // Extract path parameters from matched route
    std::unordered_map<std::string, std::string> extract_params(
        const std::smatch& match,
        const std::vector<std::string>& param_names) const;
};

}  // namespace api
}  // namespace protogate
