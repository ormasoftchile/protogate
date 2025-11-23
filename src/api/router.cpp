// src/api/router.cpp
// HTTP request router implementation

#include "router.h"
#include "../observability/logger.h"
#include <sstream>
#include <algorithm>

namespace protogate {
namespace api {

void HttpResponse::set_json(const std::string& json_body) {
    body = json_body;
    headers["Content-Type"] = "application/json";
    headers["Content-Length"] = std::to_string(json_body.size());
}

void HttpResponse::set_error(int code, const std::string& message) {
    status_code = code;
    std::ostringstream json;
    json << "{\"error\":\"" << message << "\"}";
    set_json(json.str());
}

Router::Router() = default;

void Router::get(const std::string& pattern, RouteHandler handler) {
    auto [regex, params] = compile_pattern(pattern);
    routes_.push_back({"GET", std::move(regex), std::move(params), std::move(handler)});
}

void Router::post(const std::string& pattern, RouteHandler handler) {
    auto [regex, params] = compile_pattern(pattern);
    routes_.push_back({"POST", std::move(regex), std::move(params), std::move(handler)});
}

void Router::put(const std::string& pattern, RouteHandler handler) {
    auto [regex, params] = compile_pattern(pattern);
    routes_.push_back({"PUT", std::move(regex), std::move(params), std::move(handler)});
}

void Router::delete_route(const std::string& pattern, RouteHandler handler) {
    auto [regex, params] = compile_pattern(pattern);
    routes_.push_back({"DELETE", std::move(regex), std::move(params), std::move(handler)});
}

bool Router::route(const HttpRequest& request, HttpResponse& response) const {
    for (const auto& route : routes_) {
        if (route.method != request.method) {
            continue;
        }
        
        std::smatch match;
        if (std::regex_match(request.path, match, route.pattern)) {
            // Extract path parameters
            HttpRequest mutable_request = request;
            mutable_request.path_params = extract_params(match, route.param_names);
            
            // Invoke handler
            try {
                route.handler(mutable_request, response);
                return true;
            } catch (const std::exception& e) {
                observability::Logger::instance().error("Route handler exception", {
                    {"method", request.method},
                    {"path", request.path},
                    {"error", e.what()}
                });
                response.set_error(500, "Internal server error");
                return true;
            }
        }
    }
    
    // No route matched
    response.set_error(404, "Not found");
    return false;
}

std::pair<std::regex, std::vector<std::string>> Router::compile_pattern(const std::string& pattern) {
    std::vector<std::string> param_names;
    std::string regex_pattern;
    
    size_t pos = 0;
    while (pos < pattern.size()) {
        size_t start = pattern.find('{', pos);
        if (start == std::string::npos) {
            // No more parameters
            regex_pattern += pattern.substr(pos);
            break;
        }
        
        // Add literal text before parameter
        regex_pattern += pattern.substr(pos, start - pos);
        
        // Extract parameter name
        size_t end = pattern.find('}', start);
        if (end == std::string::npos) {
            throw std::runtime_error("Unclosed parameter in route pattern: " + pattern);
        }
        
        std::string param_name = pattern.substr(start + 1, end - start - 1);
        param_names.push_back(param_name);
        
        // Add capture group for parameter (match anything except /)
        regex_pattern += "([^/]+)";
        
        pos = end + 1;
    }
    
    return {std::regex(regex_pattern), param_names};
}

std::unordered_map<std::string, std::string> Router::extract_params(
    const std::smatch& match,
    const std::vector<std::string>& param_names) const {
    
    std::unordered_map<std::string, std::string> params;
    
    // match[0] is full match, match[1]... are capture groups
    for (size_t i = 0; i < param_names.size() && i + 1 < match.size(); ++i) {
        params[param_names[i]] = match[i + 1].str();
    }
    
    return params;
}

HttpRequest Router::parse_request(const std::string& raw_request, const std::string& remote_ip) {
    HttpRequest request;
    request.remote_ip = remote_ip;
    
    std::istringstream stream(raw_request);
    std::string line;
    
    // Parse request line: GET /path HTTP/1.1
    if (std::getline(stream, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        std::istringstream request_line(line);
        std::string full_path;
        request_line >> request.method >> full_path;
        
        // Split path and query string
        size_t query_pos = full_path.find('?');
        if (query_pos != std::string::npos) {
            request.path = full_path.substr(0, query_pos);
            request.query_string = full_path.substr(query_pos + 1);
        } else {
            request.path = full_path;
        }
    }
    
    // Parse headers
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            // Empty line marks end of headers
            break;
        }
        
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            
            // Trim leading whitespace from value
            size_t value_start = value.find_first_not_of(" \t");
            if (value_start != std::string::npos) {
                value = value.substr(value_start);
            }
            
            request.headers[name] = value;
        }
    }
    
    // Parse body (remaining content)
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    request.body = body_stream.str();
    
    return request;
}

std::string Router::serialize_response(const HttpResponse& response) {
    std::ostringstream output;
    
    // Status line
    std::string status_text;
    switch (response.status_code) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 204: status_text = "No Content"; break;
        case 400: status_text = "Bad Request"; break;
        case 401: status_text = "Unauthorized"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 409: status_text = "Conflict"; break;
        case 429: status_text = "Too Many Requests"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 503: status_text = "Service Unavailable"; break;
        default: status_text = "Unknown"; break;
    }
    
    output << "HTTP/1.1 " << response.status_code << " " << status_text << "\r\n";
    
    // Headers
    for (const auto& [name, value] : response.headers) {
        output << name << ": " << value << "\r\n";
    }
    
    // Blank line separating headers and body
    output << "\r\n";
    
    // Body
    output << response.body;
    
    return output.str();
}

}  // namespace api
}  // namespace protogate
