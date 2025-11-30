# ProtoGate v0 - WebSocket-Based Tunnel Fabric

ProtoGate is a minimal but robust internal tunnel/agent fabric for connecting cloud services to on-premise/edge agents. It enables secure communication through WebSocket-based tunnels with stream multiplexing, TCP forwarding, and a job protocol for agent management.

## Features

- **WebSocket Transport**: Firewall-friendly outbound connections from agents
- **Stream Multiplexing**: Multiple logical tunnels over single WebSocket connection
- **TCP Forwarding**: Forward HTTP/TCP traffic through tunnels to local services
- **Job Protocol**: Send JSON commands to agents and receive results
- **Simple Configuration**: YAML-based config files with minimal setup
- **Token-Based Auth**: Pre-shared token authentication for agents

## Architecture

```
┌─────────────────┐                    ┌──────────────────┐
│  Tunnel Client  │                    │  ProtoGate Agent │
│  (Browser/Tool) │                    │  (On-Prem/Edge)  │
└────────┬────────┘                    └─────────┬────────┘
         │                                       │
         │ TCP Connection                        │ WebSocket
         │                                       │
         ▼                                       ▼
┌─────────────────────────────────────────────────────────┐
│           ProtoGate Server (Cloud/VM/ACA)               │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Agent Registry + WebSocket Handler              │   │
│  └──────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Tunnel Manager (Stream Multiplexing)            │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

**Required Dependencies:**
- CMake 3.20 or later
- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 2022+)
- Boost 1.84+ (Beast and Asio components)
- nlohmann/json 3.11+
- yaml-cpp 0.8+
- spdlog 1.12+

### Installation on Ubuntu/Debian

```bash
# Install build tools
sudo apt update
sudo apt install -y cmake g++ build-essential

# Install dependencies
sudo apt install -y \
    libboost-all-dev \
    nlohmann-json3-dev \
    libyaml-cpp-dev \
    libspdlog-dev
```

### Installation on macOS

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake boost nlohmann-json yaml-cpp spdlog
```

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/yourusername/protogate.git
cd protogate

# Create build directory and configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Binaries are in build/
ls -lh build/protogate-server build/protogate-agent
```

### Build Options

```bash
# Debug build with symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build without tests
cmake -B build -DBUILD_TESTS=OFF

# Install to system
cmake --build build --target install
```

## Usage

### Quick Start

1. **Start the server:**
```bash
./build/protogate-server --config examples/server.yaml
```

2. **Start a local test service (in another terminal):**
```bash
# Example: Simple HTTP server on port 3000
python3 -m http.server 3000
```

3. **Start the agent (in another terminal):**
```bash
./build/protogate-agent --config examples/agent.yaml
```

4. **Test the tunnel (in another terminal):**
```bash
# Connect to server's tunnel port (9000) to reach agent's local service (3000)
curl http://localhost:9000/
```

### Configuration

#### Server Configuration (`server.yaml`)

```yaml
agent_endpoint: "/agent"      # WebSocket path for agents
agent_port: 8080              # Port for agent WebSocket connections
tunnel_port: 9000             # Port for tunnel client connections
shared_secret: "your-secret"  # Authentication token (CHANGE IN PRODUCTION!)
log_level: "info"             # Log verbosity: debug, info, warn, error
heartbeat_timeout: 90         # Agent timeout in seconds
```

#### Agent Configuration (`agent.yaml`)

```yaml
server_url: "ws://server:8080/agent"  # WebSocket server URL
agent_id: "agent-001"                  # Unique agent identifier
shared_secret: "your-secret"           # Must match server secret
log_level: "info"                      # Log verbosity
heartbeat_interval: 30                 # Heartbeat frequency in seconds
tunnels:
  - id: "app1"                         # Tunnel identifier
    local_host: "127.0.0.1"            # Target service host
    local_port: 3000                   # Target service port
```

### Command-Line Options

```bash
# Server
./protogate-server --config server.yaml [--agent-port PORT] [--tunnel-port PORT]

# Agent
./protogate-agent --config agent.yaml [--server-url URL] [--agent-id ID]
```

## Testing

### Manual Integration Test

```bash
# Terminal 1: Start local service
python3 -m http.server 3000

# Terminal 2: Start server
./build/protogate-server --config examples/server.yaml

# Terminal 3: Start agent
./build/protogate-agent --config examples/agent.yaml

# Terminal 4: Test
curl http://localhost:9000/
```

### Automated Tests

```bash
# Run unit tests
cd build && ctest --output-on-failure

# Run integration test script
./tests/integration_test.sh
```

## Production Deployment

### TLS Termination with nginx

ProtoGate uses plain WebSocket (ws://) internally. In production, use nginx or Azure Front Door for TLS termination:

```nginx
# nginx configuration
upstream protogate {
    server localhost:8080;
}

server {
    listen 443 ssl http2;
    server_name tunnel.example.com;
    
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    
    location /agent {
        proxy_pass http://protogate;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 300s;
    }
}
```

### Azure Container Apps Deployment

```bash
# Build container
docker build -t protogate-server:latest .

# Deploy to Azure Container Apps
az containerapp create \
  --name protogate-server \
  --resource-group myResourceGroup \
  --environment myEnvironment \
  --image protogate-server:latest \
  --target-port 8080 \
  --ingress external \
  --min-replicas 1
```

## Architecture Details

### Protocol

**Control Messages (JSON over WebSocket text frames):**
- `register`: Agent authentication and registration
- `register_ack`: Server acknowledgment
- `heartbeat`: Keep-alive mechanism
- `open_tunnel`: Establish new tunnel stream
- `close_tunnel`: Close existing stream
- `job`: Send command to agent
- `job_result`: Agent response

**Data Frames (Binary over WebSocket binary frames):**
```
[4 bytes stream_id][variable payload]
```

### Components

- **AgentRegistry**: Track connected agents and capabilities
- **AgentConnection**: Handle WebSocket communication with agents
- **TunnelManager**: Manage TCP tunnel connections and stream multiplexing
- **ControlMessageHandler**: Parse and route JSON control messages

## Limitations (v0)

- No TLS in application (delegate to nginx/reverse proxy)
- No authentication beyond pre-shared tokens
- No persistence or metrics
- Single-threaded I/O model
- No dynamic tunnel creation (configured in YAML)
- No multi-tenancy or user isolation

## Roadmap

### v0.5
- Automatic reconnection with exponential backoff
- Job timeout handling with retries
- Improved error messages and logging
- Unit test coverage >80%

### v1.0
- Metrics and observability (Prometheus)
- Dynamic tunnel creation API
- Multiple target services per agent
- Agent capabilities negotiation
- Performance optimization

## Troubleshooting

### Agent Connection Issues

```bash
# Check server is listening
netstat -an | grep 8080

# Test WebSocket connection
curl -i -N -H "Connection: Upgrade" -H "Upgrade: websocket" \
  http://localhost:8080/agent
```

### Tunnel Not Working

```bash
# Verify local service is running
curl http://localhost:3000/

# Check agent logs for open_tunnel messages
# Check server logs for client connections
```

### Build Errors

```bash
# Verify dependencies
cmake --version
g++ --version
pkg-config --modversion boost
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development guidelines.

## License

[Your License Here]

## References

- [WebSocket Protocol RFC 6455](https://datatracker.ietf.org/doc/html/rfc6455)
- [Boost.Beast Documentation](https://www.boost.org/doc/libs/1_84_0/libs/beast/doc/html/index.html)
- [Specification](specs/003-protogate-v0-websocket/spec.md)
- [Implementation Plan](specs/003-protogate-v0-websocket/plan.md)
- [Task Breakdown](specs/003-protogate-v0-websocket/tasks.md)
