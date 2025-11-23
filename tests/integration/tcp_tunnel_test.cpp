#include <gtest/gtest.h>
#include "../../src/proxy/tcp_proxy.h"
#include "../../src/proxy/protocol_multiplexer.h"
#include "../../src/agent/agent_registry.h"
#include "../../src/storage/cache.h"
#include "../../src/models/tunnel.h"
#include <boost/asio.hpp>
#include <thread>
#include <chrono>
#include <random>
#include <vector>
#include <cstring>

using namespace protogate;
using boost::asio::ip::tcp;

class TCPTunnelTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_context_ = std::make_shared<boost::asio::io_context>();
        tunnel_cache_ = std::make_shared<storage::Cache<std::string, models::Tunnel>>();
        agent_registry_ = std::make_shared<agent::AgentRegistry>();
        tcp_proxy_ = std::make_shared<proxy::TCPProxy>(tunnel_cache_, agent_registry_);
        
        // Create test tunnel
        test_tunnel_.tunnel_id = "test-tunnel-001";
        test_tunnel_.protocol = models::TunnelProtocol::TCP;
        test_tunnel_.target_port = 9100;
        test_tunnel_.target_host = "localhost";
        test_tunnel_.status = models::TunnelStatus::ACTIVE;
        
        tunnel_cache_->put(test_tunnel_.tunnel_id, test_tunnel_);
        
        // Note: register_agent requires an AgentConnection instance
        // For this test, we'll need to create a mock agent or skip agent registration
        // agent_registry_->register_agent(test_tunnel_.tunnel_id, mock_agent_ptr);
    }
    
    void TearDown() override {
        io_context_->stop();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }
    
    void run_io_context() {
        io_thread_ = std::thread([this]() {
            io_context_->run();
        });
    }
    
    std::shared_ptr<boost::asio::io_context> io_context_;
    std::shared_ptr<storage::Cache<std::string, models::Tunnel>> tunnel_cache_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    std::shared_ptr<proxy::TCPProxy> tcp_proxy_;
    models::Tunnel test_tunnel_;
    std::thread io_thread_;
};

/**
 * Test: Send 1MB data through TCP tunnel, verify byte-for-byte match
 * 
 * This test validates:
 * - Large data transfer (1MB) through TCP tunnel
 * - Byte-for-byte data integrity
 * - No data corruption during forwarding
 * - Proper buffer management for large payloads
 */
TEST_F(TCPTunnelTest, Send1MBDataVerifyIntegrity) {
    const size_t data_size = 1024 * 1024;  // 1 MB
    
    // Generate random test data
    std::vector<uint8_t> send_data(data_size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 0; i < data_size; ++i) {
        send_data[i] = static_cast<uint8_t>(dis(gen));
    }
    
    // Track received data
    std::vector<uint8_t> received_data;
    received_data.reserve(data_size);
    
    bool connection_established = false;
    bool test_completed = false;
    std::string connection_id;
    
    // Create client socket
    auto client_socket = std::make_shared<tcp::socket>(*io_context_);
    
    // Create server socket for receiving
    tcp::acceptor server_acceptor(*io_context_, tcp::endpoint(tcp::v4(), 0));
    auto server_socket = std::make_shared<tcp::socket>(*io_context_);
    
    // Accept connection on server side
    server_acceptor.async_accept(*server_socket,
        [&](const boost::system::error_code& ec) {
            ASSERT_FALSE(ec) << "Server accept failed: " << ec.message();
            connection_established = true;
            
            // Read data from server socket
            auto read_buffer = std::make_shared<std::vector<uint8_t>>(8192);
            std::function<void()> do_read = [&, read_buffer]() {
                server_socket->async_read_some(
                    boost::asio::buffer(*read_buffer),
                    [&, read_buffer, do_read](const boost::system::error_code& ec, size_t bytes_read) {
                        if (!ec) {
                            received_data.insert(received_data.end(),
                                               read_buffer->begin(),
                                               read_buffer->begin() + bytes_read);
                            
                            if (received_data.size() < data_size) {
                                do_read();  // Continue reading
                            } else {
                                test_completed = true;
                            }
                        } else if (ec == boost::asio::error::eof) {
                            test_completed = true;
                        }
                    });
            };
            do_read();
        });
    
    // Start io_context in background
    run_io_context();
    
    // Create TCP proxy connection
    tcp_proxy_->create_connection(
        client_socket,
        9100,
        [&](bool success, const std::string& error) {
            ASSERT_TRUE(success) << "Connection failed: " << error;
            connection_established = true;
            
            // Get connection
            auto conn = tcp_proxy_->get_connection(connection_id);
            ASSERT_NE(conn, nullptr);
            connection_id = conn->connection_id;
            
            // Send data in chunks
            size_t sent_total = 0;
            const size_t chunk_size = 8192;  // 8KB chunks
            
            while (sent_total < data_size) {
                size_t to_send = std::min(chunk_size, data_size - sent_total);
                size_t sent = tcp_proxy_->send_data(
                    connection_id,
                    send_data.data() + sent_total,
                    to_send);
                
                ASSERT_GT(sent, 0) << "Failed to send data at offset " << sent_total;
                sent_total += sent;
                
                // Small delay to prevent overwhelming buffers
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    
    // Wait for test to complete (with timeout)
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(30);
    
    while (!test_completed) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed > timeout) {
            FAIL() << "Test timeout - received " << received_data.size() << " of " << data_size << " bytes";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Verify data integrity
    ASSERT_EQ(received_data.size(), data_size) << "Data size mismatch";
    ASSERT_EQ(received_data, send_data) << "Data content mismatch - byte-for-byte verification failed";
    
    // Verify connection statistics
    auto conn = tcp_proxy_->get_connection(connection_id);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->bytes_sent, data_size);
    EXPECT_EQ(conn->state, proxy::TCPProxy::ConnectionState::ESTABLISHED);
}

/**
 * Test: Verify TCP framing protocol
 * 
 * This test validates:
 * - TCP_DATA frame serialization/deserialization
 * - TCP_CLOSE frame handling
 * - TCP_ERROR frame handling
 * - TCP_WINDOW frame handling
 * - Frame boundary detection
 */
TEST_F(TCPTunnelTest, TCPFramingProtocol) {
    proxy::ProtocolMultiplexer multiplexer;
    
    // Test TCP_DATA frame
    {
        std::string conn_id = "test-connection-123";
        std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};
        
        ASSERT_TRUE(multiplexer.send_tcp_data(conn_id, payload.data(), payload.size()));
        
        std::string output = multiplexer.get_output_data();
        ASSERT_GT(output.size(), 0);
        
        // Parse frame
        auto frame = proxy::ProtocolMultiplexer::TCPFrame::deserialize(
            reinterpret_cast<const uint8_t*>(output.data()),
            output.size());
        
        EXPECT_EQ(frame.type, proxy::ProtocolMultiplexer::TCPFrameType::TCP_DATA);
        EXPECT_EQ(frame.connection_id, conn_id);
        EXPECT_EQ(frame.payload, payload);
    }
    
    // Test TCP_CLOSE frame
    {
        std::string conn_id = "test-connection-456";
        ASSERT_TRUE(multiplexer.send_tcp_close(conn_id));
        
        std::string output = multiplexer.get_output_data();
        auto frame = proxy::ProtocolMultiplexer::TCPFrame::deserialize(
            reinterpret_cast<const uint8_t*>(output.data()),
            output.size());
        
        EXPECT_EQ(frame.type, proxy::ProtocolMultiplexer::TCPFrameType::TCP_CLOSE);
        EXPECT_EQ(frame.connection_id, conn_id);
        EXPECT_EQ(frame.payload.size(), 0);
    }
    
    // Test TCP_ERROR frame
    {
        std::string conn_id = "test-connection-789";
        std::string error_msg = "Connection refused";
        ASSERT_TRUE(multiplexer.send_tcp_error(conn_id, error_msg));
        
        std::string output = multiplexer.get_output_data();
        auto frame = proxy::ProtocolMultiplexer::TCPFrame::deserialize(
            reinterpret_cast<const uint8_t*>(output.data()),
            output.size());
        
        EXPECT_EQ(frame.type, proxy::ProtocolMultiplexer::TCPFrameType::TCP_ERROR);
        EXPECT_EQ(frame.connection_id, conn_id);
        
        std::string received_error(frame.payload.begin(), frame.payload.end());
        EXPECT_EQ(received_error, error_msg);
    }
    
    // Test TCP_WINDOW frame
    {
        std::string conn_id = "test-connection-abc";
        uint32_t window_size = 65536;
        ASSERT_TRUE(multiplexer.send_tcp_window(conn_id, window_size));
        
        std::string output = multiplexer.get_output_data();
        auto frame = proxy::ProtocolMultiplexer::TCPFrame::deserialize(
            reinterpret_cast<const uint8_t*>(output.data()),
            output.size());
        
        EXPECT_EQ(frame.type, proxy::ProtocolMultiplexer::TCPFrameType::TCP_WINDOW);
        EXPECT_EQ(frame.connection_id, conn_id);
        EXPECT_EQ(frame.payload.size(), 4);
        
        // Decode window size
        uint32_t decoded_window = (static_cast<uint32_t>(frame.payload[0]) << 24) |
                                 (static_cast<uint32_t>(frame.payload[1]) << 16) |
                                 (static_cast<uint32_t>(frame.payload[2]) << 8) |
                                 static_cast<uint32_t>(frame.payload[3]);
        EXPECT_EQ(decoded_window, window_size);
    }
}

/**
 * Test: Flow control with backpressure
 * 
 * This test validates:
 * - Send window enforcement (64KB)
 * - Backpressure detection when buffer full
 * - Window update mechanism
 * - Buffer overflow prevention
 */
TEST_F(TCPTunnelTest, FlowControlBackpressure) {
    auto client_socket = std::make_shared<tcp::socket>(*io_context_);
    std::string connection_id;
    
    // Create connection
    tcp_proxy_->create_connection(
        client_socket,
        9100,
        [&](bool success, const std::string& error) {
            ASSERT_TRUE(success) << "Connection failed: " << error;
        });
    
    // Get connection
    auto conn = tcp_proxy_->get_connection(connection_id);
    ASSERT_NE(conn, nullptr);
    connection_id = conn->connection_id;
    
    // Fill send buffer to capacity
    const size_t window_size = 65536;  // 64KB
    std::vector<uint8_t> large_data(window_size + 1000);
    
    size_t sent = tcp_proxy_->send_data(
        connection_id,
        large_data.data(),
        large_data.size());
    
    // Should only send up to window size
    EXPECT_LE(sent, window_size);
    EXPECT_FALSE(tcp_proxy_->can_send(connection_id));
    
    // Update window (simulate data consumed by receiver)
    tcp_proxy_->update_window(connection_id, 8192);  // 8KB consumed
    
    // Should now be able to send more
    EXPECT_TRUE(tcp_proxy_->can_send(connection_id));
    
    // Send additional data
    size_t additional_sent = tcp_proxy_->send_data(
        connection_id,
        large_data.data(),
        1000);
    
    EXPECT_GT(additional_sent, 0);
}
