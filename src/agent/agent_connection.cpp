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
      http2_session_(nullptr),
      heartbeat_timer_(io_context),
      bytes_sent_(0),
      bytes_received_(0),
      connected_at_(std::chrono::system_clock::now()) {
    
    observability::Logger::instance().debug("AgentConnection created", {
        {"tunnel_id", tunnel_id_}
    });
}

AgentConnection::AgentConnection(ssl_socket&& socket, const std::string& tunnel_id)
    : io_context_(static_cast<boost::asio::io_context&>(socket.lowest_layer().get_executor().context())),
      socket_(std::move(socket)),
      tunnel_id_(tunnel_id),
      state_(State::CONNECTED),  // Already authenticated
      http2_session_(nullptr),
      heartbeat_timer_(io_context_),
      bytes_sent_(0),
      bytes_received_(0),
      connected_at_(std::chrono::system_clock::now()) {
    
    observability::Logger::instance().info("AgentConnection created from authenticated socket", {
        {"tunnel_id", tunnel_id_}
    });
}

void AgentConnection::start(disconnect_callback on_disconnect) {
    on_disconnect_ = std::move(on_disconnect);
    
    if (state_ == State::CONNECTED) {
        // Already authenticated, start session directly
        observability::Logger::instance().info("Starting authenticated session", {
            {"tunnel_id", tunnel_id_}
        });
        initialize_nghttp2();
        start_heartbeat();
        start_read();
    } else {
        // Need to perform handshake
        do_handshake();
    }
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
            initialize_nghttp2();
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
        
        // Send HTTP/2 PING frame
        if (http2_session_) {
            uint8_t ping_data[8] = {0};
            nghttp2_submit_ping(http2_session_, NGHTTP2_FLAG_NONE, ping_data);
            send_pending_data();
            
            observability::Logger::instance().debug("Sending heartbeat PING", {
                {"tunnel_id", tunnel_id_}
            });
        }
        
        // Schedule next heartbeat
        send_heartbeat();
    });
}

void AgentConnection::initialize_nghttp2() {
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    
    nghttp2_session_server_new(&http2_session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    
    // Send initial SETTINGS frame
    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535}
    };
    nghttp2_submit_settings(http2_session_, NGHTTP2_FLAG_NONE, settings, 2);
    send_pending_data();
    
    observability::Logger::instance().info("HTTP/2 session initialized", {
        {"tunnel_id", tunnel_id_}
    });
}

void AgentConnection::send_pending_data() {
    const uint8_t* data;
    while (true) {
        ssize_t len = nghttp2_session_mem_send(http2_session_, &data);
        if (len <= 0) break;
        
        send_buffer_.insert(send_buffer_.end(), data, data + len);
    }
    
    if (!send_buffer_.empty()) {
        auto self = shared_from_this();
        boost::asio::async_write(socket_, boost::asio::buffer(send_buffer_),
            [this, self](const boost::system::error_code& ec, std::size_t bytes) {
                if (ec) {
                    observability::Logger::instance().error("Send error", {
                        {"tunnel_id", tunnel_id_},
                        {"error", ec.message()}
                    });
                    close();
                    return;
                }
                
                bytes_sent_ += bytes;
                send_buffer_.clear();
            });
    }
}

ssize_t AgentConnection::send_callback(nghttp2_session* session, const uint8_t* data,
                                       size_t length, int flags, void* user_data) {
    auto* conn = static_cast<AgentConnection*>(user_data);
    conn->send_buffer_.insert(conn->send_buffer_.end(), data, data + length);
    return static_cast<ssize_t>(length);
}

int AgentConnection::on_frame_recv_callback(nghttp2_session* session,
                                            const nghttp2_frame* frame, void* user_data) {
    auto* conn = static_cast<AgentConnection*>(user_data);
    
    switch (frame->hd.type) {
        case NGHTTP2_PING:
            if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
                conn->last_heartbeat_ = std::chrono::steady_clock::now();
                observability::Logger::instance().debug("PING ACK received", {
                    {"tunnel_id", conn->tunnel_id_}
                });
            }
            break;
            
        case NGHTTP2_HEADERS:
            if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
                observability::Logger::instance().debug("Request headers received", {
                    {"tunnel_id", conn->tunnel_id_},
                    {"stream_id", std::to_string(frame->hd.stream_id)}
                });
            }
            break;
            
        case NGHTTP2_DATA:
            observability::Logger::instance().debug("Data frame received", {
                {"tunnel_id", conn->tunnel_id_},
                {"stream_id", std::to_string(frame->hd.stream_id)},
                {"length", std::to_string(frame->hd.length)}
            });
            break;
            
        case NGHTTP2_GOAWAY:
            observability::Logger::instance().info("GOAWAY received", {
                {"tunnel_id", conn->tunnel_id_}
            });
            conn->close();
            break;
    }
    
    return 0;
}

int AgentConnection::on_header_callback(nghttp2_session* session, const nghttp2_frame* frame,
                                        const uint8_t* name, size_t namelen,
                                        const uint8_t* value, size_t valuelen,
                                        uint8_t flags, void* user_data) {
    auto* conn = static_cast<AgentConnection*>(user_data);
    
    std::string header_name(reinterpret_cast<const char*>(name), namelen);
    std::string header_value(reinterpret_cast<const char*>(value), valuelen);
    
    // Store headers for this stream
    conn->stream_headers_[frame->hd.stream_id] += header_name + ": " + header_value + "\r\n";
    
    observability::Logger::instance().debug("Header received", {
        {"tunnel_id", conn->tunnel_id_},
        {"stream_id", std::to_string(frame->hd.stream_id)},
        {"name", header_name},
        {"value", header_value}
    });
    
    return 0;
}

int AgentConnection::on_data_chunk_recv_callback(nghttp2_session* session, uint8_t flags,
                                                 int32_t stream_id, const uint8_t* data,
                                                 size_t len, void* user_data) {
    auto* conn = static_cast<AgentConnection*>(user_data);
    
    // Store data for this stream
    conn->stream_bodies_[stream_id].append(reinterpret_cast<const char*>(data), len);
    
    observability::Logger::instance().debug("Data chunk received", {
        {"tunnel_id", conn->tunnel_id_},
        {"stream_id", std::to_string(stream_id)},
        {"length", std::to_string(len)}
    });
    
    return 0;
}

void AgentConnection::start_read() {
    auto self = shared_from_this();
    
    socket_.async_read_some(boost::asio::buffer(recv_buffer_),
        [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                if (ec != boost::asio::error::eof) {
                    observability::Logger::instance().error("Read error", {
                        {"tunnel_id", tunnel_id_},
                        {"error", ec.message()}
                    });
                }
                close();
                return;
            }
            
            bytes_received_ += bytes_transferred;
            
            // Feed data to nghttp2
            ssize_t rv = nghttp2_session_mem_recv(http2_session_, 
                                                  recv_buffer_.data(), 
                                                  bytes_transferred);
            if (rv < 0) {
                observability::Logger::instance().error("nghttp2 error", {
                    {"tunnel_id", tunnel_id_},
                    {"error", nghttp2_strerror(static_cast<int>(rv))}
                });
                close();
                return;
            }
            
            // Send any pending responses
            send_pending_data();
            
            // Continue reading
            start_read();
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
    
    // Clean up nghttp2 session
    if (http2_session_) {
        nghttp2_session_del(http2_session_);
        http2_session_ = nullptr;
    }
    
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
