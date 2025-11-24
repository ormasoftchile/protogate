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

HTTP2Session::HTTP2Session(TLSClient& tls_client, const std::string& tunnel_id, const std::string& token)
    : tls_client_(tls_client), tunnel_id_(tunnel_id), token_(token), session_(nullptr), active_(false) {
}

HTTP2Session::~HTTP2Session() {
    stop();
}

void HTTP2Session::start(RequestCallback on_request) {
    on_request_ = on_request;
    
    // Check if HTTP/2 was negotiated via ALPN
    std::string alpn_protocol = tls_client_.get_alpn_protocol();
    
    Logger::info("Starting session", {
        {"alpn_protocol", alpn_protocol.empty() ? "none" : alpn_protocol}
    });
    
    // Server MVP doesn't support ALPN yet, so we use HTTP/1.1 auth for compatibility
    // TODO: Remove this fallback once server implements proper HTTP/2 with ALPN
    if (alpn_protocol != "h2") {
        Logger::info("Using HTTP/1.1 authentication (server MVP compatibility mode)");
        
        // Send HTTP/1.1 authentication headers (temporary for server MVP)
        std::ostringstream auth_request;
        auth_request << "CONNECT tunnel-agent HTTP/1.1\r\n"
                     << "Host: tunnel-agent\r\n"
                     << "Authorization: Bearer " << token_ << "\r\n"
                     << "X-Tunnel-ID: " << tunnel_id_ << "\r\n"
                     << "X-Agent-Version: 1.0.0\r\n"
                     << "\r\n";
        
        std::string auth_str = auth_request.str();
        boost::system::error_code ec;
        boost::asio::write(tls_client_.socket(), boost::asio::buffer(auth_str), ec);
        
        if (ec) {
            throw std::runtime_error("Failed to send authentication: " + ec.message());
        }
        
        Logger::info("Sent HTTP/1.1 authentication");
        
        // Read authentication response
        char response_buffer[1024];
        size_t bytes_read = tls_client_.socket().read_some(boost::asio::buffer(response_buffer), ec);
        
        if (ec && ec != boost::asio::error::eof) {
            throw std::runtime_error("Failed to read auth response: " + ec.message());
        }
        
        std::string response(response_buffer, bytes_read);
        Logger::info("Received auth response", {
            {"response", response.substr(0, std::min<size_t>(100, response.size()))}
        });
        
        // Check if authentication succeeded
        if (response.find("200") == std::string::npos && response.find("Connection Established") == std::string::npos) {
            throw std::runtime_error("Authentication failed: " + response);
        }
    }
    // When server supports ALPN and h2, use proper HTTP/2 CONNECT:
    // else {
    //     Logger::info("Using HTTP/2 with ALPN");
    //     // HTTP/2 connection preface already sent by nghttp2
    //     // Authentication will be in the CONNECT request headers
    // }
    
    // Initialize nghttp2 session
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    
    nghttp2_session_client_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);
    
    // Send HTTP/2 connection preface
    nghttp2_settings_entry iv[1] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
    };
    nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv, 1);
    
    // Send pending frames
    send_data();
    
    active_ = true;
    
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
    
    Logger::info("HTTP/2 session stopped");
}

bool HTTP2Session::is_active() const {
    return active_;
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
    // Build response headers
    std::vector<nghttp2_nv> hdrs;
    
    std::string status_str = std::to_string(status_code);
    const char* status_name = ":status";
    hdrs.push_back({(uint8_t*)status_name, (uint8_t*)status_str.c_str(), 
                   strlen(status_name), status_str.size(), NGHTTP2_NV_FLAG_NONE});
    
    for (const auto& [key, value] : headers) {
        hdrs.push_back({(uint8_t*)key.c_str(), (uint8_t*)value.c_str(), 
                       key.size(), value.size(), NGHTTP2_NV_FLAG_NONE});
    }
    
    // Create data provider for response body
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = (void*)&body;
    data_prd.read_callback = [](nghttp2_session* session, int32_t stream_id,
                                uint8_t* buf, size_t length, uint32_t* data_flags,
                                nghttp2_data_source* source, void* user_data) -> ssize_t {
        auto* body_ptr = (const std::vector<uint8_t>*)source->ptr;
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
    receive_data();
    send_data();
}

void HTTP2Session::send_data() {
    int rv = nghttp2_session_send(session_);
    if (rv != 0) {
        Logger::error("nghttp2_session_send failed", {
            {"error", nghttp2_strerror(rv)}
        });
    }
}

void HTTP2Session::receive_data() {
    try {
        uint8_t buffer[8192];
        
        boost::system::error_code ec;
        size_t len = tls_client_.socket().read_some(boost::asio::buffer(buffer), ec);
        
        if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
            return;
        }
        
        if (ec) {
            Logger::error("Socket read error", {
                {"error", ec.message()}
            });
            active_ = false;
            return;
        }
        
        if (len > 0) {
            ssize_t readlen = nghttp2_session_mem_recv(session_, buffer, len);
            if (readlen < 0) {
                Logger::error("nghttp2_session_mem_recv failed", {
                    {"error", nghttp2_strerror(readlen)}
                });
                active_ = false;
            }
        }
        
    } catch (const std::exception& e) {
        Logger::error("Exception in receive_data", {
            {"error", e.what()}
        });
        active_ = false;
    }
}

ssize_t HTTP2Session::send_callback(nghttp2_session* session, const uint8_t* data,
                                   size_t length, int flags, void* user_data) {
    auto* self = static_cast<HTTP2Session*>(user_data);
    
    try {
        boost::system::error_code ec;
        size_t written = boost::asio::write(self->tls_client_.socket(), 
                                           boost::asio::buffer(data, length), ec);
        
        if (ec) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        
        return written;
        
    } catch (...) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
}

int HTTP2Session::on_frame_recv_callback(nghttp2_session* session,
                                        const nghttp2_frame* frame, void* user_data) {
    auto* self = static_cast<HTTP2Session*>(user_data);
    
    switch (frame->hd.type) {
    case NGHTTP2_HEADERS:
        if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
            // Check if this is the CONNECT response
            if (frame->hd.stream_id == 1) {
                Logger::info("Received CONNECT response");
            } else {
                // This is a new request from server
                auto it = self->pending_requests_.find(frame->hd.stream_id);
                if (it != self->pending_requests_.end()) {
                    Logger::debug("Request headers complete", {
                        {"stream_id", std::to_string(frame->hd.stream_id)}
                    });
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
        Logger::debug("Received PING ACK");
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
    
    if (frame->hd.stream_id == 1) {
        // CONNECT response headers
        return 0;
    }
    
    // Store request headers
    auto& req = self->pending_requests_[frame->hd.stream_id];
    req.stream_id = frame->hd.stream_id;
    
    std::string name_str((char*)name, namelen);
    std::string value_str((char*)value, valuelen);
    
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

}  // namespace agent
}  // namespace protogate
