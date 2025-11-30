#include "tcp_forwarder.h"
#include <iomanip>
#include <sstream>

namespace protogate {
namespace agent {

// TCP frame types (must match server)
enum class TCPFrameType : uint8_t {
    TCP_OPEN = 0x10,
    TCP_DATA = 0x11,
    TCP_CLOSE = 0x12,
    TCP_ERROR = 0x13,
    TCP_ACK = 0x14
};

TCPForwarder::TCPForwarder(boost::asio::io_context& io_context,
                           const std::string& target_host,
                           uint16_t target_port)
    : io_context_(io_context),
      target_host_(target_host),
      target_port_(target_port) {
    Logger::info("TCPForwarder initialized", {
        {"target_host", target_host_},
        {"target_port", std::to_string(target_port_)}
    });
}

TCPForwarder::~TCPForwarder() {
    // Close all active connections
    for (auto& [conn_id, conn] : connections_) {
        if (conn && conn->local_socket && conn->local_socket->is_open()) {
            boost::system::error_code ec;
            conn->local_socket->close(ec);
        }
    }
    connections_.clear();
}

void TCPForwarder::set_send_callback(TCPFrameSendCallback callback) {
    send_callback_ = callback;
}

void TCPForwarder::handle_tcp_open(const std::array<uint8_t, 16>& connection_id, uint16_t target_port) {
    std::string conn_id_str = connection_id_to_string(connection_id);
    Logger::info("Opening TCP connection", {
        {"connection_id", conn_id_str},
        {"target_port", std::to_string(target_port)}
    });
    
    // Create new connection object
    auto conn = std::make_shared<TCPConnection>();
    conn->connection_id = connection_id;
    conn->local_socket = std::make_unique<tcp::socket>(io_context_);
    conn->sequence_number = 0;
    conn->closing = false;
    
    // Store connection immediately (before async operations)
    connections_[connection_id] = conn;
    Logger::info("Connection stored in map", {
        {"connection_id", conn_id_str},
        {"connections_count", std::to_string(connections_.size())}
    });
    
    // Use target port from frame if non-zero, otherwise default
    uint16_t port = (target_port > 0) ? target_port : target_port_;
    
    Logger::info("Resolving target endpoint", {
        {"connection_id", conn_id_str},
        {"target_host", target_host_},
        {"target_port", std::to_string(port)}
    });
    
    // Resolve and connect asynchronously
    auto resolver = std::make_shared<tcp::resolver>(io_context_);
    auto self = shared_from_this();
    
    // Keep conn alive during async operations by capturing in lambda
    resolver->async_resolve(target_host_, std::to_string(port),
        [this, self, resolver, connection_id, conn_id_str, conn](
            const boost::system::error_code& ec,
            const tcp::resolver::results_type& endpoints) {
            
            if (ec) {
                Logger::error("Failed to resolve", {
                    {"connection_id", conn_id_str},
                    {"error", ec.message()}
                });
                send_tcp_close_frame(connection_id, 1);
                return;
            }
            
            Logger::info("Resolved, connecting", {
                {"connection_id", conn_id_str},
                {"endpoints", std::to_string(endpoints.size())}
            });
            
            boost::asio::async_connect(*conn->local_socket, endpoints,
                [this, self, connection_id, conn_id_str, conn](
                    const boost::system::error_code& ec,
                    const tcp::endpoint& endpoint) {
                    
                    if (ec) {
                        Logger::error("Failed to connect", {
                            {"connection_id", conn_id_str},
                            {"error", ec.message()}
                        });
                        send_tcp_close_frame(connection_id, 1);
                        return;
                    }
                    
                    Logger::info("Connected successfully", {
                        {"connection_id", conn_id_str},
                        {"endpoint", endpoint.address().to_string() + ":" + std::to_string(endpoint.port())}
                    });
                    
                    // Mark as connected
                    conn->connected = true;
                    
                    // Flush any pending data received before connection completed
                    if (!conn->pending_data.empty()) {
                        Logger::info("Flushing pending data", {
                            {"connection_id", conn_id_str},
                            {"queue_size", std::to_string(conn->pending_data.size())}
                        });
                        
                        for (auto& data : conn->pending_data) {
                            boost::asio::async_write(*conn->local_socket, boost::asio::buffer(data),
                                [this, connection_id, conn_id_str](const boost::system::error_code& ec, std::size_t bytes_written) {
                                    if (ec) {
                                        Logger::error("Failed to write pending data", {
                                            {"connection_id", conn_id_str},
                                            {"error", ec.message()}
                                        });
                                    } else {
                                        Logger::info("Wrote pending data", {
                                            {"connection_id", conn_id_str},
                                            {"bytes", std::to_string(bytes_written)}
                                        });
                                    }
                                });
                        }
                        conn->pending_data.clear();
                    }
                    
                    // Connection already stored in map, just start reading
                    start_local_read(conn);
                });
        });
}

void TCPForwarder::handle_tcp_data(const std::array<uint8_t, 16>& connection_id, 
                                   const std::vector<uint8_t>& data) {
    std::string conn_id_str = connection_id_to_string(connection_id);
    Logger::info("handle_tcp_data called", {
        {"connection_id", conn_id_str},
        {"data_size", std::to_string(data.size())}
    });
    
    auto it = connections_.find(connection_id);
    if (it == connections_.end()) {
        Logger::warning("Received data for unknown connection", {
            {"connection_id", conn_id_str},
            {"connections_count", std::to_string(connections_.size())}
        });
        
        // Log all connection IDs in the map
        for (const auto& [key, conn] : connections_) {
            Logger::info("Connection in map", {
                {"connection_id", connection_id_to_string(key)}
            });
        }
        return;
    }
    
    auto conn = it->second;
    if (!conn || !conn->local_socket) {
        Logger::warning("Connection or socket invalid", {
            {"connection_id", conn_id_str}
        });
        return;
    }
    
    // If not connected yet, queue the data
    if (!conn->connected) {
        Logger::info("Queueing data until connection ready", {
            {"connection_id", conn_id_str},
            {"data_size", std::to_string(data.size())}
        });
        conn->pending_data.push_back(data);
        return;
    }
    
    // Write data to local socket
    auto weak_conn = std::weak_ptr<TCPConnection>(conn);
    boost::asio::async_write(*conn->local_socket, boost::asio::buffer(data),
        [this, connection_id, conn_id_str, weak_conn](const boost::system::error_code& ec, std::size_t bytes_written) {
            auto conn = weak_conn.lock();
            if (!conn) return;
            
            if (ec) {
                Logger::error("Failed to write to local socket", {
                    {"connection_id", conn_id_str},
                    {"error", ec.message()}
                });
                close_connection(connection_id);
                return;
            }
            
            Logger::info("Wrote to local socket", {
                {"connection_id", conn_id_str},
                {"bytes", std::to_string(bytes_written)}
            });
        });
}

void TCPForwarder::handle_tcp_close(const std::array<uint8_t, 16>& connection_id) {
    Logger::info("Closing connection", {
        {"connection_id", connection_id_to_string(connection_id)}
    });
    close_connection(connection_id);
}

void TCPForwarder::handle_tcp_error(const std::array<uint8_t, 16>& connection_id,
                                    uint16_t error_code,
                                    const std::string& message) {
    Logger::error("Connection error", {
        {"connection_id", connection_id_to_string(connection_id)},
        {"error_code", std::to_string(error_code)},
        {"message", message}
    });
    close_connection(connection_id);
}

void TCPForwarder::start_local_read(std::shared_ptr<TCPConnection> conn) {
    if (!conn || !conn->local_socket || !conn->local_socket->is_open()) {
        return;
    }
    
    auto weak_conn = std::weak_ptr<TCPConnection>(conn);
    conn->local_socket->async_read_some(boost::asio::buffer(conn->read_buffer),
        [this, weak_conn](const boost::system::error_code& ec, std::size_t bytes_read) {
            auto conn = weak_conn.lock();
            if (!conn) return;
            
            if (ec) {
                if (ec == boost::asio::error::eof) {
                    Logger::info("Local connection closed", {
                        {"reason", "EOF"}
                    });
                } else {
                    Logger::error("Local read error", {
                        {"error", ec.message()}
                    });
                }
                send_tcp_close_frame(conn->connection_id, 0);  // 0 = normal close
                close_connection(conn->connection_id);
                return;
            }
            
            // Send data to server
            std::vector<uint8_t> data(conn->read_buffer.begin(), conn->read_buffer.begin() + bytes_read);
            send_tcp_data_frame(conn->connection_id, data, conn->sequence_number++);
            
            // Continue reading
            start_local_read(conn);
        });
}

void TCPForwarder::close_connection(const std::array<uint8_t, 16>& connection_id) {
    auto it = connections_.find(connection_id);
    if (it == connections_.end()) {
        return;
    }
    
    auto conn = it->second;
    if (conn && conn->local_socket && conn->local_socket->is_open()) {
        boost::system::error_code ec;
        conn->local_socket->shutdown(tcp::socket::shutdown_both, ec);
        conn->local_socket->close(ec);
    }
    
    connections_.erase(it);
    Logger::info("Connection closed and removed", {
        {"connection_id", connection_id_to_string(connection_id)}
    });
}

void TCPForwarder::send_tcp_data_frame(const std::array<uint8_t, 16>& connection_id,
                                       const std::vector<uint8_t>& data,
                                       uint32_t sequence_number) {
    if (!send_callback_) {
        Logger::error("Cannot send TCP_DATA", {
            {"reason", "no callback set"}
        });
        return;
    }
    
    // Build TCP_DATA frame: type(1) + conn_id(16) + seq(4) + data_len(4) + data
    std::vector<uint8_t> frame;
    frame.reserve(1 + 16 + 4 + 4 + data.size());
    
    // Type
    frame.push_back(static_cast<uint8_t>(TCPFrameType::TCP_DATA));
    
    // Connection ID
    frame.insert(frame.end(), connection_id.begin(), connection_id.end());
    
    // Sequence number (network byte order)
    uint32_t seq_net = htonl(sequence_number);
    const uint8_t* seq_bytes = reinterpret_cast<const uint8_t*>(&seq_net);
    frame.insert(frame.end(), seq_bytes, seq_bytes + 4);
    
    // Data length (network byte order)
    uint32_t len_net = htonl(static_cast<uint32_t>(data.size()));
    const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&len_net);
    frame.insert(frame.end(), len_bytes, len_bytes + 4);
    
    // Data payload
    frame.insert(frame.end(), data.begin(), data.end());
    
    Logger::debug("Sending TCP_DATA frame", {
        {"bytes", std::to_string(data.size())},
        {"sequence_number", std::to_string(sequence_number)}
    });
    send_callback_(frame);
}

void TCPForwarder::send_tcp_close_frame(const std::array<uint8_t, 16>& connection_id,
                                        uint16_t reason_code) {
    if (!send_callback_) {
        Logger::error("Cannot send TCP_CLOSE", {
            {"reason", "no callback set"}
        });
        return;
    }
    
    // Build TCP_CLOSE frame: type(1) + conn_id(16) + reason(2)
    std::vector<uint8_t> frame;
    frame.reserve(1 + 16 + 2);
    
    // Type
    frame.push_back(static_cast<uint8_t>(TCPFrameType::TCP_CLOSE));
    
    // Connection ID
    frame.insert(frame.end(), connection_id.begin(), connection_id.end());
    
    // Reason code (network byte order)
    uint16_t reason_net = htons(reason_code);
    const uint8_t* reason_bytes = reinterpret_cast<const uint8_t*>(&reason_net);
    frame.insert(frame.end(), reason_bytes, reason_bytes + 2);
    
    Logger::info("Sending TCP_CLOSE frame", {
        {"reason_code", std::to_string(reason_code)}
    });
    send_callback_(frame);
}

std::string TCPForwarder::connection_id_to_string(const std::array<uint8_t, 16>& id) const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(id[i]);
    }
    return oss.str();
}

}  // namespace agent
}  // namespace protogate
