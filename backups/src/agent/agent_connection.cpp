#include "agent_connection.h"
#include "../observability/logger.h"
#include <sstream>
#include <algorithm>

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
    
    // Set socket to non-blocking for async operations
    socket_.lowest_layer().non_blocking(true);
    
    // Verify executor
    auto& executor_context = static_cast<boost::asio::io_context&>(
        socket_.lowest_layer().get_executor().context());
    
    observability::Logger::instance().info("Socket set to non-blocking mode", {
        {"tunnel_id", tunnel_id_},
        {"socket_executor_addr", std::to_string(reinterpret_cast<uintptr_t>(&executor_context))},
        {"member_io_context_addr", std::to_string(reinterpret_cast<uintptr_t>(&io_context_))},
        {"match", (&executor_context == &io_context_) ? "YES" : "NO"}
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
    
    // After CONNECT, server acts as HTTP/2 client to send requests to agent
    nghttp2_session_client_new(&http2_session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    
    // Send HTTP/2 connection preface and SETTINGS
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
    // nghttp2 will call send_callback which handles actual transmission
    // This method just triggers nghttp2 to flush its buffers
    int rv = nghttp2_session_send(http2_session_);
    if (rv != 0) {
        observability::Logger::instance().error("nghttp2_session_send failed", {
            {"tunnel_id", tunnel_id_},
            {"error", nghttp2_strerror(rv)}
        });
        close();
    }
}

ssize_t AgentConnection::send_callback(nghttp2_session* session, const uint8_t* data,
                                       size_t length, int flags, void* user_data) {
    auto* conn = static_cast<AgentConnection*>(user_data);
    (void)session;
    (void)flags;
    
    observability::Logger::instance().info("send_callback invoked", {
        {"tunnel_id", conn->tunnel_id_},
        {"bytes", std::to_string(length)}
    });
    
    try {
        boost::system::error_code ec;
        size_t written = conn->socket_.write_some(
            boost::asio::buffer(data, length), ec);
        
        if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
            observability::Logger::instance().info("send_callback would block", {
                {"tunnel_id", conn->tunnel_id_}
            });
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        
        if (ec) {
            observability::Logger::instance().error("Send callback error", {
                {"tunnel_id", conn->tunnel_id_},
                {"error", ec.message()}
            });
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        
        conn->bytes_sent_ += written;
        
        observability::Logger::instance().info("HTTP/2 frames sent", {
            {"tunnel_id", conn->tunnel_id_},
            {"bytes", std::to_string(written)}
        });
        
        return static_cast<ssize_t>(written);
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Send callback exception", {
            {"tunnel_id", conn->tunnel_id_},
            {"error", e.what()}
        });
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
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
            if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
                observability::Logger::instance().debug("Response headers received", {
                    {"tunnel_id", conn->tunnel_id_},
                    {"stream_id", std::to_string(frame->hd.stream_id)}
                });
                
                // If END_STREAM flag is set and no body, complete the request
                if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                    conn->complete_request(frame->hd.stream_id);
                }
            }
            break;
            
        case NGHTTP2_DATA:
            observability::Logger::instance().debug("Data frame received", {
                {"tunnel_id", conn->tunnel_id_},
                {"stream_id", std::to_string(frame->hd.stream_id)},
                {"length", std::to_string(frame->hd.length)}
            });
            
            // If END_STREAM flag is set, complete the request
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                conn->complete_request(frame->hd.stream_id);
            }
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
    
    // Convert HTTP/2 :status to HTTP/1.1 status line
    if (header_name == ":status") {
        // If this is first header, initialize with HTTP/1.1 status line
        if (conn->stream_headers_.find(frame->hd.stream_id) == conn->stream_headers_.end()) {
            conn->stream_headers_[frame->hd.stream_id] = "HTTP/1.1 " + header_value + " OK\r\n";
        }
    } else if (header_name[0] == ':') {
        // Skip other HTTP/2 pseudo-headers (:method, :path, :scheme, :authority)
        observability::Logger::instance().debug("Skipping HTTP/2 pseudo-header", {
            {"tunnel_id", conn->tunnel_id_},
            {"name", header_name}
        });
    } else {
        // Store regular headers
        conn->stream_headers_[frame->hd.stream_id] += header_name + ": " + header_value + "\r\n";
    }
    
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
    conn->stream_bodies_recv_[stream_id].append(reinterpret_cast<const char*>(data), len);
    
    observability::Logger::instance().debug("Data chunk received", {
        {"tunnel_id", conn->tunnel_id_},
        {"stream_id", std::to_string(stream_id)},
        {"length", std::to_string(len)}
    });
    
    return 0;
}

void AgentConnection::complete_request(int32_t stream_id) {
    std::lock_guard lock(requests_mutex_);
    
    // Find request_id for this stream
    auto stream_it = stream_to_request_.find(stream_id);
    if (stream_it == stream_to_request_.end()) {
        observability::Logger::instance().warning("No request found for stream", {
            {"tunnel_id", tunnel_id_},
            {"stream_id", std::to_string(stream_id)}
        });
        return;
    }
    
    std::string request_id = stream_it->second;
    
    // Find callback
    auto callback_it = pending_requests_.find(request_id);
    if (callback_it == pending_requests_.end()) {
        observability::Logger::instance().warning("No callback found for request", {
            {"tunnel_id", tunnel_id_},
            {"request_id", request_id}
        });
        return;
    }
    
    // Build HTTP response from headers and body
    std::string response;
    
    // Add headers
    if (stream_headers_.count(stream_id)) {
        response = stream_headers_[stream_id];
    }
    
    // Add blank line between headers and body
    if (!response.empty()) {
        response += "\r\n";
    }
    
    // Add body
    if (stream_bodies_recv_.count(stream_id)) {
        response += stream_bodies_recv_[stream_id];
    }
    
    observability::Logger::instance().info("Request completed", {
        {"tunnel_id", tunnel_id_},
        {"request_id", request_id},
        {"stream_id", std::to_string(stream_id)},
        {"response_size", std::to_string(response.size())}
    });
    
    // Invoke callback
    callback_it->second(response, false);
    
    // Clean up
    pending_requests_.erase(callback_it);
    stream_to_request_.erase(stream_it);
    stream_headers_.erase(stream_id);
    stream_bodies_recv_.erase(stream_id);
    stream_bodies_.erase(request_id);
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
            
            observability::Logger::instance().info("Received data from agent", {
                {"tunnel_id", tunnel_id_},
                {"bytes", std::to_string(bytes_transferred)}
            });
            
            // Check if data contains TCP frames (type 0x10-0x14)
            // TCP frames should be processed separately, not fed to nghttp2
            size_t tcp_consumed = 0;
            if (bytes_transferred > 0 && recv_buffer_[0] >= 0x10 && recv_buffer_[0] <= 0x14) {
                observability::Logger::instance().info("Detected TCP frame from agent", {
                    {"tunnel_id", tunnel_id_},
                    {"frame_type", std::to_string(static_cast<int>(recv_buffer_[0]))}
                });
                
                // Forward to TCP frame handler if set
                if (tcp_frame_handler_) {
                    tcp_frame_handler_(recv_buffer_.data(), bytes_transferred);
                } else {
                    observability::Logger::instance().warning("TCP frame dropped: no handler", {
                        {"tunnel_id", tunnel_id_}
                    });
                }
                
                tcp_consumed = bytes_transferred;  // All bytes were TCP frames
            }
            
            // Feed remaining data to nghttp2
            if (tcp_consumed < bytes_transferred) {
                ssize_t rv = nghttp2_session_mem_recv(http2_session_, 
                                                      recv_buffer_.data() + tcp_consumed, 
                                                      bytes_transferred - tcp_consumed);
                if (rv < 0) {
                    observability::Logger::instance().error("nghttp2_session_mem_recv error", {
                        {"tunnel_id", tunnel_id_},
                        {"error", nghttp2_strerror(static_cast<int>(rv))}
                    });
                    close();
                    return;
                }
                
                observability::Logger::instance().info("nghttp2_session_mem_recv succeeded", {
                    {"tunnel_id", tunnel_id_},
                    {"consumed", std::to_string(rv)}
                });
            }
            // CRITICAL: After receiving data, must call nghttp2_session_send()
            // This sends response frames like SETTINGS ACK
            int send_rv = nghttp2_session_send(http2_session_);
            if (send_rv != 0) {
                observability::Logger::instance().error("nghttp2_session_send after recv failed", {
                    {"tunnel_id", tunnel_id_},
                    {"error", nghttp2_strerror(send_rv)}
                });
                close();
                return;
            }
            
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
    
    if (!http2_session_) {
        observability::Logger::instance().error("HTTP/2 session not initialized", {
            {"tunnel_id", tunnel_id_}
        });
        callback("", true);
        return;
    }
    
    // Parse HTTP request to extract method, path, headers, body
    std::istringstream request_stream(http_request);
    std::string request_line;
    std::getline(request_stream, request_line);
    
    // Parse request line: METHOD PATH HTTP/VERSION
    std::istringstream line_stream(request_line);
    std::string method, path, version;
    line_stream >> method >> path >> version;
    
    // Parse headers
    std::map<std::string, std::string> headers;
    std::string header_line;
    while (std::getline(request_stream, header_line) && !header_line.empty() && header_line != "\r") {
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        
        size_t colon = header_line.find(':');
        if (colon != std::string::npos) {
            std::string name = header_line.substr(0, colon);
            std::string value = header_line.substr(colon + 1);
            
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Convert to lowercase for HTTP/2
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            headers[name] = value;
        }
    }
    
    // Read body
    std::ostringstream body_stream;
    body_stream << request_stream.rdbuf();
    std::string body = body_stream.str();
    
    // Build header storage first (all strings) to avoid vector reallocation issues
    std::vector<std::pair<std::string, std::string>> header_pairs;
    header_pairs.emplace_back(":method", method);
    header_pairs.emplace_back(":path", path);
    header_pairs.emplace_back(":scheme", "https");
    
    if (headers.count("host")) {
        header_pairs.emplace_back(":authority", headers.at("host"));
    }
    
    header_pairs.emplace_back("x-tunnel-request-id", request_id);
    
    // Regular headers (skip host as it's now :authority)
    for (const auto& [name, value] : headers) {
        if (name != "host") {
            header_pairs.emplace_back(name, value);
        }
    }
    
    // Now create nghttp2_nv array pointing to the stable strings in header_pairs
    std::vector<nghttp2_nv> hdrs;
    hdrs.reserve(header_pairs.size());
    for (const auto& [name, value] : header_pairs) {
        hdrs.push_back({
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(name.data())),
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(value.data())),
            name.size(),
            value.size(),
            NGHTTP2_NV_FLAG_NONE
        });
    }
    
    // Submit headers with END_STREAM if no body
    int flags = NGHTTP2_FLAG_END_HEADERS;
    if (body.empty()) {
        flags |= NGHTTP2_FLAG_END_STREAM;
    }
    
    int32_t stream_id = nghttp2_submit_headers(http2_session_, flags, 
                                                -1, nullptr,
                                                hdrs.data(), hdrs.size(), 
                                                nullptr);
    
    if (stream_id < 0) {
        observability::Logger::instance().error("Failed to submit HTTP/2 headers", {
            {"tunnel_id", tunnel_id_},
            {"error", nghttp2_strerror(stream_id)}
        });
        callback("", true);
        return;
    }
    
    // Send body if present
    if (!body.empty()) {
        // Store body in pending requests map to keep it alive
        stream_bodies_[request_id] = body;
        
        nghttp2_data_provider body_provider;
        body_provider.source.ptr = &stream_bodies_[request_id];
        body_provider.read_callback = [](nghttp2_session* session, int32_t stream_id,
                                         uint8_t* buf, size_t length, uint32_t* data_flags,
                                         nghttp2_data_source* source, void* user_data) -> ssize_t {
            auto* body_ptr = static_cast<std::string*>(source->ptr);
            size_t to_copy = std::min(length, body_ptr->size());
            std::memcpy(buf, body_ptr->data(), to_copy);
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            return to_copy;
        };
        
        int rv = nghttp2_submit_data(http2_session_, NGHTTP2_FLAG_END_STREAM,
                                     stream_id, &body_provider);
        if (rv != 0) {
            observability::Logger::instance().error("Failed to submit HTTP/2 body", {
                {"tunnel_id", tunnel_id_},
                {"stream_id", std::to_string(stream_id)},
                {"error", nghttp2_strerror(rv)}
            });
        }
    } else {
        // No body, end stream with another HEADERS frame
        std::vector<nghttp2_nv> empty_hdrs;
        nghttp2_submit_headers(http2_session_, NGHTTP2_FLAG_END_STREAM,
                              stream_id, nullptr, empty_hdrs.data(), 0, nullptr);
    }
    
    // Store callback mapped to stream_id
    {
        std::lock_guard lock(requests_mutex_);
        pending_requests_[request_id] = std::move(callback);
        stream_to_request_[stream_id] = request_id;
    }
    
    // Send the request
    send_pending_data();
    
    observability::Logger::instance().info("HTTP/2 request sent to agent", {
        {"tunnel_id", tunnel_id_},
        {"request_id", request_id},
        {"stream_id", std::to_string(stream_id)},
        {"method", method},
        {"path", path}
    });
    
    // Set timeout
    auto self = shared_from_this();
    auto timer = std::make_shared<boost::asio::steady_timer>(io_context_);
    timer->expires_after(timeout);
    timer->async_wait([this, self, request_id, stream_id, timer](const boost::system::error_code& ec) {
        if (ec) {
            return; // Cancelled
        }
        
        // Timeout - invoke callback with error
        std::lock_guard lock(requests_mutex_);
        auto it = pending_requests_.find(request_id);
        if (it != pending_requests_.end()) {
            it->second("", true);
            pending_requests_.erase(it);
            stream_to_request_.erase(stream_id);
            stream_bodies_.erase(request_id);
            
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
    const request_callback& callback) {
    
    observability::Logger::instance().info("send_tcp_data called with callback", {
        {"connection_id", connection_id},
        {"callback_valid", callback ? "YES" : "NO"},
        {"callback_ptr", std::to_string(reinterpret_cast<uintptr_t>(&callback))},
        {"callback_target_type", callback.target_type().name()}
    });
    
    if (state_ != State::CONNECTED) {
        observability::Logger::instance().warning("Cannot send TCP data - not connected", {
            {"tunnel_id", tunnel_id_},
            {"connection_id", connection_id},
            {"state", std::to_string(static_cast<int>(state_))}
        });
        callback("", true);
        return;
    }
    
    observability::Logger::instance().info("Attempting to send TCP frame via sync write", {
        {"tunnel_id", tunnel_id_},
        {"connection_id", connection_id},
        {"frame_size", std::to_string(data.size())},
        {"socket_open", socket_.lowest_layer().is_open() ? "YES" : "NO"}
    });
    
    // Send binary TCP frame over TLS socket (out-of-band from HTTP/2)
    // Use synchronous write_some like nghttp2's send_callback does - they share the same socket
    try {
        boost::system::error_code ec;
        size_t bytes_transferred = socket_.write_some(boost::asio::buffer(data), ec);
        
        if (ec) {
            observability::Logger::instance().error("Failed to send TCP data", {
                {"tunnel_id", tunnel_id_},
                {"connection_id", connection_id},
                {"error", ec.message()}
            });
            callback("", true);
            return;
        }
        
        observability::Logger::instance().info("TCP frame sent successfully", {
            {"tunnel_id", tunnel_id_},
            {"connection_id", connection_id},
            {"bytes", std::to_string(bytes_transferred)}
        });
        
        observability::Logger::instance().info("About to invoke callback", {
            {"connection_id", connection_id},
            {"error", "false"}
        });
        
        update_stats(bytes_transferred, 0);
        
        try {
            observability::Logger::instance().info("Invoking callback NOW", {
                {"connection_id", connection_id}
            });
            callback("", false);
            observability::Logger::instance().info("Callback returned normally", {
                {"connection_id", connection_id}
            });
        } catch (const std::exception& ex) {
            observability::Logger::instance().error("Exception during callback invocation", {
                {"connection_id", connection_id},
                {"error", ex.what()}
            });
        } catch (...) {
            observability::Logger::instance().error("Unknown exception during callback invocation", {
                {"connection_id", connection_id}
            });
        }
        
        observability::Logger::instance().info("Callback invoked successfully", {
            {"connection_id", connection_id}
        });
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Exception sending TCP data", {
            {"tunnel_id", tunnel_id_},
            {"connection_id", connection_id},
            {"error", e.what()}
        });
        callback("", true);
    }
}

void AgentConnection::send_tcp_close(const std::string& connection_id) {
    if (state_ != State::CONNECTED) {
        return;
    }
    
    // Create and send TCP_CLOSE frame
    // Note: This is a simplified version - actual implementation needs tcp_protocol.h import
    // For now, send a minimal close frame: [type=0x12][conn_id=16B][reason=0x0000]
    std::vector<uint8_t> frame;
    frame.push_back(0x12);  // TCP_CLOSE frame type
    
    // Add connection ID (16 bytes) - simplified UUID representation
    for (size_t i = 0; i < 16 && i < connection_id.size(); ++i) {
        frame.push_back(static_cast<uint8_t>(connection_id[i]));
    }
    while (frame.size() < 17) {
        frame.push_back(0);
    }
    
    // Add reason code (2 bytes) - NORMAL=0x0000
    frame.push_back(0x00);
    frame.push_back(0x00);
    
    std::string frame_str(frame.begin(), frame.end());
    
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(frame_str),
        [this, connection_id](const boost::system::error_code& ec, size_t) {
            if (ec) {
                observability::Logger::instance().error("Failed to send TCP close", {
                    {"tunnel_id", tunnel_id_},
                    {"connection_id", connection_id},
                    {"error", ec.message()}
                });
                return;
            }
            
            observability::Logger::instance().debug("TCP connection close sent", {
                {"tunnel_id", tunnel_id_},
                {"connection_id", connection_id}
            });
        });
}

void AgentConnection::set_tcp_frame_handler(tcp_frame_callback callback) {
    tcp_frame_handler_ = callback;
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
