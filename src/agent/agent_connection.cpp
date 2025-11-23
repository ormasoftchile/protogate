#include "agent_connection.h"
#include "../observability/logger.h"

namespace protogate {
namespace agent {

AgentConnection::AgentConnection(
    boost::asio::io_context& io_context,
    boost::asio::ssl::context& ssl_context,
    const std::string& tunnel_id)
    : io_context_(io_context),
      socket_(io_context, ssl_context),
      tunnel_id_(tunnel_id),
      state_(State::CONNECTING),
      heartbeat_timer_(io_context),
      bytes_sent_(0),
      bytes_received_(0),
      connected_at_(std::chrono::system_clock::now()) {
    
    observability::Logger::instance().debug("AgentConnection created", {
        {"tunnel_id", tunnel_id_}
    });
}

void AgentConnection::start(disconnect_callback on_disconnect) {
    on_disconnect_ = std::move(on_disconnect);
    do_handshake();
}

void AgentConnection::do_handshake() {
    auto self = shared_from_this();
    
    socket_.async_handshake(boost::asio::ssl::stream_base::server,
        [this, self](const boost::system::error_code& ec) {
            if (ec) {
                observability::Logger::instance().error("TLS handshake failed", {
                    {"tunnel_id", tunnel_id_},
                    {"error", ec.message()}
                });
                state_ = State::DISCONNECTED;
                handle_disconnect();
                return;
            }
            
            observability::Logger::instance().info("TLS handshake completed", {
                {"tunnel_id", tunnel_id_}
            });
            
            state_ = State::CONNECTED;
            start_heartbeat();
            start_read();
        });
}

void AgentConnection::start_heartbeat() {
    last_heartbeat_ = std::chrono::steady_clock::now();
    send_heartbeat();
}

void AgentConnection::send_heartbeat() {
    auto self = shared_from_this();
    
    // Schedule next heartbeat in 30 seconds
    heartbeat_timer_.expires_after(std::chrono::seconds(30));
    heartbeat_timer_.async_wait([this, self](const boost::system::error_code& ec) {
        if (ec) {
            return; // Timer cancelled
        }
        
        // Check if heartbeat timeout (60 seconds)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_heartbeat_).count();
        
        if (elapsed > 60) {
            observability::Logger::instance().warning("Heartbeat timeout", {
                {"tunnel_id", tunnel_id_},
                {"elapsed_seconds", std::to_string(elapsed)}
            });
            close();
            return;
        }
        
        // TODO: Send HTTP/2 PING frame or binary heartbeat frame
        observability::Logger::instance().debug("Sending heartbeat", {
            {"tunnel_id", tunnel_id_}
        });
        
        // Schedule next heartbeat
        send_heartbeat();
    });
}

void AgentConnection::start_read() {
    // TODO: Implement async read loop for HTTP/2 or binary protocol
    // This will be implemented with nghttp2 integration
    
    observability::Logger::instance().debug("Started read loop", {
        {"tunnel_id", tunnel_id_}
    });
}

void AgentConnection::send_http_request(
    const std::string& request_id,
    const std::string& http_request,
    request_callback callback,
    std::chrono::seconds timeout) {
    
    if (state_ != State::CONNECTED) {
        callback("", true);
        return;
    }
    
    // Store callback
    {
        std::lock_guard lock(requests_mutex_);
        pending_requests_[request_id] = std::move(callback);
    }
    
    // TODO: Send HTTP/2 stream with request
    // This will be implemented with nghttp2 integration
    
    observability::Logger::instance().debug("HTTP request queued", {
        {"tunnel_id", tunnel_id_},
        {"request_id", request_id},
        {"timeout_seconds", std::to_string(timeout.count())}
    });
    
    // Set timeout
    auto self = shared_from_this();
    auto timer = std::make_shared<boost::asio::steady_timer>(io_context_);
    timer->expires_after(timeout);
    timer->async_wait([this, self, request_id, timer](const boost::system::error_code& ec) {
        if (ec) {
            return; // Cancelled
        }
        
        // Timeout - invoke callback with error
        std::lock_guard lock(requests_mutex_);
        auto it = pending_requests_.find(request_id);
        if (it != pending_requests_.end()) {
            it->second("", true);
            pending_requests_.erase(it);
            
            observability::Logger::instance().warning("Request timeout", {
                {"tunnel_id", tunnel_id_},
                {"request_id", request_id}
            });
        }
    });
}

void AgentConnection::send_tcp_data(
    const std::string& connection_id,
    const std::string& data,
    request_callback callback) {
    
    if (state_ != State::CONNECTED) {
        callback("", true);
        return;
    }
    
    // TODO: Send binary TCP_DATA frame
    // Frame format: [type=0x10][conn_id=16B][seq=4B][len=4B][data]
    
    observability::Logger::instance().debug("TCP data queued", {
        {"tunnel_id", tunnel_id_},
        {"connection_id", connection_id},
        {"bytes", std::to_string(data.size())}
    });
    
    update_stats(data.size(), 0);
    callback("", false);
}

void AgentConnection::send_tcp_close(const std::string& connection_id) {
    if (state_ != State::CONNECTED) {
        return;
    }
    
    // TODO: Send binary TCP_CLOSE frame
    
    observability::Logger::instance().debug("TCP connection close sent", {
        {"tunnel_id", tunnel_id_},
        {"connection_id", connection_id}
    });
}

void AgentConnection::close() {
    if (state_ == State::DISCONNECTED || state_ == State::DISCONNECTING) {
        return;
    }
    
    state_ = State::DISCONNECTING;
    heartbeat_timer_.cancel();
    
    boost::system::error_code ec;
    socket_.shutdown(ec);
    
    if (ec) {
        observability::Logger::instance().warning("Socket shutdown error", {
            {"tunnel_id", tunnel_id_},
            {"error", ec.message()}
        });
    }
    
    state_ = State::DISCONNECTED;
    handle_disconnect();
}

void AgentConnection::handle_disconnect() {
    observability::Logger::instance().info("Agent disconnected", {
        {"tunnel_id", tunnel_id_},
        {"bytes_sent", std::to_string(bytes_sent_)},
        {"bytes_received", std::to_string(bytes_received_)}
    });
    
    // Fail all pending requests
    {
        std::lock_guard lock(requests_mutex_);
        for (auto& [request_id, callback] : pending_requests_) {
            callback("", true);
        }
        pending_requests_.clear();
    }
    
    if (on_disconnect_) {
        on_disconnect_(tunnel_id_);
    }
}

void AgentConnection::update_stats(size_t bytes_sent, size_t bytes_received) {
    bytes_sent_ += bytes_sent;
    bytes_received_ += bytes_received;
    last_heartbeat_ = std::chrono::steady_clock::now();
}

models::TunnelAgent AgentConnection::get_metadata() const {
    models::TunnelAgent agent;
    agent.agent_id = ""; // TODO: Generate UUID
    agent.tunnel_id = tunnel_id_;
    agent.remote_ip = ""; // TODO: Extract from socket
    agent.remote_port = 0;
    agent.state = (state_ == State::CONNECTED) 
        ? models::AgentConnectionState::CONNECTED 
        : models::AgentConnectionState::DISCONNECTED;
    agent.connected_at = connected_at_;
    agent.last_heartbeat = std::chrono::system_clock::now(); // Convert from steady_clock
    agent.bytes_sent = bytes_sent_;
    agent.bytes_received = bytes_received_;
    agent.active_connections = 0; // TODO: Track active connections
    
    return agent;
}

}  // namespace agent
}  // namespace protogate
