# Tunnel Agent Specification

**Feature ID**: 002-tunnel-agent  
**Version**: 1.0.0  
**Created**: 2025-11-23  
**Status**: Planning

---

## Overview

The Tunnel Agent is a client application that establishes an outbound TLS connection to the Protogate server and forwards HTTP/TCP traffic from the server to local services. This enables secure remote access to services behind firewalls and NATs without requiring inbound port forwarding.

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
│                  Tunnel Agent                       │
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
- Requests are forwarded to configured local service (e.g., localhost:3000)
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
- Auto-reconnect with exponential backoff (1s, 2s, 4s, ... max 60s)
- Connection state logged

### US4: Configuration
**As a** developer  
**I want** to configure the agent via CLI or config file  
**So that** setup is flexible

**Acceptance Criteria**:
- CLI flags: `--server`, `--token`, `--local-url`, `--tunnel-id`
- Config file support (YAML or JSON)
- Environment variables supported
- Defaults: server=localhost:8443, local-url=http://localhost:3000

---

## Technical Requirements

### Language & Stack

**Option 1: Python** (Recommended for MVP)
- Pros: Fast development, good HTTP/2 libraries (h2, httpx)
- Cons: Performance overhead, deployment size
- Libraries: `h2`, `httpx`, `ssl`, `asyncio`

**Option 2: Go**
- Pros: Single binary, excellent HTTP/2 support, fast
- Cons: Slightly longer development time
- Libraries: `net/http`, `crypto/tls`, `golang.org/x/net/http2`

**Option 3: Node.js**
- Pros: Good HTTP/2 support, npm ecosystem
- Cons: Runtime dependency
- Libraries: `http2`, `https`, `axios`

**Decision**: Start with **Python** for rapid prototyping, migrate to Go for production if needed.

### Dependencies

**Python**:
```
h2==4.1.0           # HTTP/2 protocol
httpx==0.25.0       # HTTP client
asyncio             # Async I/O
ssl                 # TLS support
pyyaml==6.0.1       # Config file parsing
click==8.1.7        # CLI framework
```

### Configuration Schema

```yaml
# tunnel-agent.yaml
server:
  host: tunnel-agent.tunnel.mycorp.com
  port: 8443
  verify_tls: true

tunnel:
  id: api
  token: tnl_abc123...

local:
  url: http://localhost:3000
  timeout: 1800  # 30 minutes in seconds

health:
  heartbeat_interval: 30
  heartbeat_timeout: 60

reconnect:
  initial_delay: 1
  max_delay: 60
  max_attempts: 0  # 0 = infinite
```

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

1. **Server sends request** on HTTP/2 stream:
   ```
   :method: GET
   :path: /users
   :scheme: https
   :authority: api.tunnel.mycorp.com
   x-tunnel-request-id: 550e8400-e29b-41d4-a716-446655440000
   x-client-ip: 203.0.113.45
   content-type: application/json
   
   {"query":"search"}
   ```

2. **Agent forwards to local service**:
   ```python
   response = httpx.get('http://localhost:3000/users', 
                       headers={'x-client-ip': '203.0.113.45', ...},
                       json={"query":"search"})
   ```

3. **Agent sends response** back on same stream:
   ```
   :status: 200
   content-type: application/json
   
   {"users":[...]}
   ```

### Error Handling

**Local Service Down**:
```
:status: 502
x-tunnel-error: LOCAL_SERVICE_UNAVAILABLE
x-error-message: Connection to localhost:3000 refused
```

**Timeout**:
```
:status: 504
x-tunnel-error: REQUEST_TIMEOUT
x-error-message: Local service timeout after 30m
```

---

## File Structure

```
tunnel-agent/
├── README.md
├── requirements.txt
├── setup.py
├── tunnel-agent.yaml.example
├── agent/
│   ├── __init__.py
│   ├── main.py           # Entry point, CLI
│   ├── config.py         # Configuration loading
│   ├── client.py         # TLS + HTTP/2 client
│   ├── forwarder.py      # Request forwarding logic
│   ├── heartbeat.py      # Heartbeat manager
│   └── reconnect.py      # Reconnection logic
├── tests/
│   ├── test_client.py
│   ├── test_forwarder.py
│   └── test_config.py
└── scripts/
    ├── build.sh
    └── install.sh
```

---

## Development Phases

### Phase 1: Basic Connection (Week 1)
- TLS connection to server
- HTTP/2 CONNECT handshake
- Token authentication
- Connection status logging

### Phase 2: Request Forwarding (Week 1)
- Receive HTTP/2 requests
- Forward to local HTTP service
- Return responses
- Basic error handling

### Phase 3: Health & Reconnection (Week 2)
- Heartbeat implementation
- Connection monitoring
- Auto-reconnect with backoff
- Graceful shutdown

### Phase 4: Configuration & CLI (Week 2)
- Config file parsing
- CLI interface
- Environment variables
- Help/version commands

### Phase 5: Testing & Documentation (Week 3)
- Unit tests
- Integration tests with real server
- Performance testing
- User documentation

---

## Success Metrics

- **Connection Success Rate**: >99.9%
- **Request Latency**: <50ms overhead vs direct connection
- **Reconnection Time**: <5 seconds for 95th percentile
- **Memory Usage**: <50MB resident
- **Test Coverage**: >80%

---

## Security Considerations

1. **Token Storage**: Never log full token, only hash
2. **TLS Verification**: Verify server certificate by default
3. **Request Validation**: Sanitize headers before forwarding
4. **Rate Limiting**: Respect X-RateLimit headers from server
5. **Secrets**: Support reading token from file or env var

---

## Future Enhancements

1. **TCP Tunneling**: Support raw TCP connections
2. **Multi-Target**: Single agent handles multiple local services
3. **Metrics**: Expose Prometheus metrics endpoint
4. **UI**: Web dashboard for agent status
5. **Plugin System**: Custom request/response transformations

---

## References

- [Agent Protocol Specification](../001-tunnel-core-server/contracts/agent-protocol.md)
- [Server Architecture](../001-tunnel-core-server/plan.md)
- [HTTP/2 RFC 7540](https://tools.ietf.org/html/rfc7540)
