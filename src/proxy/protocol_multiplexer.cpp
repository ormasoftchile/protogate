#include "protocol_multiplexer.h"
#include "../observability/logger.h"

// Stub implementation for nghttp2 integration
// TODO: Add nghttp2 library and implement full HTTP/2 protocol

// Suppress unused private field warnings for stub implementation
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

namespace protogate {
namespace proxy {

ProtocolMultiplexer::ProtocolMultiplexer()
    : session_(nullptr), current_stream_id_(-1) {
    
    observability::Logger::instance().debug("ProtocolMultiplexer created");
}

ProtocolMultiplexer::~ProtocolMultiplexer() {
    // TODO: Clean up nghttp2 session
    // if (session_) {
    //     nghttp2_session_del(session_);
    // }
    
    observability::Logger::instance().debug("ProtocolMultiplexer destroyed");
}

bool ProtocolMultiplexer::initialize_session(bool is_server) {
    // TODO: Initialize nghttp2 session
    // nghttp2_session_callbacks* callbacks;
    // nghttp2_session_callbacks_new(&callbacks);
    // nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    // nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    // nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    // nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    //
    // if (is_server) {
    //     nghttp2_session_server_new(&session_, callbacks, this);
    // } else {
    //     nghttp2_session_client_new(&session_, callbacks, this);
    // }
    //
    // nghttp2_session_callbacks_del(callbacks);
    
    observability::Logger::instance().info("HTTP/2 session initialized (stub)", {
        {"mode", is_server ? "server" : "client"}
    });
    
    return true;
}

int32_t ProtocolMultiplexer::submit_request(
    const std::string& method,
    const std::string& path,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body) {
    
    // TODO: Submit HTTP/2 request using nghttp2
    // Build header array:
    // nghttp2_nv nv[] = {
    //     {(uint8_t*)":method", (uint8_t*)method.c_str(), ...},
    //     {(uint8_t*)":path", (uint8_t*)path.c_str(), ...},
    //     ...
    // };
    // int32_t stream_id = nghttp2_submit_request(session_, nullptr, nv, nvlen, nullptr, nullptr);
    
    observability::Logger::instance().debug("HTTP/2 request submitted (stub)", {
        {"method", method},
        {"path", path}
    });
    
    return 1; // Stub: return dummy stream ID
}

bool ProtocolMultiplexer::submit_response(
    int32_t stream_id,
    int status_code,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body) {
    
    // TODO: Submit HTTP/2 response using nghttp2
    // nghttp2_submit_response(session_, stream_id, nv, nvlen, data_prd);
    
    observability::Logger::instance().debug("HTTP/2 response submitted (stub)", {
        {"stream_id", std::to_string(stream_id)},
        {"status", std::to_string(status_code)}
    });
    
    return true;
}

ssize_t ProtocolMultiplexer::process_input(const std::string& data) {
    // TODO: Feed data to nghttp2
    // return nghttp2_session_mem_recv(session_, 
    //     reinterpret_cast<const uint8_t*>(data.data()), data.size());
    
    observability::Logger::instance().debug("Processing HTTP/2 input (stub)", {
        {"bytes", std::to_string(data.size())}
    });
    
    return data.size();
}

std::string ProtocolMultiplexer::get_output_data() {
    // TODO: Get data from nghttp2 to send
    // const uint8_t* data;
    // ssize_t datalen = nghttp2_session_mem_send(session_, &data);
    // return std::string(reinterpret_cast<const char*>(data), datalen);
    
    std::string output = std::move(output_buffer_);
    output_buffer_.clear();
    return output;
}

void ProtocolMultiplexer::set_header_callback(stream_header_callback callback) {
    header_callback_ = std::move(callback);
}

void ProtocolMultiplexer::set_data_callback(stream_data_callback callback) {
    data_callback_ = std::move(callback);
}

bool ProtocolMultiplexer::send_ping() {
    // TODO: nghttp2_submit_ping(session_, ...);
    
    observability::Logger::instance().debug("PING frame sent (stub)");
    return true;
}

bool ProtocolMultiplexer::send_goaway() {
    // TODO: nghttp2_submit_goaway(session_, ...);
    
    observability::Logger::instance().debug("GOAWAY frame sent (stub)");
    return true;
}

bool ProtocolMultiplexer::wants_write() const {
    // TODO: return nghttp2_session_want_write(session_);
    return !output_buffer_.empty();
}

bool ProtocolMultiplexer::wants_read() const {
    // TODO: return nghttp2_session_want_read(session_);
    return true;
}

// Static callback implementations (will be used by nghttp2)

ssize_t ProtocolMultiplexer::send_callback(
    nghttp2_session* session,
    const uint8_t* data, size_t length,
    int flags, void* user_data) {
    
    auto* self = static_cast<ProtocolMultiplexer*>(user_data);
    self->output_buffer_.append(reinterpret_cast<const char*>(data), length);
    return length;
}

int ProtocolMultiplexer::on_frame_recv_callback(
    nghttp2_session* session,
    const void* frame, void* user_data) {
    
    // TODO: Handle received frames
    return 0;
}

int ProtocolMultiplexer::on_data_chunk_recv_callback(
    nghttp2_session* session,
    uint8_t flags, int32_t stream_id,
    const uint8_t* data, size_t len,
    void* user_data) {
    
    auto* self = static_cast<ProtocolMultiplexer*>(user_data);
    
    if (self->data_callback_) {
        std::string chunk(reinterpret_cast<const char*>(data), len);
        bool end_stream = (flags & 0x01); // END_STREAM flag
        self->data_callback_(stream_id, chunk, end_stream);
    }
    
    return 0;
}

int ProtocolMultiplexer::on_header_callback(
    nghttp2_session* session,
    const void* frame,
    const uint8_t* name, size_t namelen,
    const uint8_t* value, size_t valuelen,
    uint8_t flags, void* user_data) {
    
    auto* self = static_cast<ProtocolMultiplexer*>(user_data);
    
    if (self->header_callback_) {
        std::string name_str(reinterpret_cast<const char*>(name), namelen);
        std::string value_str(reinterpret_cast<const char*>(value), valuelen);
        
        // Extract stream_id from frame (cast to appropriate type)
        int32_t stream_id = 0; // TODO: Get from frame
        
        self->header_callback_(stream_id, name_str, value_str);
    }
    
    return 0;
}

// ===== TCP Tunnel Protocol Implementation =====

std::vector<uint8_t> ProtocolMultiplexer::TCPFrame::serialize() const {
    std::vector<uint8_t> result;
    
    // Frame type (1 byte)
    result.push_back(static_cast<uint8_t>(type));
    
    // Connection ID length (2 bytes, big-endian)
    uint16_t conn_id_len = static_cast<uint16_t>(connection_id.size());
    result.push_back((conn_id_len >> 8) & 0xFF);
    result.push_back(conn_id_len & 0xFF);
    
    // Connection ID
    result.insert(result.end(), connection_id.begin(), connection_id.end());
    
    // Payload length (4 bytes, big-endian)
    uint32_t payload_len = static_cast<uint32_t>(payload.size());
    result.push_back((payload_len >> 24) & 0xFF);
    result.push_back((payload_len >> 16) & 0xFF);
    result.push_back((payload_len >> 8) & 0xFF);
    result.push_back(payload_len & 0xFF);
    
    // Payload
    result.insert(result.end(), payload.begin(), payload.end());
    
    return result;
}

ProtocolMultiplexer::TCPFrame ProtocolMultiplexer::TCPFrame::deserialize(
    const uint8_t* data, size_t length) {
    
    TCPFrame frame;
    
    if (length < 7) {  // Minimum frame size: type(1) + conn_id_len(2) + payload_len(4)
        throw std::runtime_error("Invalid TCP frame: too short");
    }
    
    size_t offset = 0;
    
    // Parse type
    frame.type = static_cast<TCPFrameType>(data[offset++]);
    
    // Parse connection ID length
    uint16_t conn_id_len = (static_cast<uint16_t>(data[offset]) << 8) |
                           static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    
    if (offset + conn_id_len + 4 > length) {
        throw std::runtime_error("Invalid TCP frame: truncated connection ID");
    }
    
    // Parse connection ID
    frame.connection_id = std::string(
        reinterpret_cast<const char*>(data + offset),
        conn_id_len);
    offset += conn_id_len;
    
    // Parse payload length
    uint32_t payload_len = (static_cast<uint32_t>(data[offset]) << 24) |
                          (static_cast<uint32_t>(data[offset + 1]) << 16) |
                          (static_cast<uint32_t>(data[offset + 2]) << 8) |
                          static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    
    if (offset + payload_len > length) {
        throw std::runtime_error("Invalid TCP frame: truncated payload");
    }
    
    // Parse payload
    frame.payload.assign(data + offset, data + offset + payload_len);
    
    return frame;
}

bool ProtocolMultiplexer::send_tcp_data(
    const std::string& connection_id,
    const uint8_t* data,
    size_t length) {
    
    TCPFrame frame;
    frame.type = TCPFrameType::TCP_DATA;
    frame.connection_id = connection_id;
    frame.payload.assign(data, data + length);
    
    auto serialized = frame.serialize();
    output_buffer_.append(
        reinterpret_cast<const char*>(serialized.data()),
        serialized.size());
    
    observability::Logger::instance().debug("TCP_DATA frame queued", {
        {"connection_id", connection_id},
        {"bytes", std::to_string(length)}
    });
    
    return true;
}

bool ProtocolMultiplexer::send_tcp_close(const std::string& connection_id) {
    TCPFrame frame;
    frame.type = TCPFrameType::TCP_CLOSE;
    frame.connection_id = connection_id;
    
    auto serialized = frame.serialize();
    output_buffer_.append(
        reinterpret_cast<const char*>(serialized.data()),
        serialized.size());
    
    observability::Logger::instance().debug("TCP_CLOSE frame queued", {
        {"connection_id", connection_id}
    });
    
    return true;
}

bool ProtocolMultiplexer::send_tcp_error(
    const std::string& connection_id,
    const std::string& error_message) {
    
    TCPFrame frame;
    frame.type = TCPFrameType::TCP_ERROR;
    frame.connection_id = connection_id;
    frame.payload.assign(error_message.begin(), error_message.end());
    
    auto serialized = frame.serialize();
    output_buffer_.append(
        reinterpret_cast<const char*>(serialized.data()),
        serialized.size());
    
    observability::Logger::instance().debug("TCP_ERROR frame queued", {
        {"connection_id", connection_id},
        {"error", error_message}
    });
    
    return true;
}

bool ProtocolMultiplexer::send_tcp_window(
    const std::string& connection_id,
    uint32_t window_size) {
    
    TCPFrame frame;
    frame.type = TCPFrameType::TCP_WINDOW;
    frame.connection_id = connection_id;
    
    // Encode window size as 4 bytes
    frame.payload.resize(4);
    frame.payload[0] = (window_size >> 24) & 0xFF;
    frame.payload[1] = (window_size >> 16) & 0xFF;
    frame.payload[2] = (window_size >> 8) & 0xFF;
    frame.payload[3] = window_size & 0xFF;
    
    auto serialized = frame.serialize();
    output_buffer_.append(
        reinterpret_cast<const char*>(serialized.data()),
        serialized.size());
    
    observability::Logger::instance().debug("TCP_WINDOW frame queued", {
        {"connection_id", connection_id},
        {"window_size", std::to_string(window_size)}
    });
    
    return true;
}

void ProtocolMultiplexer::set_tcp_frame_callback(tcp_frame_callback callback) {
    tcp_frame_callback_ = std::move(callback);
}

}  // namespace proxy
}  // namespace protogate

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
