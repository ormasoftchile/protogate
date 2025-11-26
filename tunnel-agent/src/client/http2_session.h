#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include <nghttp2/nghttp2.h>
#include <boost/asio.hpp>
#include "tls_client.h"
#include "../forwarder/tcp_forwarder.h"

namespace protogate {
namespace agent {

struct HTTP2Request {
    int32_t stream_id;
    std::string method;
    std::string path;
    std::string authority;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    std::string request_id;
    std::string client_ip;
};

using RequestCallback = std::function<void(const HTTP2Request&)>;
using PingAckCallback = std::function<void()>;

// TCP frame types (must match server)
enum class TCPFrameType : uint8_t {
    TCP_OPEN = 0x10,
    TCP_DATA = 0x11,
    TCP_CLOSE = 0x12,
    TCP_ERROR = 0x13,
    TCP_ACK = 0x14
};

class HTTP2Session {
public:
    HTTP2Session(boost::asio::io_context& io_context, TLSClient& tls_client, 
                 const std::string& tunnel_id, const std::string& token);
    ~HTTP2Session();
    
    void start(RequestCallback on_request);
    void stop();
    bool is_active() const;
    
    void set_ping_ack_callback(PingAckCallback callback);
    
    void send_response(int32_t stream_id, int status_code, 
                      const std::map<std::string, std::string>& headers,
                      const std::vector<uint8_t>& body);
    
    void send_ping();
    void process_events();
    
    // TCP tunneling support
    void set_tcp_forwarder(std::shared_ptr<TCPForwarder> forwarder);
    void send_tcp_frame(const std::vector<uint8_t>& frame_data);
    
private:
    boost::asio::io_context& io_context_;
    TLSClient& tls_client_;
    std::string tunnel_id_;
    std::string token_;
    
    nghttp2_session* session_;
    bool active_;
    RequestCallback on_request_;
    PingAckCallback on_ping_ack_;
    
    // Async read buffer
    std::array<uint8_t, 8192> read_buffer_;
    
    // TCP frame buffering
    std::vector<uint8_t> tcp_frame_buffer_;
    std::shared_ptr<TCPForwarder> tcp_forwarder_;
    
    std::map<int32_t, HTTP2Request> pending_requests_;
    std::map<int32_t, std::vector<uint8_t>> response_bodies_;  // Keep response bodies alive
    
    // Keep header strings alive for nghttp2
    std::string auth_header_name_;
    std::string auth_header_value_;
    std::string tunnel_header_name_;
    std::string version_header_name_;
    
    void send_connect_request();
    void send_data();
    void start_async_read();
    void handle_read(const boost::system::error_code& ec, size_t bytes_transferred);
    
    // TCP frame processing
    bool is_tcp_frame(const uint8_t* data, size_t length);
    size_t process_tcp_frames(const uint8_t* data, size_t length);
    void handle_tcp_frame(const uint8_t* frame_data, size_t frame_length);
    
    static ssize_t send_callback(nghttp2_session* session, const uint8_t* data,
                                size_t length, int flags, void* user_data);
    static int on_frame_recv_callback(nghttp2_session* session,
                                     const nghttp2_frame* frame, void* user_data);
    static int on_header_callback(nghttp2_session* session,
                                 const nghttp2_frame* frame,
                                 const uint8_t* name, size_t namelen,
                                 const uint8_t* value, size_t valuelen,
                                 uint8_t flags, void* user_data);
    static int on_data_chunk_recv_callback(nghttp2_session* session,
                                          uint8_t flags, int32_t stream_id,
                                          const uint8_t* data, size_t len,
                                          void* user_data);
};

}  // namespace agent
}  // namespace protogate
