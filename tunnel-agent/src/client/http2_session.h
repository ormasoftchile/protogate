#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include <nghttp2/nghttp2.h>
#include "tls_client.h"

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

class HTTP2Session {
public:
    HTTP2Session(TLSClient& tls_client, const std::string& tunnel_id, const std::string& token);
    ~HTTP2Session();
    
    void start(RequestCallback on_request);
    void stop();
    bool is_active() const;
    
    void send_response(int32_t stream_id, int status_code, 
                      const std::map<std::string, std::string>& headers,
                      const std::vector<uint8_t>& body);
    
    void send_ping();
    void process_events();
    
private:
    TLSClient& tls_client_;
    std::string tunnel_id_;
    std::string token_;
    
    nghttp2_session* session_;
    bool active_;
    RequestCallback on_request_;
    
    std::map<int32_t, HTTP2Request> pending_requests_;
    
    void send_connect_request();
    void send_data();
    void receive_data();
    
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
