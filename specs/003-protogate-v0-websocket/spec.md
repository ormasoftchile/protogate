# Feature Specification: ProtoGate v0 - WebSocket-Based Tunnel Fabric

**Feature ID**: 003-protogate-v0-websocket  
**Created**: 2025-11-29  
**Status**: Draft  
**Priority**: P0 (Foundation)

---

## Executive Summary

ProtoGate v0 is a minimal but robust **internal tunnel/agent fabric** built in C++20. It enables secure communication between cloud-hosted servers and on-premise/edge agents through WebSocket-based tunnels. The system supports basic HTTP/TCP forwarding and a JSON control protocol for agent management and job distribution.

**Core Value**: Reusable infrastructure for vertical products (thermal printing, queuing systems, bug-bash tools) without building a full-featured public tunnel service.

**Key Constraint**: v0 focuses on correctness, simplicity, and clarity over features. No UI, no complex auth, no performance optimization.

---

## Context

### What We're Building

A two-component system:

1. **protogate-server** (cloud/VM):
   - Accepts WebSocket connections from agents
   - Maintains agent registry
   - Forwards tunnel traffic between clients and agents
   - Sends job messages to agents

2. **protogate-agent** (on-prem/edge):
   - Establishes outbound WebSocket connection to server
   - Registers with agent ID and capabilities
   - Opens local TCP connections to target services
   - Forwards data through multiplexed streams
   - Handles job messages

### What We're NOT Building (v0 Scope)

- ❌ User-facing web UI or admin portal
- ❌ OAuth/OpenID or complex authentication
- ❌ Dynamic nginx configuration management
- ❌ Performance tuning or advanced multiplexing
- ❌ UDP or non-TCP protocols
- ❌ Multi-tenant isolation
- ❌ Public marketplace features

### Current Status

- ✅ Clean slate: Old HTTP/2 implementation removed
- ✅ Fresh directory structure created
- ✅ Ready for WebSocket-based implementation
- ⚠️ Previous attempt backed up in `backups/websocket-attempt-20251129-214938/`

---

## Goals

### Primary Goals

1. **Agent Registration**: Agents connect to server, authenticate with pre-shared token, and register with unique ID
2. **Persistent Connection**: Maintain long-lived WebSocket connection with automatic reconnect
3. **Stream Multiplexing**: Support multiple logical tunnels over single WebSocket connection
4. **TCP Forwarding**: Forward TCP traffic between tunnel clients → server → agent → local service
5. **Job Protocol**: Send JSON job messages to agents and receive results

### Success Criteria

1. Agent can connect to server and register successfully
2. Agent maintains connection with heartbeat mechanism
3. Client can connect to server's tunnel port and reach agent's local service
4. HTTP request/response flows correctly through tunnel
5. Job messages are delivered to agent and results returned
6. System handles graceful shutdown and reconnection
7. Code is clear, documented, and ready for extension

### Non-Goals (Deferred to Future Versions)

- Advanced error recovery and retry logic
- Performance benchmarking or optimization
- Load balancing across multiple agents
- Dynamic tunnel creation via API
- Metrics and observability beyond basic logging

---

## Architecture Overview

### High-Level Components

```
┌─────────────────┐                    ┌──────────────────┐
│  Tunnel Client  │                    │  ProtoGate Agent │
│  (Browser/Tool) │                    │  (On-Prem/Edge)  │
└────────┬────────┘                    └─────────┬────────┘
         │                                       │
         │ TCP Connection                        │ WebSocket/TLS
         │                                       │
         ▼                                       ▼
┌─────────────────────────────────────────────────────────┐
│              ProtoGate Server (Cloud/VM)                │
│  ┌──────────────────────────────────────────────────┐   │
│  │  AgentConnectionManager                          │   │
│  │  - Accepts WebSocket connections                 │   │
│  │  - Authenticates agents                          │   │
│  │  - Routes control messages                       │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │  AgentRegistry                                   │   │
│  │  - Stores connected agents                       │   │
│  │  - Maps agent_id → connection                    │   │
│  │  - Tracks capabilities                           │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │  TunnelManager                                   │   │
│  │  - Listens on TCP port for clients               │   │
│  │  - Assigns stream_id to connections              │   │
│  │  - Routes data frames                            │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │  ControlMessageHandler                           │   │
│  │  - Parses JSON control messages                  │   │
│  │  - Handles register/heartbeat/job_result         │   │
│  │  - Sends open_tunnel/close_tunnel/job            │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### Protocol Design

#### Control Messages (JSON over WebSocket Text Frames)

**Agent → Server:**
```json
{
  "type": "register",
  "agent_id": "agent-123",
  "secret": "shared-secret-token",
  "capabilities": ["tunnel", "jobs"]
}

{
  "type": "heartbeat",
  "agent_id": "agent-123"
}

{
  "type": "job_result",
  "job_id": "abc123",
  "status": "ok",
  "result": {"details": "printed 5 pages"}
}
```

**Server → Agent:**
```json
{
  "type": "open_tunnel",
  "stream_id": 123,
  "target_host": "127.0.0.1",
  "target_port": 3000
}

{
  "type": "close_tunnel",
  "stream_id": 123,
  "reason": "client_closed"
}

{
  "type": "job",
  "job_id": "abc123",
  "job_type": "print",
  "payload": {"document": "invoice.pdf", "copies": 2}
}
```

#### Data Frames (Binary over WebSocket Binary Frames)

```
┌──────────────────┬────────────────────────┐
│  stream_id (4B)  │  payload (variable)    │
│  (network order) │  (raw TCP data)        │
└──────────────────┴────────────────────────┘
```

- **stream_id**: 32-bit unsigned integer, big-endian
- **payload**: Raw bytes from TCP connection

### Communication Flow

#### 1. Agent Startup and Registration

```
Agent                           Server
  │                               │
  ├──── WebSocket Connect ───────>│
  │                               │
  ├──── register (JSON) ─────────>│
  │                               │<─── Validate token
  │                               │<─── Add to AgentRegistry
  │                               │
  │<──── register_ack (JSON) ─────┤
  │                               │
  ├──── heartbeat (periodic) ────>│
  │                               │
```

#### 2. Tunnel Connection Flow

```
Client          Server                    Agent              Local Service
  │               │                         │                      │
  ├─ TCP Connect >│                         │                      │
  │               ├── Assign stream_id=456  │                      │
  │               │                         │                      │
  │               ├─ open_tunnel(456) ─────>│                      │
  │               │                         ├─ TCP Connect ───────>│
  │               │                         │                      │
  │               │                         │<─ Accept ────────────┤
  │               │<── ack(456) ────────────┤                      │
  │               │                         │                      │
  ├─ HTTP Request >│                         │                      │
  │               ├─ data(456, bytes) ─────>│                      │
  │               │                         ├─ Forward ───────────>│
  │               │                         │                      │
  │               │                         │<─ HTTP Response ─────┤
  │               │<── data(456, bytes) ────┤                      │
  │<─ HTTP Response┤                         │                      │
```

#### 3. Job Message Flow

```
Server                          Agent
  │                               │
  ├──── job (JSON) ─────────────>│
  │                               │<─── Parse job
  │                               │<─── Execute (stub)
  │                               │
  │<──── job_result (JSON) ───────┤
  │                               │
```

---

## Functional Requirements

### FR1: Agent Registration and Authentication

**Priority**: P0  
**User Story**: As a system, I need agents to authenticate before accepting connections.

**Requirements**:
- Agent MUST send `register` message with `agent_id` and `secret`
- Server MUST validate `secret` against pre-shared token in config
- Server MUST reject connection if token is invalid
- Server MUST store agent in `AgentRegistry` on successful registration
- Server MUST send `register_ack` or `register_error` response

**Acceptance Criteria**:
- Valid token → agent registered, connection maintained
- Invalid token → connection closed with error message
- Duplicate `agent_id` → error (only one active connection per agent)

---

### FR2: Persistent Connection with Heartbeat

**Priority**: P0  
**User Story**: As a system, I need to detect agent disconnections quickly.

**Requirements**:
- Agent MUST send `heartbeat` message every 30 seconds (configurable)
- Server MUST track last heartbeat timestamp per agent
- Server MUST remove agent from registry if no heartbeat for 90 seconds
- Agent MUST implement automatic reconnection on disconnect

**Acceptance Criteria**:
- Heartbeat received → timestamp updated
- Missed heartbeats → agent removed after timeout
- Network interruption → agent reconnects within 10 seconds

---

### FR3: Stream Multiplexing

**Priority**: P0  
**User Story**: As a system, I need to support multiple tunnels over one connection.

**Requirements**:
- Server MUST assign unique `stream_id` for each client connection
- Data frames MUST include `stream_id` prefix (4 bytes, network order)
- Server MUST route data frames to correct agent based on `stream_id`
- Agent MUST route data frames to correct local connection based on `stream_id`
- `stream_id` MUST be released when tunnel closes

**Acceptance Criteria**:
- Multiple concurrent connections work independently
- Data doesn't mix between streams
- stream_id values are unique and reused after close

---

### FR4: Tunnel Opening and Closing

**Priority**: P0  
**User Story**: As a client, I need my TCP connection to reach the agent's local service.

**Requirements**:
- Server MUST send `open_tunnel` when client connects
- Agent MUST open local TCP connection to configured `target_host:target_port`
- Agent MUST send error if local connection fails
- Server MUST close client connection if agent connection fails
- Either side MUST send `close_tunnel` when connection closes
- Both sides MUST clean up resources on `close_tunnel`

**Acceptance Criteria**:
- Client connects → `open_tunnel` sent → local connection opened
- Local connection fails → client receives error
- Client disconnects → `close_tunnel` sent → local connection closed
- Agent disconnects → all client connections closed

---

### FR5: TCP Data Forwarding

**Priority**: P0  
**User Story**: As a client, I need my data to reach the target service through the tunnel.

**Requirements**:
- Server MUST read from client TCP socket and send as data frame to agent
- Agent MUST read data frames and write to local TCP socket
- Agent MUST read from local TCP socket and send as data frame to server
- Server MUST read data frames and write to client TCP socket
- Both sides MUST handle partial reads/writes correctly
- Both sides MUST handle backpressure (slow reader)

**Acceptance Criteria**:
- HTTP request flows: client → server → agent → local service
- HTTP response flows: local service → agent → server → client
- Large transfers (>1MB) work correctly
- Slow connections don't block other streams

---

### FR6: Job Message Protocol

**Priority**: P1  
**User Story**: As a system, I need to send commands to agents and get results.

**Requirements**:
- Server MUST send `job` message with unique `job_id`
- Agent MUST receive job, log payload (v0 stub implementation)
- Agent MUST send `job_result` with same `job_id`
- Server MUST match result to original job request
- Server MUST timeout jobs after 60 seconds (configurable)

**Acceptance Criteria**:
- Job sent → agent logs payload → result returned
- Job timeout → error returned to caller
- Multiple jobs can be in-flight simultaneously

---

### FR7: Configuration Management

**Priority**: P0  
**User Story**: As an operator, I need to configure server and agent via files.

**Requirements**:
- Server MUST read config from YAML/JSON file
- Agent MUST read config from YAML/JSON file
- Config MUST support command-line overrides
- Server config MUST include: listen ports, shared secret, log level
- Agent config MUST include: server URL, agent ID, secret, tunnels, log level

**Example Agent Config**:
```yaml
server_url: "wss://protogate.example.com/agent"
agent_id: "store-123-agent1"
shared_secret: "SECURE_TOKEN_HERE"
log_level: "info"
heartbeat_interval: 30
tunnels:
  - id: "dev-app"
    local_host: "127.0.0.1"
    local_port: 3000
```

**Example Server Config**:
```yaml
agent_endpoint: "/agent"
agent_port: 8080
tunnel_port: 9000
shared_secret: "SECURE_TOKEN_HERE"
log_level: "info"
heartbeat_timeout: 90
```

**Acceptance Criteria**:
- Config file parsed correctly
- Invalid config → clear error message
- Command-line flags override config values

---

### FR8: Error Handling and Logging

**Priority**: P0  
**User Story**: As an operator, I need clear logs to diagnose issues.

**Requirements**:
- Both components MUST log to stdout in structured format (timestamp, level, message)
- Log levels: DEBUG, INFO, WARN, ERROR
- MUST log: agent connect/disconnect, tunnel open/close, errors
- MUST include context: agent_id, stream_id, error details
- MUST NOT log sensitive data (tokens, payloads by default)

**Acceptance Criteria**:
- Agent connection logged with agent_id
- Tunnel events logged with stream_id
- Errors include stack context
- Log level configurable

---

## Key Entities

### Agent

**Attributes**:
- `agent_id` (string): Unique identifier
- `secret` (string): Pre-shared authentication token
- `capabilities` (array): e.g., ["tunnel", "jobs"]
- `last_heartbeat` (timestamp): Last heartbeat received
- `connection` (WebSocket): Active connection handle

**Lifecycle**:
1. Created: On successful registration
2. Active: While heartbeats received
3. Removed: On disconnect or timeout

---

### Stream

**Attributes**:
- `stream_id` (uint32): Unique tunnel identifier
- `agent_id` (string): Associated agent
- `client_socket` (TCP): Connection to tunnel client
- `state` (enum): opening, active, closing, closed

**Lifecycle**:
1. Created: When client connects to tunnel port
2. Active: After `open_tunnel` acknowledged
3. Closed: When either side disconnects

---

### Job

**Attributes**:
- `job_id` (string): Unique identifier (UUID)
- `job_type` (string): e.g., "print", "compute"
- `payload` (JSON): Job-specific data
- `status` (enum): pending, sent, completed, failed, timeout
- `result` (JSON): Response data
- `created_at` (timestamp): Job creation time

**Lifecycle**:
1. Created: When server sends job to agent
2. Sent: Job delivered to agent
3. Completed/Failed: Result received or timeout

---

## User Scenarios & Testing

### Scenario 1: Agent Startup

**Given**: Server running, agent configured  
**When**: Agent starts  
**Then**:
- Agent connects to WebSocket endpoint
- Agent sends register message
- Server validates token
- Server responds with register_ack
- Agent starts heartbeat timer

**Test**:
```bash
# Start server
./protogate-server --config server.yaml

# Start agent
./protogate-agent --config agent.yaml

# Verify logs show:
# [Server] Agent agent-123 registered
# [Agent] Registration successful
# [Agent] Heartbeat started
```

---

### Scenario 2: HTTP Tunneling

**Given**: Server and agent running, agent registered  
**When**: HTTP client connects to tunnel port  
**Then**:
- Server accepts client connection
- Server assigns stream_id
- Server sends open_tunnel to agent
- Agent opens local connection
- HTTP request flows through tunnel
- HTTP response returned to client

**Test**:
```bash
# Start local HTTP server on agent machine
python3 -m http.server 3000

# Connect from tunnel client
curl http://protogate-server:9000/

# Verify: Directory listing from local server
```

---

### Scenario 3: Job Execution

**Given**: Agent connected and registered  
**When**: Server sends job message  
**Then**:
- Agent receives job
- Agent logs job payload
- Agent sends job_result
- Server receives result

**Test**:
```bash
# Send job via server API (future) or trigger internally
# For v0: Trigger job send in server code

# Verify logs show:
# [Server] Sent job abc123 to agent-123
# [Agent] Received job abc123: {"type":"print"}
# [Agent] Sent job_result abc123: {"status":"ok"}
# [Server] Received job_result abc123
```

---

### Scenario 4: Agent Reconnection

**Given**: Agent connected  
**When**: Network interruption occurs  
**Then**:
- Agent detects disconnect
- Agent attempts reconnection with exponential backoff
- Agent re-registers on reconnect
- Existing tunnels are closed gracefully

**Test**:
```bash
# Kill network or restart server
sudo ifconfig <interface> down && sleep 5 && sudo ifconfig <interface> up

# Verify logs show:
# [Agent] Connection lost, reconnecting...
# [Agent] Reconnected, re-registering
# [Server] Agent agent-123 re-registered
```

---

## Success Criteria

### Must Have (v0 MVP)

1. ✅ Agent registration with token validation
2. ✅ Persistent WebSocket connection with heartbeat
3. ✅ Stream multiplexing (multiple tunnels per connection)
4. ✅ HTTP request/response tunneling works end-to-end
5. ✅ Job message send and stub response
6. ✅ Graceful shutdown handling
7. ✅ Basic structured logging
8. ✅ Configuration via YAML files
9. ✅ README with build and run instructions
10. ✅ Manual integration test passes

### Should Have (v0.5)

- Automatic reconnection with exponential backoff
- Job timeout handling
- Better error messages and logging
- Unit tests for protocol parsing

### Could Have (v1)

- Metrics and observability
- Multiple target services per agent
- Dynamic tunnel creation API
- Agent capabilities negotiation
- TLS certificate validation

---

## Assumptions

1. **Network**: Agents have outbound internet access to reach server
2. **Firewall**: WebSocket connection (port 8080) allowed through agent firewall
3. **TLS**: nginx or similar terminates TLS in production (server accepts plain WebSocket in v0)
4. **Single Agent Instance**: No HA or clustering for v0
5. **Token Management**: Pre-shared tokens distributed manually
6. **Stream IDs**: Uint32 provides enough range (4 billion concurrent connections)
7. **Platform**: Linux x86_64 primary target (macOS for development)

---

## Dependencies

### External Dependencies

- **Boost.Beast** (1.84+): WebSocket and HTTP implementation
- **Boost.Asio** (1.84+): Async I/O, TCP sockets
- **nlohmann/json** (3.11+): JSON parsing and serialization
- **yaml-cpp** (0.8+): YAML configuration parsing
- **spdlog** (1.12+): Structured logging

### Build Dependencies

- CMake 3.20+
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- OpenSSL 3.0+ (for TLS if needed)

---

## Out of Scope

### Explicitly Deferred

- **Web UI**: No admin portal or dashboard
- **Multi-Tenancy**: No user isolation or quota management
- **Dynamic Configuration**: No runtime config updates
- **Advanced Routing**: No load balancing or failover
- **Protocol Extensions**: No UDP, QUIC, or other transports
- **Performance Optimization**: No zero-copy or kernel bypass
- **Monitoring Integration**: No Prometheus/Grafana integration
- **Database**: No persistent storage (in-memory only)
- **API Server**: No REST API for tunnel management

---

## Technical Notes

### WebSocket Frame Format

**Text Frame** (Control Messages):
```
WebSocket Text Frame
└─ JSON payload
```

**Binary Frame** (Data):
```
WebSocket Binary Frame
├─ stream_id (4 bytes, network order)
└─ TCP data (variable length)
```

### Stream ID Management

- Server maintains `next_stream_id` counter (starts at 1)
- Increment on each new client connection
- Wrap around at UINT32_MAX (unlikely in v0)
- Reuse immediately after close (simple implementation)

### Error Handling Strategy

1. **Protocol Errors**: Close WebSocket connection with error code
2. **TCP Errors**: Send close_tunnel message, close local connection
3. **Timeout Errors**: Log and clean up resources
4. **Resource Errors**: Reject new connections if limits reached (future)

### Concurrency Model

- **Server**: One thread per component (agent listener, tunnel listener, control)
- **Agent**: Single-threaded async I/O with Asio
- **Synchronization**: Mutex-protected shared state (AgentRegistry, stream map)

---

## Questions & Clarifications

### Open Questions

1. **Q**: Should server enforce maximum streams per agent?  
   **A**: No for v0. Assume reasonable usage.

2. **Q**: How to handle partial JSON messages on WebSocket?  
   **A**: WebSocket provides message boundaries. One message = one JSON object.

3. **Q**: Should agent support multiple tunnel targets?  
   **A**: Yes, configured in YAML. Server sends target_host/port in open_tunnel.

4. **Q**: What happens if local connection fails during tunnel open?  
   **A**: Agent sends close_tunnel with error reason. Server closes client connection.

5. **Q**: Should we implement TLS on server side?  
   **A**: Optional for v0. Assume nginx termination in production. Support plain WS for testing.

---

## References

- **WebSocket Protocol**: RFC 6455
- **Boost.Beast Documentation**: https://www.boost.org/doc/libs/1_84_0/libs/beast/doc/html/index.html
- **nlohmann/json**: https://json.nlohmann.me/
- **Previous Implementation**: `backups/websocket-attempt-20251129-214938/` (reference for what NOT to do)

---

## Appendix: Message Schema Reference

### Control Message Types

| Type | Direction | Required Fields | Optional Fields |
|------|-----------|----------------|-----------------|
| `register` | Agent → Server | `type`, `agent_id`, `secret`, `capabilities` | - |
| `register_ack` | Server → Agent | `type`, `success` | `message` |
| `heartbeat` | Agent → Server | `type`, `agent_id` | - |
| `open_tunnel` | Server → Agent | `type`, `stream_id`, `target_host`, `target_port` | - |
| `close_tunnel` | Bidirectional | `type`, `stream_id` | `reason` |
| `job` | Server → Agent | `type`, `job_id`, `job_type`, `payload` | - |
| `job_result` | Agent → Server | `type`, `job_id`, `status`, `result` | - |

### Error Codes

| Code | Description |
|------|-------------|
| 4001 | Invalid token |
| 4002 | Agent ID already registered |
| 4003 | Invalid message format |
| 4004 | Stream ID not found |
| 4005 | Local connection failed |
| 4006 | Heartbeat timeout |

---

**End of Specification**
