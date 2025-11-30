#include "request_forwarder.h"
#include "../utils/logger.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <stdexcept>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace protogate {
namespace agent {

RequestForwarder::RequestForwarder(const std::string& local_url, int timeout_ms)
    : timeout_ms_(timeout_ms) {
    parse_local_url(local_url);
}

RequestForwarder::~RequestForwarder() {
}

void RequestForwarder::parse_local_url(const std::string& url) {
    // Parse URL: http://host:port/path
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        throw std::invalid_argument("Invalid local URL: missing scheme");
    }
    
    size_t host_start = scheme_end + 3;
    size_t port_start = url.find(':', host_start);
    size_t path_start = url.find('/', host_start);
    
    if (port_start != std::string::npos && (path_start == std::string::npos || port_start < path_start)) {
        local_host_ = url.substr(host_start, port_start - host_start);
        
        size_t port_end = (path_start != std::string::npos) ? path_start : url.length();
        local_port_ = url.substr(port_start + 1, port_end - port_start - 1);
    } else {
        size_t host_end = (path_start != std::string::npos) ? path_start : url.length();
        local_host_ = url.substr(host_start, host_end - host_start);
        local_port_ = "80";
    }
    
    if (path_start != std::string::npos) {
        local_path_prefix_ = url.substr(path_start);
    } else {
        local_path_prefix_ = "/";
    }
    
    Logger::debug("Parsed local URL", {
        {"host", local_host_},
        {"port", local_port_},
        {"path_prefix", local_path_prefix_}
    });
}

ForwardResponse RequestForwarder::forward(const HTTP2Request& request) {
    try {
        Logger::info("Forwarding request", {
            {"method", request.method},
            {"path", request.path},
            {"request_id", request.request_id}
        });
        
        // Create IO context
        net::io_context ioc;
        
        // Resolve target
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(local_host_, local_port_);
        
        // Connect to local service
        beast::tcp_stream stream(ioc);
        stream.connect(results);
        
        // Set timeout
        stream.expires_after(std::chrono::milliseconds(timeout_ms_));
        
        // Build HTTP request
        http::verb method = http::string_to_verb(request.method);
        std::string target = request.path;
        
        http::request<http::string_body> req{method, target, 11};
        req.set(http::field::host, local_host_ + ":" + local_port_);
        req.set(http::field::user_agent, "Protogate-Agent/1.0");
        
        // Copy headers (skip pseudo-headers)
        for (const auto& [key, value] : request.headers) {
            if (!key.empty() && key[0] != ':') {
                req.set(key, value);
            }
        }
        
        // Set body if present
        if (!request.body.empty()) {
            req.body() = std::string(request.body.begin(), request.body.end());
            req.prepare_payload();
        }
        
        // Send request
        http::write(stream, req);
        
        // Receive response
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        // Graceful close
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        
        // Build response
        ForwardResponse response;
        response.status_code = res.result_int();
        
        for (const auto& field : res) {
            response.headers[std::string(field.name_string())] = std::string(field.value());
        }
        
        const auto& body_str = res.body();
        response.body.assign(body_str.begin(), body_str.end());
        
        Logger::info("Request forwarded successfully", {
            {"request_id", request.request_id},
            {"status", std::to_string(response.status_code)},
            {"body_size", std::to_string(response.body.size())}
        });
        
        return response;
        
    } catch (const std::exception& e) {
        Logger::error("Forward failed", {
            {"request_id", request.request_id},
            {"error", e.what()}
        });
        
        return make_error_response(502, "Bad Gateway: " + std::string(e.what()));
    }
}

ForwardResponse RequestForwarder::make_error_response(int status_code, const std::string& message) {
    ForwardResponse response;
    response.status_code = status_code;
    response.headers["content-type"] = "text/plain";
    response.body.assign(message.begin(), message.end());
    return response;
}

}  // namespace agent
}  // namespace protogate
