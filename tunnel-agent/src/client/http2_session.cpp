#include "http2_session.h"
#include "../utils/logger.h"
#include <cstring>
#include <sstream>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <stdexcept>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace protogate {
namespace agent {

HTTP2Session::HTTP2Session(boost::asio::io_context& io_context, HttpClient& http_client, 
                           const std::string& tunnel_id, const std::string& token)
    : io_context_(io_context), http_client_(http_client), tunnel_id_(tunnel_id), 
      token_(token), session_(nullptr), active_(false), tcp_forwarder_(nullptr) {
}

HTTP2Session::~HTTP2Session() {
    stop();
}

void HTTP2Session::start(RequestCallback on_request) {
    on_request_ = on_request;
    
    Logger::info("Starting HTTP/2 session after HTTP upgrade");
    
    // HTTP upgrade already handled by HttpClient
    // Socket is now ready for HTTP/2 communication
    
    // Initialize nghttp2 session
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    
    // After CONNECT succeeds, agent acts as HTTP/2 server to receive requests
    nghttp2_session_server_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    
    // Send initial SETTINGS frame
    nghttp2_settings_entry iv[1] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
    };
    nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv, 1);
    
    // Send pending frames
    send_data();
    
    active_ = true;
    Logger::info("HTTP/2 session active flag set to TRUE");
    
    // Start async read loop
    start_async_read();
    Logger::info("HTTP/2 session started", {
        {"tunnel_id", tunnel_id_}
    });
}

void HTTP2Session::stop() {
    if (!active_) {
        return;
    }
    
    if (session_) {
        nghttp2_session_del(session_);
        session_ = nullptr;
    }
    
    active_ = false;
    Logger::info("HTTP/2 session active flag set to FALSE in stop()");
    
    Logger::info("HTTP/2 session stopped");
}

bool HTTP2Session::is_active() const {
    return active_;
}

void HTTP2Session::set_ping_ack_callback(PingAckCallback callback) {
    on_ping_ack_ = std::move(callback);
}


void HTTP2Session::send_connect_request() {
    // Build CONNECT request headers
    std::vector<nghttp2_nv> hdrs;
    
    const char* method = ":method";
    const char* method_val = "CONNECT";
    hdrs.push_back({(uint8_t*)method, (uint8_t*)method_val, strlen(method), strlen(method_val), NGHTTP2_NV_FLAG_NONE});
    
    const char* scheme = ":scheme";
    const char* scheme_val = "https";
    hdrs.push_back({(uint8_t*)scheme, (uint8_t*)scheme_val, strlen(scheme), strlen(scheme_val), NGHTTP2_NV_FLAG_NONE});
    
    const char* authority = ":authority";
    const char* authority_val = "tunnel-agent";
    hdrs.push_back({(uint8_t*)authority, (uint8_t*)authority_val, strlen(authority), strlen(authority_val), NGHTTP2_NV_FLAG_NONE});
    
    const char* path = ":path";
    const char* path_val = "/";
    hdrs.push_back({(uint8_t*)path, (uint8_t*)path_val, strlen(path), strlen(path_val), NGHTTP2_NV_FLAG_NONE});
    
    // Store header strings as member variables to keep them alive
    auth_header_name_ = "authorization";
    auth_header_value_ = "Bearer " + token_;
    hdrs.push_back({(uint8_t*)auth_header_name_.c_str(), (uint8_t*)auth_header_value_.c_str(), 
                   auth_header_name_.size(), auth_header_value_.size(), NGHTTP2_NV_FLAG_NONE});
    
    tunnel_header_name_ = "x-tunnel-id";
    hdrs.push_back({(uint8_t*)tunnel_header_name_.c_str(), (uint8_t*)tunnel_id_.c_str(), 
                   tunnel_header_name_.size(), tunnel_id_.size(), NGHTTP2_NV_FLAG_NONE});
    
    version_header_name_ = "x-agent-version";
    const char* version_val = "1.0.0";
    hdrs.push_back({(uint8_t*)version_header_name_.c_str(), (uint8_t*)version_val, 
                   version_header_name_.size(), strlen(version_val), NGHTTP2_NV_FLAG_NONE});
    
    int32_t stream_id = nghttp2_submit_request(session_, nullptr, hdrs.data(), hdrs.size(), nullptr, nullptr);
    
    if (stream_id < 0) {
        throw std::runtime_error("Failed to submit CONNECT request");
    }
    
    Logger::info("Sent CONNECT request", {
        {"stream_id", std::to_string(stream_id)},
        {"tunnel_id", tunnel_id_}
    });
}

void HTTP2Session::send_response(int32_t stream_id, int status_code, 
                                 const std::map<std::string, std::string>& headers,
                                 const std::vector<uint8_t>& body) {
    // Store body to keep it alive
    response_bodies_[stream_id] = body;
    
    // Build header storage to prevent dangling pointers
    std::vector<std::pair<std::string, std::string>> header_pairs;
    header_pairs.emplace_back(":status", std::to_string(status_code));
    
    for (const auto& [key, value] : headers) {
        header_pairs.emplace_back(key, value);
    }
    
    // Build nghttp2_nv array from stable header_pairs
    std::vector<nghttp2_nv> hdrs;
    for (const auto& [name, value] : header_pairs) {
        hdrs.push_back({
            (uint8_t*)name.data(), (uint8_t*)value.data(),
            name.size(), value.size(),
            NGHTTP2_NV_FLAG_NONE
        });
    }
    
    // Create data provider for response body pointing to stored copy
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = (void*)&response_bodies_[stream_id];
    data_prd.read_callback = [](nghttp2_session* session, int32_t stream_id,
                                uint8_t* buf, size_t length, uint32_t* data_flags,
                                nghttp2_data_source* source, void* user_data) -> ssize_t {
        auto* body_ptr = (std::vector<uint8_t>*)source->ptr;
        size_t to_copy = std::min(length, body_ptr->size());
        std::memcpy(buf, body_ptr->data(), to_copy);
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        return to_copy;
    };
    
    int rv = nghttp2_submit_response(session_, stream_id, hdrs.data(), hdrs.size(), &data_prd);
    
    if (rv != 0) {
        Logger::error("Failed to submit response", {
            {"stream_id", std::to_string(stream_id)},
            {"error", nghttp2_strerror(rv)}
        });
        return;
    }
    
    send_data();
    
    Logger::debug("Sent response", {
        {"stream_id", std::to_string(stream_id)},
        {"status", std::to_string(status_code)},
        {"body_size", std::to_string(body.size())}
    });
}

void HTTP2Session::send_ping() {
    uint8_t opaque_data[8] = {0};
    nghttp2_submit_ping(session_, NGHTTP2_FLAG_NONE, opaque_data);
    send_data();
    
    Logger::debug("Sent PING frame");
}

void HTTP2Session::process_events() {
    send_data();
}

void HTTP2Session::send_data() {
    int rv = nghttp2_session_send(session_);
    if (rv != 0) {
        Logger::error("nghttp2_session_send failed", {
            {"error", nghttp2_strerror(rv)}
        });
        active_ = false;
    }
}

void HTTP2Session::start_async_read() {
    if (!active_) {
        Logger::info("start_async_read called but session not active");
        return;
    }
    
    Logger::info("Posting async_read_some to io_context", {
        {"io_context_addr", std::to_string(reinterpret_cast<uintptr_t>(&io_context_))},
        {"socket_is_open", http_client_.ssl_socket().lowest_layer().is_open() ? "YES" : "NO"},
        {"active_", active_ ? "YES" : "NO"}
    });
    
    // Start async read operation
    http_client_.ssl_socket().async_read_some(
        boost::asio::buffer(read_buffer_),
        [this](const boost::system::error_code& ec, size_t bytes_transferred) {
            Logger::info("async_read_some completion handler FIRED", {
                {"error", ec.message()},
                {"bytes", std::to_string(bytes_transferred)},
                {"active_", active_ ? "YES" : "NO"}
            });
            handle_read(ec, bytes_transferred);
        }
    );
    
    Logger::info("async_read_some posted successfully");
}

void HTTP2Session::handle_read(const boost::system::error_code& ec, size_t bytes_transferred) {
    if (ec) {
        if (ec != boost::asio::error::eof) {
            Logger::error("Socket read error", {
                {"error", ec.message()}
            });
        }
        active_ = false;
        return;
    }
    
    if (bytes_transferred > 0) {
        Logger::info("Received data from server", {
            {"bytes", std::to_string(bytes_transferred)}
        });
        
        // Check if this might be TCP frame data
        // TCP frames start with 0x10-0x14, HTTP/2 frames typically start with 0x00-0x09
        size_t tcp_consumed = 0;
        if (is_tcp_frame(read_buffer_.data(), bytes_transferred)) {
            Logger::info("Detected TCP frame in buffer", {
                {"first_byte", std::to_string(static_cast<int>(read_buffer_.data()[0]))},
                {"bytes", std::to_string(bytes_transferred)}
            });
            
            // Process TCP frames first
            tcp_consumed = process_tcp_frames(read_buffer_.data(), bytes_transferred);
            Logger::info("Processed TCP frames", {
                {"consumed", std::to_string(tcp_consumed)},
                {"remaining", std::to_string(bytes_transferred - tcp_consumed)}
            });
        } else {
            Logger::info("Not a TCP frame, passing to HTTP/2", {
                {"first_byte", std::to_string(static_cast<int>(read_buffer_.data()[0]))},
                {"bytes", std::to_string(bytes_transferred)}
            });
        }
        
        // Process remaining data as HTTP/2 if any left
        if (tcp_consumed < bytes_transferred) {
            const uint8_t* http2_data = read_buffer_.data() + tcp_consumed;
            size_t http2_length = bytes_transferred - tcp_consumed;
            
            // Feed received data to nghttp2
            ssize_t readlen = nghttp2_session_mem_recv(session_, http2_data, http2_length);
            if (readlen < 0) {
                Logger::error("nghttp2_session_mem_recv failed", {
                    {"error", nghttp2_strerror(static_cast<int>(readlen))}
                });
                active_ = false;
                return;
            }
            
            Logger::info("nghttp2_session_mem_recv succeeded", {
                {"consumed", std::to_string(readlen)}
            });
            
            // CRITICAL: After receiving data, must call nghttp2_session_send()
            // This sends response frames like SETTINGS ACK
            int rv = nghttp2_session_send(session_);
            if (rv != 0) {
                Logger::error("nghttp2_session_send after recv failed", {
                    {"error", nghttp2_strerror(rv)}
                });
                active_ = false;
                return;
            }
            
            Logger::info("nghttp2_session_send after recv succeeded");
        }
    }
    
    // Continue reading
    start_async_read();
}

ssize_t HTTP2Session::send_callback(nghttp2_session* session, const uint8_t* data,
                                   size_t length, int flags, void* user_data) {
    auto* self = static_cast<HTTP2Session*>(user_data);
    
    try {
        boost::system::error_code ec;
        size_t written = self->http_client_.ssl_socket().write_some(
            boost::asio::buffer(data, length), ec);
        
        if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        
        if (ec) {
            Logger::error("Send callback error", {
                {"error", ec.message()}
            });
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        
        return written;
        
    } catch (const std::exception& e) {
        Logger::error("Send callback exception", {
            {"error", e.what()}
        });
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
}

int HTTP2Session::on_frame_recv_callback(nghttp2_session* session,
                                        const nghttp2_frame* frame, void* user_data) {
    auto* self = static_cast<HTTP2Session*>(user_data);
    
    Logger::info("Frame received", {
        {"stream_id", std::to_string(frame->hd.stream_id)},
        {"type", std::to_string(frame->hd.type)},
        {"flags", std::to_string(frame->hd.flags)}
    });
    
    switch (frame->hd.type) {
    case NGHTTP2_HEADERS:
        Logger::info("Processing HEADERS frame", {
            {"stream_id", std::to_string(frame->hd.stream_id)},
            {"has_end_headers", std::to_string((frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) != 0)},
            {"has_end_stream", std::to_string((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0)}
        });
        if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
            // This is a new request from server
            auto it = self->pending_requests_.find(frame->hd.stream_id);
            Logger::info("Looking up pending request", {
                {"stream_id", std::to_string(frame->hd.stream_id)},
                {"found", std::to_string(it != self->pending_requests_.end())}
            });
            if (it != self->pending_requests_.end()) {
                Logger::debug("Request headers complete", {
                    {"stream_id", std::to_string(frame->hd.stream_id)}
                });
                
                // If END_STREAM is also set (no body), complete the request now
                if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                    Logger::debug("Request complete (no body)", {
                        {"stream_id", std::to_string(frame->hd.stream_id)}
                    });
                    if (self->on_request_) {
                        self->on_request_(it->second);
                    }
                    self->pending_requests_.erase(it);
                }
            }
        }
        break;
        
    case NGHTTP2_DATA:
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            // Request complete, invoke callback
            auto it = self->pending_requests_.find(frame->hd.stream_id);
            if (it != self->pending_requests_.end()) {
                if (self->on_request_) {
                    self->on_request_(it->second);
                }
                self->pending_requests_.erase(it);
            }
        }
        break;
        
    case NGHTTP2_PING:
        if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
            Logger::info("Received PING ACK from server");
            if (self->on_ping_ack_) {
                self->on_ping_ack_();
            }
        } else {
            Logger::info("Received PING request from server - nghttp2 will auto-respond");
        }
        break;
    }
    
    return 0;
}

int HTTP2Session::on_header_callback(nghttp2_session* session,
                                    const nghttp2_frame* frame,
                                    const uint8_t* name, size_t namelen,
                                    const uint8_t* value, size_t valuelen,
                                    uint8_t flags, void* user_data) {
    auto* self = static_cast<HTTP2Session*>(user_data);
    
    // Store request headers
    auto& req = self->pending_requests_[frame->hd.stream_id];
    req.stream_id = frame->hd.stream_id;
    
    std::string name_str((char*)name, namelen);
    std::string value_str((char*)value, valuelen);
    
    Logger::info("Header received", {
        {"stream_id", std::to_string(frame->hd.stream_id)},
        {"name", name_str},
        {"value", value_str}
    });
    
    if (name_str == ":method") {
        req.method = value_str;
    } else if (name_str == ":path") {
        req.path = value_str;
    } else if (name_str == ":authority") {
        req.authority = value_str;
    } else if (name_str == "x-tunnel-request-id") {
        req.request_id = value_str;
    } else if (name_str == "x-client-ip") {
        req.client_ip = value_str;
    } else {
        req.headers[name_str] = value_str;
    }
    
    return 0;
}

int HTTP2Session::on_data_chunk_recv_callback(nghttp2_session* session,
                                              uint8_t flags, int32_t stream_id,
                                              const uint8_t* data, size_t len,
                                              void* user_data) {
    auto* self = static_cast<HTTP2Session*>(user_data);
    
    if (stream_id == 1) {
        // CONNECT response body (should be empty)
        return 0;
    }
    
    // Append request body data
    auto& req = self->pending_requests_[stream_id];
    req.body.insert(req.body.end(), data, data + len);
    
    return 0;
}

// TCP frame processing methods

void HTTP2Session::set_tcp_forwarder(std::shared_ptr<TCPForwarder> forwarder) {
    tcp_forwarder_ = forwarder;
    
    // Set callback for sending TCP frames back to server
    if (tcp_forwarder_) {
        tcp_forwarder_->set_send_callback([this](const std::vector<uint8_t>& frame_data) {
            send_tcp_frame(frame_data);
        });
    }
}

void HTTP2Session::send_tcp_frame(const std::vector<uint8_t>& frame_data) {
    if (!active_) {
        Logger::error("Cannot send TCP frame: session not active");
        return;
    }
    
    boost::system::error_code ec;
    size_t written = http_client_.ssl_socket().write_some(boost::asio::buffer(frame_data), ec);
    
    if (ec) {
        Logger::error("Failed to send TCP frame", {
            {"error", ec.message()}
        });
        return;
    }
    
    Logger::debug("Sent TCP frame", {
        {"bytes", std::to_string(written)}
    });
}

bool HTTP2Session::is_tcp_frame(const uint8_t* data, size_t length) {
    if (length < 1) {
        return false;
    }
    
    // TCP frames have type 0x10-0x14
    uint8_t frame_type = data[0];
    return (frame_type >= 0x10 && frame_type <= 0x14);
}

size_t HTTP2Session::process_tcp_frames(const uint8_t* data, size_t length) {
    size_t consumed = 0;
    
    while (consumed < length) {
        const uint8_t* current = data + consumed;
        size_t remaining = length - consumed;
        
        // Need at least 1 byte for frame type
        if (remaining < 1) {
            break;
        }
        
        uint8_t frame_type = current[0];
        
        // Check if this is a TCP frame
        if (frame_type < 0x10 || frame_type > 0x14) {
            // Not a TCP frame, stop processing
            break;
        }
        
        // Determine frame size based on type
        size_t frame_size = 0;
        switch (static_cast<TCPFrameType>(frame_type)) {
            case TCPFrameType::TCP_OPEN:
                // type(1) + conn_id(16) + target_port(2) = 19 bytes
                frame_size = 19;
                break;
            case TCPFrameType::TCP_DATA:
                // type(1) + conn_id(16) + seq(4) + data_len(4) + data
                if (remaining >= 25) {
                    // Read data_len from bytes 21-24 (big-endian/network byte order)
                    uint32_t data_len = (static_cast<uint32_t>(current[21]) << 24) |
                                       (static_cast<uint32_t>(current[22]) << 16) |
                                       (static_cast<uint32_t>(current[23]) << 8) |
                                       static_cast<uint32_t>(current[24]);
                    
                    // Log the raw bytes for debugging
                    Logger::info("TCP_DATA frame header bytes", {
                        {"byte_21", std::to_string(current[21])},
                        {"byte_22", std::to_string(current[22])},
                        {"byte_23", std::to_string(current[23])},
                        {"byte_24", std::to_string(current[24])},
                        {"calculated_data_len", std::to_string(data_len)},
                        {"remaining_bytes", std::to_string(remaining)}
                    });
                    
                    // Sanity check: data_len should be reasonable (< 64KB for single frame)
                    if (data_len > 65536) {
                        Logger::error("Invalid TCP_DATA frame: data_len too large", {
                            {"data_len", std::to_string(data_len)},
                            {"frame_type", std::to_string(frame_type)}
                        });
                        // This might not be a TCP frame, stop processing
                        break;
                    }
                    
                    frame_size = 25 + data_len;
                    
                    Logger::debug("TCP_DATA frame size calculated", {
                        {"data_len", std::to_string(data_len)},
                        {"total_frame_size", std::to_string(frame_size)},
                        {"available", std::to_string(remaining)}
                    });
                }
                break;
            case TCPFrameType::TCP_CLOSE:
                // type(1) + conn_id(16) + reason(2) = 19 bytes
                frame_size = 19;
                break;
            case TCPFrameType::TCP_ERROR:
                // type(1) + conn_id(16) + error_code(2) + msg_len(2) + message
                if (remaining >= 21) {
                    uint16_t msg_len = (static_cast<uint16_t>(current[19]) << 8) |
                                      static_cast<uint16_t>(current[20]);
                    frame_size = 21 + msg_len;
                }
                break;
            case TCPFrameType::TCP_ACK:
                // type(1) + conn_id(16) + ack_seq(4) + window_size(4) = 25 bytes
                frame_size = 25;
                break;
        }
        
        // Check if we have the complete frame
        if (frame_size == 0 || remaining < frame_size) {
            // Incomplete frame, buffer it for next read
            Logger::info("Incomplete TCP frame", {
                {"frame_type", std::to_string(frame_type)},
                {"expected_size", std::to_string(frame_size)},
                {"available", std::to_string(remaining)}
            });
            break;
        }
        
        // Process complete frame
        handle_tcp_frame(current, frame_size);
        consumed += frame_size;
    }
    
    return consumed;
}

void HTTP2Session::handle_tcp_frame(const uint8_t* frame_data, size_t frame_length) {
    if (frame_length < 1) {
        return;
    }
    
    uint8_t frame_type = frame_data[0];
    Logger::info("Handling TCP frame", {
        {"type", std::to_string(frame_type)},
        {"length", std::to_string(frame_length)}
    });
    
    if (!tcp_forwarder_) {
        Logger::warning("TCP frame dropped", {
            {"reason", "no forwarder configured"}
        });
        return;
    }
    
    // Extract connection ID (bytes 1-16)
    if (frame_length < 17) {
        Logger::error("TCP frame too short for connection ID");
        return;
    }
    
    std::array<uint8_t, 16> connection_id;
    std::copy(frame_data + 1, frame_data + 17, connection_id.begin());
    
    switch (static_cast<TCPFrameType>(frame_type)) {
        case TCPFrameType::TCP_OPEN:
            if (frame_length >= 19) {
                uint16_t target_port = (static_cast<uint16_t>(frame_data[17]) << 8) |
                                      static_cast<uint16_t>(frame_data[18]);
                tcp_forwarder_->handle_tcp_open(connection_id, target_port);
            }
            break;
            
        case TCPFrameType::TCP_DATA:
            if (frame_length >= 25) {
                uint32_t data_len = (static_cast<uint32_t>(frame_data[21]) << 24) |
                                   (static_cast<uint32_t>(frame_data[22]) << 16) |
                                   (static_cast<uint32_t>(frame_data[23]) << 8) |
                                   static_cast<uint32_t>(frame_data[24]);
                
                if (frame_length >= 25 + data_len) {
                    std::vector<uint8_t> data(frame_data + 25, frame_data + 25 + data_len);
                    tcp_forwarder_->handle_tcp_data(connection_id, data);
                }
            }
            break;
            
        case TCPFrameType::TCP_CLOSE:
            tcp_forwarder_->handle_tcp_close(connection_id);
            break;
            
        case TCPFrameType::TCP_ERROR:
            if (frame_length >= 21) {
                uint16_t error_code = (static_cast<uint16_t>(frame_data[17]) << 8) |
                                     static_cast<uint16_t>(frame_data[18]);
                uint16_t msg_len = (static_cast<uint16_t>(frame_data[19]) << 8) |
                                  static_cast<uint16_t>(frame_data[20]);
                
                std::string message;
                if (frame_length >= 21 + msg_len) {
                    message = std::string(reinterpret_cast<const char*>(frame_data + 21), msg_len);
                }
                
                tcp_forwarder_->handle_tcp_error(connection_id, error_code, message);
            }
            break;
            
        case TCPFrameType::TCP_ACK:
            // ACK frames are informational, not currently used
            Logger::debug("Received TCP_ACK frame");
            break;
    }
}

}  // namespace agent
}  // namespace protogate
