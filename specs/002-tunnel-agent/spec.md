# Tunnel Agent Specification (C++)

**Feature ID**: 002-tunnel-agent  
**Version**: 1.0.0  
**Created**: 2025-11-23  
**Status**: Planning

---

## Overview

The Tunnel Agent is a C++17 client application that establishes an outbound TLS connection to the Protogate server and forwards HTTP/TCP traffic from the server to local services. This enables secure remote access to services behind firewalls and NATs without requiring inbound port forwarding.

## Goals

1. **Secure Connection**: Establish TLS 1.2+ connection to Protogate server on port 8443
2. **Token Authentication**: Authenticate using tunnel-specific bearer token
3. **HTTP Tunneling**: Forward HTTP requests via HTTP/2 to local HTTP services
4. **Health Monitoring**: Send heartbeats and handle connection failures
5. **Auto Reconnection**: Reconnect automatically with exponential backoff
6. **Cross-Platform**: Support macOS, Linux, Windows

## Non-Goals (v1.0)

- TCP tunneling (HTTP only for MVP)
- Multiple local targets per agent
- WebSocket protocol support
- UDP tunneling
- Load balancing across multiple agents

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Tunnel Agent (C++)                 │
│                                                     │
│  ┌──────────────┐    ┌──────────────┐             │
│  │  TLS Client  │────│ HTTP/2 Client│             │
│  │  (Port 8443) │    │   Session    │             │
│  └──────────────┘    └──────────────┘             │
│         │                    │                     │
│         │                    │                     │
│  ┌──────────────────────────────────┐             │
│  │      Request Forwarder           │             │
│  │  - Receive HTTP requests         │             │
│  │  - Forward to local service      │             │
│  │  - Return response to server     │             │
│  └──────────────────────────────────┘             │
│         │                                          │
│         ▼                                          │
│  ┌──────────────┐                                 │
│  │Local Service │                                 │
│  │localhost:3000│                                 │
│  └──────────────┘                                 │
└─────────────────────────────────────────────────────┘
```

---

## Technical Stack

### Language & Build System
- **Language**: C++17
- **Build System**: CMake 3.20+
- **Package Manager**: vcpkg
- **Compiler**: GCC 9+, Clang 10+, MSVC 2019+

### Core Dependencies
- **Boost.Asio**: Async I/O and networking
- **Boost.Beast**: HTTP client
- **nghttp2**: HTTP/2 protocol
- **OpenSSL**: TLS 1.2/1.3 support
- **nlohmann/json**: JSON parsing
- **spdlog**: Structured logging

### Optional Dependencies
- **Catch2**: Unit testing
- **Google Benchmark**: Performance testing

---

## File Structure

```
tunnel-agent/
├── CMakeLists.txt
├── vcpkg.json
├── README.md
├── config.example.json
├── src/
│   ├── main.cpp                 # Entry point
│   ├── config/
│   │   ├── agent_config.h
│   │   └── agent_config.cpp
│   ├── client/
│   │   ├── tls_client.h         # TLS connection
│   │   ├── tls_client.cpp
│   │   ├── http2_session.h      # HTTP/2 session
│   │   └── http2_session.cpp
│   ├── forwarder/
│   │   ├── request_forwarder.h  # HTTP request forwarding
│   │   └── request_forwarder.cpp
│   ├── health/
│   │   ├── heartbeat.h          # Heartbeat manager
│   │   └── heartbeat.cpp
│   └── utils/
│       ├── reconnect.h          # Reconnection logic
│       ├── reconnect.cpp
│       ├── logger.h
│       └── logger.cpp
├── tests/
│   ├── test_config.cpp
│   ├── test_client.cpp
│   └── test_forwarder.cpp
└── scripts/
    ├── build.sh
    └── install.sh
```

---

## Configuration

### JSON Configuration File

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

### Command Line Arguments

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
TUNNEL_SERVER=tunnel-agent.mycorp.com:8443
TUNNEL_TOKEN=tnl_abc123...
TUNNEL_ID=api
LOCAL_URL=http://localhost:3000
```

---

## User Stories

### US1: Agent Connection
**As a** developer  
**I want** to run the tunnel agent with a token  
**So that** my local service is accessible through the tunnel

**Acceptance Criteria**:
- Agent connects to server with TLS
- Agent authenticates with bearer token
- Connection status is logged
- Graceful error handling on auth failure

### US2: HTTP Request Forwarding
**As a** developer  
**I want** HTTP requests to be forwarded to my local service  
**So that** external clients can access my API

**Acceptance Criteria**:
- Agent receives HTTP/2 requests from server
- Requests are forwarded to configured local service
- Responses are sent back through HTTP/2
- Request/response headers preserved
- Timeouts handled (30 minute default)

### US3: Connection Health
**As a** system administrator  
**I want** the agent to maintain connection health  
**So that** tunnels remain reliable

**Acceptance Criteria**:
- Heartbeat sent every 30 seconds
- Connection closed if no heartbeat ACK within 60 seconds
- Auto-reconnect with exponential backoff
- Connection state logged

### US4: Configuration
**As a** developer  
**I want** to configure the agent via CLI, config file, or env vars  
**So that** setup is flexible

**Acceptance Criteria**:
- CLI flags supported
- JSON config file supported
- Environment variables supported
- Defaults provided

---

## Protocol Implementation

### Connection Flow

1. **TLS Handshake**: Connect to server on port 8443 with TLS 1.2+
2. **HTTP/2 CONNECT**: Send authentication request
   ```
   CONNECT tunnel-agent HTTP/2
   Host: tunnel-agent.tunnel.mycorp.com
   Authorization: Bearer tnl_abc123
   X-Tunnel-ID: api
   X-Agent-Version: 1.0.0
   ```
3. **Server Response**: Verify 200 Connection Established
4. **Heartbeat Loop**: Send PING frames every 30 seconds
5. **Request Handling**: Process incoming HTTP/2 streams

### Request Flow

1. Server sends request on HTTP/2 stream
2. Agent parses headers and body
3. Agent forwards to local service via HTTP client
4. Agent captures response
5. Agent sends response back on same HTTP/2 stream

---

## Development Phases

### Phase 1: Project Setup & Configuration (Week 1)
- CMake project structure
- vcpkg dependencies
- Configuration loading (JSON, CLI, env vars)
- Logging setup

### Phase 2: TLS & HTTP/2 Client (Week 2)
- TLS connection with Boost.Asio + OpenSSL
- HTTP/2 session with nghttp2
- Authentication handshake
- Connection error handling

### Phase 3: Request Forwarding (Week 2)
- HTTP/2 stream event handling
- Request parsing
- HTTP client for local service
- Response encoding

### Phase 4: Health & Reconnection (Week 3)
- Heartbeat implementation
- Connection monitoring
- Auto-reconnect with backoff
- Graceful shutdown

### Phase 5: Testing & Documentation (Week 3)
- Unit tests
- Integration tests with server
- Performance testing
- Documentation

---

## Success Metrics

- **Connection Success Rate**: >99.9%
- **Request Latency**: <50ms overhead vs direct connection
- **Reconnection Time**: <5 seconds for 95th percentile
- **Memory Usage**: <20MB resident
- **Test Coverage**: >80%

---

## References

- [Agent Protocol Specification](../001-tunnel-core-server/contracts/agent-protocol.md)
- [Server Implementation](../../src/server/)
- [HTTP/2 RFC 7540](https://tools.ietf.org/html/rfc7540)
- [nghttp2 Documentation](https://nghttp2.org/)
