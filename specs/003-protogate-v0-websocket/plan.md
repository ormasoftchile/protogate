# Implementation Plan: ProtoGate v0 - WebSocket-Based Tunnel Fabric

**Feature ID**: 003-protogate-v0-websocket  
**Created**: 2025-11-29  
**Status**: Planning  
**Estimated Duration**: 16-20 hours

---

## Overview

This plan outlines the implementation strategy for ProtoGate v0, a minimal but robust WebSocket-based tunnel fabric. The implementation is organized into 7 phases, starting from project setup through to integration testing.

**Key Principles**:
- Build incrementally with working code at each phase
- Test continuously (compile and run after each phase)
- Keep it simple (no premature optimization)
- Clear ownership and responsibilities for each component

---

## Phase 0: Project Setup and Dependencies (2-3 hours)

### Objectives
- Set up CMake build system
- Configure dependencies (Boost, nlohmann/json, yaml-cpp, spdlog)
- Create basic project structure
- Verify build environment

### Deliverables

#### File Structure
```
/Volumes/Projects/protogate/
├── CMakeLists.txt                    # Root CMake configuration
├── cmake/
│   ├── FindBoost.cmake              # Boost finder (if needed)
│   └── dependencies.cmake           # External dependencies
├── src/
│   ├── server/
│   │   └── main.cpp                 # Server entry point (stub)
│   ├── agent/
│   │   └── main.cpp                 # Agent entry point (stub)
│   └── common/
│       ├── config.cpp/h             # Configuration parser
│       └── logger.cpp/h             # Logging wrapper
├── include/
│   └── protogate/
│       ├── protocol.h               # Protocol message definitions
│       └── types.h                  # Common types
├── tests/
│   ├── CMakeLists.txt
│   └── test_protocol.cpp            # Basic protocol tests
├── examples/
│   ├── server.yaml                  # Example server config
│   └── agent.yaml                   # Example agent config
└── README.md                        # Build and run instructions
```

#### CMakeLists.txt (Root)
```cmake
cmake_minimum_required(VERSION 3.20)
project(protogate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find dependencies
find_package(Boost 1.84 REQUIRED COMPONENTS system)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(yaml-cpp 0.8 REQUIRED)
find_package(spdlog 1.12 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Subdirectories
add_subdirectory(src/server)
add_subdirectory(src/agent)
add_subdirectory(src/common)

# Tests (optional)
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

#### Tasks
1. Create directory structure
2. Write root CMakeLists.txt
3. Create stub main.cpp for server and agent
4. Write basic README.md with build instructions
5. Set up logging wrapper (spdlog)
6. Set up config parser (yaml-cpp)
7. Test build: `cmake -B build && cmake --build build`

**Validation**: Both `protogate-server` and `protogate-agent` executables build and print "Hello, ProtoGate!"

---

## Phase 1: Protocol Definition and Message Handling (3-4 hours)

### Objectives
- Define protocol message structures
- Implement JSON serialization/deserialization
- Create message parser and builder
- Write unit tests for protocol

### Deliverables

#### include/protogate/protocol.h
```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace protogate {

enum class MessageType {
    Register,
    RegisterAck,
    Heartbeat,
    OpenTunnel,
    CloseTunnel,
    Job,
    JobResult,
    Unknown
};

// Base message
struct Message {
    MessageType type;
    virtual ~Message() = default;
    virtual nlohmann::json to_json() const = 0;
    static std::unique_ptr<Message> from_json(const nlohmann::json& j);
};

// Agent -> Server: Registration
struct RegisterMessage : Message {
    std::string agent_id;
    std::string secret;
    std::vector<std::string> capabilities;
    
    RegisterMessage();
    nlohmann::json to_json() const override;
};

// Server -> Agent: Registration acknowledgment
struct RegisterAckMessage : Message {
    bool success;
    std::optional<std::string> message;
    
    RegisterAckMessage();
    nlohmann::json to_json() const override;
};

// Agent -> Server: Heartbeat
struct HeartbeatMessage : Message {
    std::string agent_id;
    
    HeartbeatMessage();
    nlohmann::json to_json() const override;
};

// Server -> Agent: Open tunnel
struct OpenTunnelMessage : Message {
    uint32_t stream_id;
    std::string target_host;
    uint16_t target_port;
    
    OpenTunnelMessage();
    nlohmann::json to_json() const override;
};

// Bidirectional: Close tunnel
struct CloseTunnelMessage : Message {
    uint32_t stream_id;
    std::optional<std::string> reason;
    
    CloseTunnelMessage();
    nlohmann::json to_json() const override;
};

// Server -> Agent: Job
struct JobMessage : Message {
    std::string job_id;
    std::string job_type;
    nlohmann::json payload;
    
    JobMessage();
    nlohmann::json to_json() const override;
};

// Agent -> Server: Job result
struct JobResultMessage : Message {
    std::string job_id;
    std::string status;
    nlohmann::json result;
    
    JobResultMessage();
    nlohmann::json to_json() const override;
};

// Data frame structure
struct DataFrame {
    uint32_t stream_id;
    std::vector<uint8_t> payload;
    
    // Serialize to binary (stream_id + payload)
    std::vector<uint8_t> to_binary() const;
    
    // Deserialize from binary
    static DataFrame from_binary(const std::vector<uint8_t>& data);
};

} // namespace protogate
```

#### src/common/protocol.cpp
```cpp
#include "protogate/protocol.h"
#include <arpa/inet.h> // for htonl/ntohl

namespace protogate {

// Implementation of to_json() and from_json() for each message type
// DataFrame binary serialization/deserialization

std::vector<uint8_t> DataFrame::to_binary() const {
    std::vector<uint8_t> result(4 + payload.size());
    uint32_t network_stream_id = htonl(stream_id);
    std::memcpy(result.data(), &network_stream_id, 4);
    std::memcpy(result.data() + 4, payload.data(), payload.size());
    return result;
}

DataFrame DataFrame::from_binary(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        throw std::runtime_error("Invalid data frame: too short");
    }
    
    uint32_t network_stream_id;
    std::memcpy(&network_stream_id, data.data(), 4);
    uint32_t stream_id = ntohl(network_stream_id);
    
    DataFrame frame;
    frame.stream_id = stream_id;
    frame.payload.assign(data.begin() + 4, data.end());
    return frame;
}

} // namespace protogate
```

#### Tasks
1. Define all message structures in protocol.h
2. Implement JSON serialization for each message type
3. Implement JSON deserialization with error handling
4. Implement DataFrame binary encoding/decoding
5. Write unit tests for each message type
6. Test round-trip: message → JSON → message
7. Test binary frame encoding/decoding

**Validation**: All protocol tests pass. Messages can be created, serialized, deserialized correctly.

---

## Phase 2: Server - WebSocket Listener and Agent Registry (4-5 hours)

### Objectives
- Implement WebSocket server using Boost.Beast
- Create AgentRegistry for tracking connected agents
- Handle agent registration and authentication
- Implement heartbeat tracking

### Deliverables

#### include/protogate/agent_registry.h
```cpp
#pragma once
#include <string>
#include <memory>
#include <map>
#include <mutex>
#include <chrono>

namespace protogate {

struct AgentInfo {
    std::string agent_id;
    std::vector<std::string> capabilities;
    std::chrono::steady_clock::time_point last_heartbeat;
    // WebSocket connection handle (defined in server)
    std::shared_ptr<void> connection;
};

class AgentRegistry {
public:
    // Register new agent
    bool register_agent(const std::string& agent_id, 
                       std::shared_ptr<void> connection,
                       const std::vector<std::string>& capabilities);
    
    // Unregister agent
    void unregister_agent(const std::string& agent_id);
    
    // Update heartbeat timestamp
    void update_heartbeat(const std::string& agent_id);
    
    // Get agent info
    std::shared_ptr<AgentInfo> get_agent(const std::string& agent_id);
    
    // Check for timed-out agents
    std::vector<std::string> get_timed_out_agents(std::chrono::seconds timeout);
    
private:
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<AgentInfo>> agents_;
};

} // namespace protogate
```

#### src/server/agent_connection.h
```cpp
#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace protogate {

class AgentConnection : public std::enable_shared_from_this<AgentConnection> {
public:
    explicit AgentConnection(tcp::socket socket, 
                            const std::string& shared_secret,
                            class AgentRegistry& registry);
    
    void run();
    void send_message(const std::string& json_msg);
    void send_binary(const std::vector<uint8_t>& data);
    void close();
    
private:
    void do_accept();
    void do_read();
    void handle_message(const std::string& text);
    void handle_binary(const std::vector<uint8_t>& data);
    void handle_register(const RegisterMessage& msg);
    void handle_heartbeat(const HeartbeatMessage& msg);
    void handle_job_result(const JobResultMessage& msg);
    
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::string shared_secret_;
    AgentRegistry& registry_;
    std::string agent_id_; // Set after registration
    bool authenticated_ = false;
};

} // namespace protogate
```

#### src/server/main.cpp (updated)
```cpp
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <protogate/config.h>
#include <protogate/logger.h>
#include "agent_connection.h"
#include "agent_registry.h"

int main(int argc, char* argv[]) {
    // Load config
    auto config = protogate::Config::load("server.yaml");
    protogate::Logger::init(config.log_level);
    
    // Create registry
    protogate::AgentRegistry registry;
    
    // Create io_context
    net::io_context ioc;
    
    // Create acceptor
    tcp::acceptor acceptor(ioc, 
        tcp::endpoint(tcp::v4(), config.agent_port));
    
    // Accept loop
    std::function<void()> do_accept = [&]() {
        acceptor.async_accept([&](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                auto conn = std::make_shared<AgentConnection>(
                    std::move(socket), config.shared_secret, registry);
                conn->run();
            }
            do_accept();
        });
    };
    
    do_accept();
    
    LOG_INFO("Server listening on port {}", config.agent_port);
    ioc.run();
    
    return 0;
}
```

#### Tasks
1. Implement AgentRegistry class with thread-safe operations
2. Create AgentConnection class for WebSocket handling
3. Implement WebSocket accept and upgrade
4. Implement message reading loop
5. Handle register message and authentication
6. Handle heartbeat messages
7. Add timeout checking for heartbeats
8. Write unit tests for AgentRegistry

**Validation**: Server starts, accepts WebSocket connections. Can parse register/heartbeat messages. AgentRegistry tracks agents correctly.

---

## Phase 3: Agent - WebSocket Client and Registration (3-4 hours)

### Objectives
- Implement WebSocket client using Boost.Beast
- Connect to server and perform registration
- Implement heartbeat mechanism
- Handle reconnection logic

### Deliverables

#### src/agent/agent_client.h
```cpp
#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <string>
#include <functional>

namespace protogate {

class AgentClient : public std::enable_shared_from_this<AgentClient> {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using BinaryCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    AgentClient(net::io_context& ioc,
                const std::string& server_url,
                const std::string& agent_id,
                const std::string& secret);
    
    void connect();
    void disconnect();
    void send_message(const std::string& json_msg);
    void send_binary(const std::vector<uint8_t>& data);
    
    void set_message_callback(MessageCallback cb);
    void set_binary_callback(BinaryCallback cb);
    
private:
    void do_connect();
    void do_handshake();
    void do_read();
    void do_register();
    void start_heartbeat();
    void send_heartbeat();
    void handle_message(const std::string& text);
    void handle_binary(const std::vector<uint8_t>& data);
    
    net::io_context& ioc_;
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::string server_url_;
    std::string agent_id_;
    std::string secret_;
    bool connected_ = false;
    MessageCallback message_cb_;
    BinaryCallback binary_cb_;
    net::steady_timer heartbeat_timer_;
};

} // namespace protogate
```

#### src/agent/main.cpp (updated)
```cpp
#include <boost/asio.hpp>
#include <protogate/config.h>
#include <protogate/logger.h>
#include "agent_client.h"
#include "tunnel_manager.h"

int main(int argc, char* argv[]) {
    // Load config
    auto config = protogate::Config::load("agent.yaml");
    protogate::Logger::init(config.log_level);
    
    // Create io_context
    net::io_context ioc;
    
    // Create agent client
    auto client = std::make_shared<AgentClient>(
        ioc, config.server_url, config.agent_id, config.shared_secret);
    
    // Create tunnel manager (Phase 4)
    // auto tunnel_mgr = std::make_shared<TunnelManager>(client, config.tunnels);
    
    // Set message callback
    client->set_message_callback([](const std::string& msg) {
        LOG_DEBUG("Received message: {}", msg);
        // Handle open_tunnel, close_tunnel, job messages
    });
    
    // Set binary callback
    client->set_binary_callback([](const std::vector<uint8_t>& data) {
        // Handle data frames (Phase 4)
    });
    
    // Connect
    client->connect();
    
    LOG_INFO("Agent {} starting", config.agent_id);
    ioc.run();
    
    return 0;
}
```

#### Tasks
1. Implement AgentClient class
2. Implement WebSocket connection and handshake
3. Implement registration message sending
4. Handle register_ack response
5. Implement heartbeat timer (30s interval)
6. Implement message/binary callbacks
7. Add basic reconnection logic
8. Test connection to server

**Validation**: Agent connects to server, sends register message, receives ack, sends periodic heartbeats. Server logs show agent registration.

---

## Phase 4: Tunnel Manager and Stream Multiplexing (5-6 hours)

### Objectives
- Implement TCP listener for tunnel clients (server-side)
- Implement stream ID assignment and tracking
- Implement local TCP connections (agent-side)
- Handle open_tunnel and close_tunnel messages
- Forward data between client/agent/local service

### Deliverables

#### Server: src/server/tunnel_manager.h
```cpp
#pragma once
#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <atomic>

namespace protogate {

class TunnelManager {
public:
    explicit TunnelManager(net::io_context& ioc, uint16_t port,
                          AgentRegistry& registry);
    
    void start();
    void stop();
    
    // Called when data frame received from agent
    void handle_data_frame(const DataFrame& frame);
    
    // Called when agent disconnects
    void close_agent_streams(const std::string& agent_id);
    
private:
    struct ClientConnection {
        std::shared_ptr<tcp::socket> socket;
        std::string agent_id;
        uint32_t stream_id;
        std::vector<uint8_t> buffer;
    };
    
    void do_accept();
    void handle_client(std::shared_ptr<tcp::socket> socket);
    void read_from_client(std::shared_ptr<ClientConnection> conn);
    void write_to_client(uint32_t stream_id, const std::vector<uint8_t>& data);
    uint32_t allocate_stream_id();
    
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    AgentRegistry& registry_;
    std::atomic<uint32_t> next_stream_id_{1};
    std::map<uint32_t, std::shared_ptr<ClientConnection>> clients_;
    std::mutex mutex_;
};

} // namespace protogate
```

#### Agent: src/agent/tunnel_manager.h
```cpp
#pragma once
#include <boost/asio.hpp>
#include <map>
#include <memory>

namespace protogate {

struct TunnelConfig {
    std::string id;
    std::string local_host;
    uint16_t local_port;
};

class AgentTunnelManager {
public:
    explicit AgentTunnelManager(net::io_context& ioc,
                               std::shared_ptr<AgentClient> client,
                               const std::vector<TunnelConfig>& tunnels);
    
    // Called when open_tunnel message received
    void handle_open_tunnel(const OpenTunnelMessage& msg);
    
    // Called when close_tunnel message received
    void handle_close_tunnel(const CloseTunnelMessage& msg);
    
    // Called when data frame received from server
    void handle_data_frame(const DataFrame& frame);
    
private:
    struct LocalConnection {
        std::shared_ptr<tcp::socket> socket;
        uint32_t stream_id;
        std::vector<uint8_t> buffer;
    };
    
    void connect_local(uint32_t stream_id, 
                      const std::string& host, uint16_t port);
    void read_from_local(std::shared_ptr<LocalConnection> conn);
    void write_to_local(uint32_t stream_id, const std::vector<uint8_t>& data);
    void close_stream(uint32_t stream_id, const std::string& reason);
    
    net::io_context& ioc_;
    std::shared_ptr<AgentClient> client_;
    std::vector<TunnelConfig> tunnels_;
    std::map<uint32_t, std::shared_ptr<LocalConnection>> connections_;
    std::mutex mutex_;
};

} // namespace protogate
```

#### Tasks
1. Implement server TunnelManager with TCP acceptor
2. Implement client connection handling and stream_id assignment
3. Implement reading from client socket and sending to agent
4. Implement receiving data frames from agent and writing to client
5. Implement agent TunnelManager
6. Handle open_tunnel message: connect to local service
7. Implement reading from local socket and sending to server
8. Implement receiving data frames from server and writing to local
9. Handle close_tunnel message
10. Add proper error handling and cleanup

**Validation**: 
- Client connects to server tunnel port
- Server sends open_tunnel to agent
- Agent connects to local service
- Data flows: client → server → agent → local service
- Response flows back correctly

---

## Phase 5: Job Protocol Implementation (2-3 hours)

### Objectives
- Implement job sending from server
- Implement job receiving and processing on agent
- Implement job result handling
- Add timeout mechanism

### Deliverables

#### Server: src/server/job_manager.h
```cpp
#pragma once
#include <string>
#include <map>
#include <memory>
#include <chrono>
#include <functional>

namespace protogate {

using JobCallback = std::function<void(const std::string& job_id, 
                                       const JobResultMessage& result)>;

class JobManager {
public:
    explicit JobManager(AgentRegistry& registry);
    
    // Send job to agent
    std::string send_job(const std::string& agent_id,
                        const std::string& job_type,
                        const nlohmann::json& payload,
                        JobCallback callback);
    
    // Handle job result from agent
    void handle_job_result(const JobResultMessage& result);
    
    // Check for timed-out jobs
    void check_timeouts();
    
private:
    struct PendingJob {
        std::string job_id;
        std::string agent_id;
        JobCallback callback;
        std::chrono::steady_clock::time_point created_at;
    };
    
    AgentRegistry& registry_;
    std::map<std::string, PendingJob> pending_jobs_;
    std::mutex mutex_;
    std::chrono::seconds timeout_{60};
};

} // namespace protogate
```

#### Agent: src/agent/job_handler.h
```cpp
#pragma once
#include <protogate/protocol.h>
#include <memory>

namespace protogate {

class JobHandler {
public:
    explicit JobHandler(std::shared_ptr<AgentClient> client);
    
    // Handle incoming job
    void handle_job(const JobMessage& job);
    
private:
    void execute_job(const JobMessage& job);
    void send_result(const std::string& job_id, 
                    const std::string& status,
                    const nlohmann::json& result);
    
    std::shared_ptr<AgentClient> client_;
};

} // namespace protogate
```

#### Tasks
1. Implement JobManager on server
2. Implement job sending with unique job_id generation (UUID)
3. Implement job timeout tracking
4. Implement JobHandler on agent
5. Implement stub job execution (log payload, return success)
6. Implement job result sending
7. Wire job messages into agent message callback
8. Test job round-trip

**Validation**:
- Server can send job to agent
- Agent receives job, logs payload
- Agent sends job_result back
- Server receives result and matches to original job
- Job timeout works after 60 seconds

---

## Phase 6: Configuration and Error Handling (2-3 hours)

### Objectives
- Implement comprehensive configuration loading
- Add proper error handling throughout
- Improve logging with context
- Handle edge cases and cleanup

### Deliverables

#### src/common/config.cpp
```cpp
#include "protogate/config.h"
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace protogate {

Config Config::load(const std::string& path) {
    YAML::Node yaml = YAML::LoadFile(path);
    Config config;
    
    // Parse server config
    if (yaml["agent_port"]) {
        config.agent_port = yaml["agent_port"].as<uint16_t>();
    }
    if (yaml["tunnel_port"]) {
        config.tunnel_port = yaml["tunnel_port"].as<uint16_t>();
    }
    // ... parse all fields with validation
    
    return config;
}

void Config::validate() {
    if (agent_id.empty()) {
        throw std::runtime_error("agent_id is required");
    }
    if (shared_secret.empty()) {
        throw std::runtime_error("shared_secret is required");
    }
    // ... validate all required fields
}

} // namespace protogate
```

#### Improvements
```cpp
// Add structured logging with context
LOG_INFO("Agent registered", 
    {{"agent_id", agent_id}, {"capabilities", capabilities}});

// Add error handling
try {
    // WebSocket operations
} catch (const boost::system::system_error& e) {
    LOG_ERROR("WebSocket error: {}", e.what());
    close();
} catch (const std::exception& e) {
    LOG_ERROR("Unexpected error: {}", e.what());
}

// Add graceful shutdown
void Server::shutdown() {
    LOG_INFO("Server shutting down");
    acceptor_.close();
    // Close all agent connections
    for (auto& [id, info] : registry_.get_all()) {
        auto conn = std::static_pointer_cast<AgentConnection>(info->connection);
        conn->close();
    }
}
```

#### Tasks
1. Complete Config class implementation
2. Add validation for all config fields
3. Add command-line argument parsing
4. Improve error handling in all components
5. Add structured logging with context fields
6. Implement graceful shutdown handlers
7. Add RAII wrappers for resources
8. Test with invalid configurations

**Validation**:
- Config files parsed correctly
- Invalid config produces clear error messages
- Command-line args override config
- Graceful shutdown works
- No resource leaks

---

## Phase 7: Integration Testing and Documentation (3-4 hours)

### Objectives
- Write comprehensive integration tests
- Create manual test scenarios
- Write README and usage documentation
- Test all functional requirements

### Deliverables

#### tests/integration_test.sh
```bash
#!/bin/bash
set -e

echo "=== ProtoGate v0 Integration Test ==="

# Start local test service
echo "Starting test service on port 3000..."
python3 -m http.server 3000 > /tmp/test-service.log 2>&1 &
TEST_SERVICE_PID=$!
sleep 2

# Start server
echo "Starting protogate-server..."
./build/protogate-server --config examples/server.yaml > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start agent
echo "Starting protogate-agent..."
./build/protogate-agent --config examples/agent.yaml > /tmp/agent.log 2>&1 &
AGENT_PID=$!
sleep 3

# Test 1: Agent registration
echo "Test 1: Checking agent registration..."
if grep -q "Agent.*registered" /tmp/server.log; then
    echo "✅ Agent registered successfully"
else
    echo "❌ Agent registration failed"
    cat /tmp/server.log
    exit 1
fi

# Test 2: HTTP tunneling
echo "Test 2: Testing HTTP tunnel..."
RESPONSE=$(curl -s http://localhost:9000/)
if echo "$RESPONSE" | grep -q "Directory listing"; then
    echo "✅ HTTP tunneling works"
else
    echo "❌ HTTP tunneling failed"
    echo "Response: $RESPONSE"
    exit 1
fi

# Test 3: Multiple concurrent connections
echo "Test 3: Testing concurrent connections..."
for i in {1..5}; do
    curl -s http://localhost:9000/ > /dev/null &
done
wait
echo "✅ Concurrent connections work"

# Test 4: Heartbeat mechanism
echo "Test 4: Checking heartbeat..."
sleep 35
if grep -q "heartbeat" /tmp/server.log; then
    echo "✅ Heartbeat mechanism works"
else
    echo "❌ Heartbeat not detected"
    exit 1
fi

# Cleanup
echo "Cleaning up..."
kill $AGENT_PID $SERVER_PID $TEST_SERVICE_PID 2>/dev/null
rm -f /tmp/*.log

echo "=== All tests passed! ==="
```

#### README.md
```markdown
# ProtoGate v0 - WebSocket-Based Tunnel Fabric

ProtoGate is a minimal but robust internal tunnel/agent fabric for connecting cloud services to on-premise/edge agents.

## Features

- **WebSocket Transport**: Secure, firewall-friendly connections
- **Stream Multiplexing**: Multiple tunnels over single connection
- **TCP Forwarding**: Forward HTTP/TCP traffic through tunnels
- **Job Protocol**: Send commands to agents and receive results
- **Simple Configuration**: YAML-based config files

## Building

### Dependencies

- CMake 3.20+
- C++20 compiler (GCC 11+, Clang 14+)
- Boost 1.84+ (Beast, Asio)
- nlohmann/json 3.11+
- yaml-cpp 0.8+
- spdlog 1.12+

### Build Instructions

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install cmake g++ libboost-all-dev \
    nlohmann-json3-dev libyaml-cpp-dev libspdlog-dev

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Binaries in build/
./build/protogate-server --help
./build/protogate-agent --help
```

## Usage

### Server Configuration (server.yaml)

```yaml
agent_endpoint: "/agent"
agent_port: 8080
tunnel_port: 9000
shared_secret: "CHANGE_ME_IN_PRODUCTION"
log_level: "info"
heartbeat_timeout: 90
```

### Agent Configuration (agent.yaml)

```yaml
server_url: "ws://localhost:8080/agent"
agent_id: "test-agent-1"
shared_secret: "CHANGE_ME_IN_PRODUCTION"
log_level: "info"
heartbeat_interval: 30
tunnels:
  - id: "dev-app"
    local_host: "127.0.0.1"
    local_port: 3000
```

### Running

```bash
# Terminal 1: Start server
./build/protogate-server --config examples/server.yaml

# Terminal 2: Start local service
python3 -m http.server 3000

# Terminal 3: Start agent
./build/protogate-agent --config examples/agent.yaml

# Terminal 4: Test tunnel
curl http://localhost:9000/
```

## Architecture

See [specs/003-protogate-v0-websocket/spec.md](specs/003-protogate-v0-websocket/spec.md) for detailed architecture.

## Testing

```bash
# Run integration tests
./tests/integration_test.sh

# Run unit tests
cd build && ctest
```

## Limitations (v0)

- No TLS (use nginx for production)
- No authentication beyond pre-shared tokens
- No persistence or metrics
- Single-threaded I/O
- No dynamic tunnel creation

## License

[Your License Here]
```

#### Tasks
1. Write integration test script
2. Test all functional requirements (FR1-FR8)
3. Test all user scenarios from spec
4. Write comprehensive README.md
5. Create example config files
6. Document protocol in spec
7. Add inline code documentation
8. Test on clean system

**Validation**:
- All integration tests pass
- README instructions work on fresh install
- All 4 user scenarios from spec pass
- Code is well-documented

---

## Risk Management

### Technical Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Boost.Beast API complexity | High | Medium | Start with simple examples, read docs thoroughly |
| WebSocket frame handling issues | High | Medium | Test with small and large messages early |
| Stream ID collision | Medium | Low | Use atomic counter, test wrapping |
| Memory leaks in async code | High | Medium | Use RAII, smart pointers, test with valgrind |
| Deadlocks in multi-threaded code | High | Low | Minimize shared state, use scoped locks |

### Process Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Dependencies not available | High | Low | Test build on clean system early |
| Scope creep | Medium | Medium | Strict adherence to v0 scope |
| Testing takes longer than expected | Medium | Medium | Build tests incrementally with each phase |

---

## Dependencies Between Phases

```
Phase 0 (Setup)
    ↓
Phase 1 (Protocol) ←──────────────┐
    ↓                              │
Phase 2 (Server) ──→ Phase 3 (Agent)
    ↓                      ↓
    └──→ Phase 4 (Tunnels) ←──────┘
              ↓
         Phase 5 (Jobs)
              ↓
      Phase 6 (Config/Errors)
              ↓
      Phase 7 (Integration)
```

**Critical Path**: Phase 0 → 1 → 2 → 3 → 4 → 7

**Parallelizable**: 
- Phase 2 and 3 can be developed simultaneously after Phase 1
- Phase 5 can start once Phase 3 is complete (doesn't require tunnels)
- Phase 6 improvements can be done anytime

---

## Validation Checklist

### After Each Phase

- [ ] Code compiles without warnings
- [ ] Phase-specific tests pass
- [ ] No memory leaks (test with valgrind)
- [ ] Logs are clear and informative
- [ ] Error cases handled gracefully

### After Phase 7 (Final)

- [ ] All functional requirements (FR1-FR8) implemented
- [ ] All user scenarios pass
- [ ] Integration test passes
- [ ] README instructions work
- [ ] Code is documented
- [ ] No TODOs or FIXMEs in critical paths
- [ ] Performance is acceptable (not optimized, but functional)
- [ ] Clean shutdown works
- [ ] Agent reconnection works

---

## Success Metrics

### Quantitative

- **Build time**: < 5 minutes on modern hardware
- **Binary size**: < 5MB per executable
- **Memory usage**: < 50MB per component at idle
- **Tunnel latency**: < 10ms added overhead
- **Concurrent streams**: > 100 per agent

### Qualitative

- Code is readable and maintainable
- Architecture is extensible for future versions
- Logs provide useful debugging information
- Configuration is straightforward
- Documentation is clear

---

## Next Steps After v0

### v0.5 Enhancements
- Automatic reconnection with exponential backoff
- Job timeout handling with retries
- Better error messages and recovery
- Unit test coverage > 80%

### v1.0 Features
- Metrics and observability (Prometheus)
- Dynamic tunnel creation API
- Multiple target services per agent
- TLS support in server
- Agent capabilities negotiation

---

## Appendix: File Size Estimates

| Component | Estimated LOC | Files |
|-----------|--------------|-------|
| Protocol definitions | 500 | 2 |
| Server (total) | 1200 | 8 |
| - WebSocket handling | 400 | 2 |
| - Agent registry | 200 | 2 |
| - Tunnel manager | 400 | 2 |
| - Job manager | 200 | 2 |
| Agent (total) | 1000 | 6 |
| - WebSocket client | 400 | 2 |
| - Tunnel manager | 400 | 2 |
| - Job handler | 200 | 2 |
| Common | 400 | 4 |
| Tests | 600 | 4 |
| **Total** | **~3700 LOC** | **24 files** |

---

**End of Implementation Plan**
