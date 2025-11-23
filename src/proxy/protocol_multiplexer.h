#pragma once

#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

// Forward declaration for nghttp2 (will be integrated in production)
struct nghttp2_session;

namespace protogate {
namespace proxy {

/**
 * @brief HTTP/2 protocol multiplexer using nghttp2
 * 
 * Responsibilities:
 * - Manage HTTP/2 sessions for agent connections
 * - Multiplex multiple HTTP requests over single TCP connection
 * - Handle HTTP/2 frames (HEADERS, DATA, PING, GOAWAY)
 * - Implement flow control and backpressure
 * - Support stream prioritization
 * 
 * Note: This is a wrapper around nghttp2 library for HTTP/2 support
 */
class ProtocolMultiplexer {
public:
    /**
     * @brief HTTP/2 stream callback
     */
    using stream_data_callback = std::function<void(int32_t stream_id, 
                                                    const std::string& data, 
                                                    bool end_stream)>;
    using stream_header_callback = std::function<void(int32_t stream_id,
                                                     const std::string& name,
                                                     const std::string& value)>;

    /**
     * @brief HTTP/2 request/response representation
     */
    struct HTTP2Message {
        int32_t stream_id;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        bool end_stream;
    };

    /**
     * @brief Initialize protocol multiplexer
     */
    ProtocolMultiplexer();
    ~ProtocolMultiplexer();

    /**
     * @brief Initialize HTTP/2 session
     * @param is_server true for server mode, false for client mode
     * @return true if successful
     */
    bool initialize_session(bool is_server = true);

    /**
     * @brief Submit HTTP/2 request (client mode)
     * @param method HTTP method (GET, POST, etc.)
     * @param path Request path
     * @param headers Additional headers
     * @param body Request body (empty for GET)
     * @return Stream ID or -1 on error
     */
    int32_t submit_request(const std::string& method,
                          const std::string& path,
                          const std::unordered_map<std::string, std::string>& headers,
                          const std::string& body = "");

    /**
     * @brief Submit HTTP/2 response (server mode)
     * @param stream_id Stream ID from request
     * @param status_code HTTP status code (200, 404, etc.)
     * @param headers Response headers
     * @param body Response body
     * @return true if successful
     */
    bool submit_response(int32_t stream_id,
                        int status_code,
                        const std::unordered_map<std::string, std::string>& headers,
                        const std::string& body);

    /**
     * @brief Process incoming data from connection
     * @param data Received bytes
     * @return Number of bytes processed
     */
    ssize_t process_input(const std::string& data);

    /**
     * @brief Get pending output data to send
     * @return Data to write to connection
     */
    std::string get_output_data();

    /**
     * @brief Set callback for received stream headers
     */
    void set_header_callback(stream_header_callback callback);

    /**
     * @brief Set callback for received stream data
     */
    void set_data_callback(stream_data_callback callback);

    /**
     * @brief Send PING frame
     * @return true if successful
     */
    bool send_ping();

    /**
     * @brief Send GOAWAY frame (graceful shutdown)
     * @return true if successful
     */
    bool send_goaway();

    /**
     * @brief Check if session wants to send data
     */
    bool wants_write() const;

    /**
     * @brief Check if session wants to receive data
     */
    bool wants_read() const;

private:
    /**
     * @brief nghttp2 session callbacks (static wrappers)
     */
    static ssize_t send_callback(nghttp2_session* session,
                                 const uint8_t* data, size_t length,
                                 int flags, void* user_data);
    
    static int on_frame_recv_callback(nghttp2_session* session,
                                     const void* frame, void* user_data);
    
    static int on_data_chunk_recv_callback(nghttp2_session* session,
                                          uint8_t flags, int32_t stream_id,
                                          const uint8_t* data, size_t len,
                                          void* user_data);
    
    static int on_header_callback(nghttp2_session* session,
                                 const void* frame,
                                 const uint8_t* name, size_t namelen,
                                 const uint8_t* value, size_t valuelen,
                                 uint8_t flags, void* user_data);

    nghttp2_session* session_;  // TODO: Stub implementation, will be used when integrating nghttp2
    std::string output_buffer_;
    stream_header_callback header_callback_;
    stream_data_callback data_callback_;
    
    // Current stream being processed
    int32_t current_stream_id_;  // TODO: Will be used for stream tracking in full HTTP/2 implementation
    std::unordered_map<std::string, std::string> current_headers_;
};

}  // namespace proxy
}  // namespace protogate
