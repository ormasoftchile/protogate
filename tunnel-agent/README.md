# Protogate Tunnel Agent

A C++17 client for the Protogate reverse tunnel server. Enables secure access to local services through persistent outbound connections.

## Features

- 🔒 **Secure TLS Connection**: TLS 1.2+ encryption with OpenSSL
- 🚀 **HTTP/2 Protocol**: Efficient multiplexed connections using nghttp2
- 🔑 **Token Authentication**: Bearer token authentication
- ❤️ **Health Monitoring**: Automatic heartbeat and reconnection
- 🔄 **Auto Reconnect**: Exponential backoff on connection failures
- ⚙️ **Flexible Config**: JSON file, CLI arguments, or environment variables

## Building

### Prerequisites

- CMake 3.20+
- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- Homebrew (macOS) or vcpkg (Windows/Linux) for dependencies

### macOS Build (Homebrew)

```bash
# Install dependencies
brew install cmake boost openssl nghttp2 nlohmann-json spdlog

# Clone and build
cd protogate/tunnel-agent
cmake -B build -S . -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build --config Release

# Run
./build/tunnel-agent --config config.json
```

### Linux/Windows Build (vcpkg)

```bash
# Clone repository
git clone https://github.com/ormasoftchile/protogate.git
cd protogate/tunnel-agent

# Setup vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# Install dependencies
./vcpkg/vcpkg install

# Build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Run
./build/tunnel-agent --config config.json
```

## Configuration

### JSON Configuration File

Create `config.json`:

```json
{
  "server": {
    "host": "tunnel-agent.tunnel.mycorp.com",
    "port": 8443,
    "verify_tls": true
  },
  "tunnel": {
    "id": "api",
    "token": "tnl_abc123..."
  },
  "local": {
    "url": "http://localhost:3000",
    "timeout_seconds": 1800
  },
  "health": {
    "heartbeat_interval_seconds": 30,
    "heartbeat_timeout_seconds": 60
  },
  "reconnect": {
    "initial_delay_seconds": 1,
    "max_delay_seconds": 60,
    "max_attempts": 0
  },
  "logging": {
    "level": "info",
    "format": "json"
  }
}
```

### Command Line

```bash
./tunnel-agent \
  --config config.json \
  --server tunnel-agent.mycorp.com:8443 \
  --token tnl_abc123... \
  --tunnel-id api \
  --local-url http://localhost:3000
```

### Environment Variables

```bash
export TUNNEL_SERVER=tunnel-agent.mycorp.com:8443
export TUNNEL_TOKEN=tnl_abc123...
export TUNNEL_ID=api
export LOCAL_URL=http://localhost:3000
./tunnel-agent
```

## Usage

1. **Get a token** from the Protogate Management API
2. **Configure** the agent with your server and local service details
3. **Run** the agent: `./tunnel-agent --config config.json`
4. **Access** your local service through the tunnel URL

## Architecture

```
Internet → Protogate Server → Tunnel Agent → Local Service
           (Cloud/VPS)        (Your Machine)  (localhost:3000)
```

The agent:
1. Connects to Protogate server on port 8443 with TLS
2. Authenticates using bearer token
3. Receives HTTP requests via HTTP/2
4. Forwards requests to local service
5. Returns responses to server
6. Maintains connection health with heartbeats

## Development

### Running Tests

```bash
cmake -B build -S . -DBUILD_TESTS=ON \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build
```

### Project Structure

```
tunnel-agent/
├── src/
│   ├── main.cpp                    # Entry point
│   ├── config/
│   │   ├── agent_config.h          # Configuration
│   │   └── agent_config.cpp
│   ├── client/
│   │   ├── tls_client.h            # TLS connection
│   │   ├── tls_client.cpp
│   │   ├── http2_session.h         # HTTP/2 session
│   │   └── http2_session.cpp
│   ├── forwarder/
│   │   ├── request_forwarder.h     # Request forwarding
│   │   └── request_forwarder.cpp
│   ├── health/
│   │   ├── heartbeat.h             # Health monitoring
│   │   └── heartbeat.cpp
│   └── utils/
│       ├── reconnect.h             # Reconnection logic
│       ├── reconnect.cpp
│       ├── logger.h
│       └── logger.cpp
├── tests/
├── CMakeLists.txt
├── vcpkg.json
└── README.md
```

## Troubleshooting

### Connection Refused

```
Error: Connection refused to tunnel-agent.mycorp.com:8443
```

**Solution**: Verify server is running and port 8443 is accessible.

### Authentication Failed

```
Error: 401 Unauthorized - Invalid token
```

**Solution**: Check token is valid. Generate new token via Management API.

### Local Service Unavailable

```
Error: 502 Bad Gateway - Connection to localhost:3000 refused
```

**Solution**: Ensure local service is running on the configured port.

## Security

- Never commit tokens to version control
- Use environment variables or secure config files
- Enable TLS verification in production (`verify_tls: true`)
- Rotate tokens regularly via Management API

## License

MIT License - see LICENSE file

## Support

- Documentation: https://github.com/ormasoftchile/protogate
- Issues: https://github.com/ormasoftchile/protogate/issues
