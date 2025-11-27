# Feature Specification: Management API and HTTP Proxy

**Feature Branch**: `002-management-api-http-proxy`  
**Created**: 2025-11-27  
**Status**: Draft  
**Parent**: `001-tunnel-core-server` (Complete)  
**Input**: Enable end-to-end HTTP tunneling from internet → agent → local service

## Context

**Current State**:
- ✅ Server deployed to Azure Container Apps (protogate-dev-app)
- ✅ Tunnel agent fully implemented (tunnel-agent/, 100% complete)
- ✅ Agent protocol working (TLS, HTTP/2, ALPN, authentication)
- ✅ AgentConnection accepts agents and maintains HTTP/2 sessions
- ✅ AgentRegistry tracks connected agents

**Gaps**:
- ❌ Management API (POST/GET/DELETE /v1/tunnels)
- ❌ HTTP Proxy (route internet traffic → agents based on Host header)
- ❌ Token generation and storage in Azure Key Vault
- ❌ DNS integration for wildcard domain routing

**Goal**: Complete the HTTP tunneling use case (User Story 1 from spec 001)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Tunnel Registration via Management API (Priority: P0)

**Actor**: DevOps engineer  
**Context**: Need to register a new tunnel before agents can connect

**Scenario**: An engineer calls `POST /v1/tunnels` with tunnel configuration (tunnel_id: "my-api", protocol: "HTTP", local_url: "http://localhost:3000"). The server generates a secure token, stores tunnel metadata in memory/database, and returns the token. The engineer configures the tunnel agent with this token and starts it.

**Why this priority**: Without this, there's no way to register tunnels. This is the prerequisite for all other functionality.

**Independent Test**: 
```bash
curl -X POST https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{"tunnel_id": "test-api", "protocol": "HTTP", "local_url": "http://localhost:3000"}' \
&& echo "Token: tnl_abc123..."
```

**Acceptance Scenarios**:

1. **Given** server is running, **When** POST `/v1/tunnels` with valid JSON body, **Then** server returns HTTP 201 with `{"tunnel_id": "test-api", "token": "tnl_...", "status": "registered", "agent_status": "disconnected"}`
2. **Given** tunnel already exists with same ID, **When** POST `/v1/tunnels` with duplicate ID, **Then** server returns HTTP 409 Conflict with error message
3. **Given** invalid JSON body (missing tunnel_id), **When** POST `/v1/tunnels`, **Then** server returns HTTP 400 Bad Request with validation errors
4. **Given** tunnel registered, **When** GET `/v1/tunnels/test-api`, **Then** server returns tunnel details including creation time, agent status, and connection count

---

### User Story 2 - HTTP Request Proxying (Priority: P0)

**Actor**: Internet client (web browser, API consumer)  
**Context**: Agent is connected, tunnel is registered, client wants to access local service

**Scenario**: A registered tunnel "my-api" has an agent connected. Client sends `GET https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/api/users` with `Host: my-api` header. Server looks up tunnel by Host header, finds connected agent, forwards request via HTTP/2 stream, receives response from agent, and returns it to client.

**Why this priority**: This is the core value proposition - enabling HTTP access to local services. Without this, the system doesn't work.

**Independent Test**:
```bash
# Terminal 1: Start agent
cd tunnel-agent && ./build/tunnel-agent --tunnel-id my-api --tunnel-token tnl_abc123

# Terminal 2: Test proxy
curl -H "Host: my-api" https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/api/users
# Expected: Response from localhost:3000/api/users
```

**Acceptance Scenarios**:

1. **Given** agent connected for tunnel "my-api" forwarding to localhost:3000, **When** client sends `GET /api/users` with `Host: my-api`, **Then** request routed to agent, forwarded to localhost:3000, response returned to client with HTTP 200
2. **Given** agent connected, **When** client sends POST with body and headers, **Then** all headers (except hop-by-hop) and full body forwarded to local service, response with all headers returned
3. **Given** no agent connected for tunnel "my-api", **When** client sends request with `Host: my-api`, **Then** server returns HTTP 503 Service Unavailable with `{"error": "tunnel_offline", "tunnel_id": "my-api"}`
4. **Given** agent connected, local service returns 404, **When** client sends request, **Then** server returns HTTP 404 with original response body from local service
5. **Given** agent connected, request takes 5 seconds, **When** client sends request, **Then** response returned within 5.1 seconds (minimal overhead)
6. **Given** agent connected, **When** 50 concurrent requests sent to same tunnel, **Then** all requests complete successfully with correct responses

---

### User Story 3 - Tunnel Listing and Monitoring (Priority: P1)

**Actor**: DevOps engineer, monitoring system  
**Context**: Need visibility into registered tunnels and agent status

**Scenario**: Engineer calls `GET /v1/tunnels` to see all registered tunnels. Response shows tunnel IDs, protocols, agent connection status, last seen timestamp, and traffic metrics (requests/bytes). Engineer identifies an offline tunnel and investigates.

**Why this priority**: Essential for operations and troubleshooting. Without this, can't monitor system health.

**Independent Test**:
```bash
curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels
# Expected: JSON array of all tunnels with status
```

**Acceptance Scenarios**:

1. **Given** 3 tunnels registered (2 with agents connected, 1 offline), **When** GET `/v1/tunnels`, **Then** server returns array with all 3 tunnels showing correct agent_status
2. **Given** tunnel has agent connected, **When** GET `/v1/tunnels/{id}`, **Then** response includes agent metadata (connected_at, last_heartbeat, bytes_sent, bytes_received)
3. **Given** tunnel exists, **When** GET `/v1/tunnels/{id}/agents`, **Then** response lists all currently connected agents for that tunnel (supports multiple agents per tunnel)
4. **Given** tunnel doesn't exist, **When** GET `/v1/tunnels/nonexistent`, **Then** server returns HTTP 404 Not Found

---

### User Story 4 - Tunnel Deletion (Priority: P1)

**Actor**: DevOps engineer  
**Context**: Decommission old tunnel, clean up test tunnels

**Scenario**: Engineer calls `DELETE /v1/tunnels/old-api` to remove tunnel. Server disconnects any connected agents, removes tunnel metadata, and invalidates token. Future agent connection attempts with that token are rejected.

**Why this priority**: Important for operational hygiene and security (revoke tokens). Not critical for MVP.

**Independent Test**:
```bash
curl -X DELETE https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels/old-api
# Expected: HTTP 204 No Content
```

**Acceptance Scenarios**:

1. **Given** tunnel exists with no connected agents, **When** DELETE `/v1/tunnels/{id}`, **Then** server returns HTTP 204, tunnel removed from registry, token invalidated
2. **Given** tunnel exists with 2 connected agents, **When** DELETE `/v1/tunnels/{id}`, **Then** server gracefully disconnects agents (GOAWAY), removes tunnel, agents receive disconnect notification
3. **Given** tunnel deleted, **When** agent attempts to connect with old token, **Then** server returns 401 Unauthorized and connection rejected
4. **Given** tunnel doesn't exist, **When** DELETE `/v1/tunnels/{id}`, **Then** server returns HTTP 404 Not Found

---

### User Story 5 - Token Rotation (Priority: P2)

**Actor**: Security engineer  
**Context**: Periodic token rotation for security compliance

**Scenario**: Engineer calls `POST /v1/tunnels/{id}/rotate-token` to generate new token. Server creates new token, maintains old token validity for 5-minute grace period, returns new token. Engineer updates agent configuration with new token within grace period. After 5 minutes, old token stops working.

**Why this priority**: Important for security but not required for MVP. Can be added later.

**Independent Test**:
```bash
curl -X POST https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels/my-api/rotate-token
# Expected: {"old_token": "tnl_abc...", "new_token": "tnl_xyz...", "grace_period_seconds": 300}
```

**Acceptance Scenarios**:

1. **Given** tunnel exists with agent connected using token A, **When** POST `/v1/tunnels/{id}/rotate-token`, **Then** new token B generated, agent with token A remains connected, new agents can use token B
2. **Given** token rotated 4 minutes ago, **When** new agent connects with old token, **Then** connection succeeds (within grace period)
3. **Given** token rotated 6 minutes ago, **When** new agent connects with old token, **Then** connection rejected with 401 Unauthorized
4. **Given** grace period active, **When** agent with old token disconnects and reconnects, **Then** server logs warning "using deprecated token" but allows connection

---

## Technical Requirements *(mandatory)*

### Functional Requirements

**Management API Endpoints**:
- `POST /v1/tunnels` - Create tunnel, generate token
- `GET /v1/tunnels` - List all tunnels with status
- `GET /v1/tunnels/{id}` - Get tunnel details
- `GET /v1/tunnels/{id}/agents` - List connected agents for tunnel
- `DELETE /v1/tunnels/{id}` - Delete tunnel and disconnect agents
- `POST /v1/tunnels/{id}/rotate-token` - Generate new token (P2)

**HTTP Proxy Requirements**:
- Parse `Host` header from incoming HTTP requests
- Lookup tunnel by Host value (e.g., `Host: my-api` → tunnel_id: "my-api")
- Find connected agent for tunnel (support multiple agents, use round-robin)
- Forward request to agent via HTTP/2 stream using `AgentConnection::send_http_request()`
- Timeout: 30 seconds per request
- Return response to client with original status code and headers
- Handle errors: 503 if no agent, 502 if agent error, 504 if timeout

**Token Management**:
- Generate cryptographically secure tokens (256-bit, prefix "tnl_")
- Store tokens in-memory (Phase 1) or Azure Key Vault (Phase 2)
- Validate tokens on agent connection
- Support token rotation with grace period

**Data Models**:
```cpp
struct Tunnel {
    std::string tunnel_id;        // "my-api"
    std::string protocol;          // "HTTP" or "TCP"
    std::string local_url;         // "http://localhost:3000"
    std::string token;             // "tnl_abc123..."
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    
    // Runtime state (not persisted)
    bool has_connected_agent;
    int active_connection_count;
    uint64_t total_requests;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
};
```

### Non-Functional Requirements

**Performance**:
- Management API: <100ms p99 latency
- HTTP Proxy: <10ms overhead (server-side processing)
- Support 100+ concurrent tunnels
- Support 1000+ requests/second aggregate

**Scalability**:
- In-memory storage for Phase 1 (single instance)
- Design for external storage (Redis/Cosmos DB) in Phase 2
- Support multiple replicas with shared state

**Security**:
- Token entropy: 256 bits minimum
- Validate all input (JSON schema validation)
- Rate limiting: 100 req/min per tunnel for Management API
- Audit logging: All tunnel creation/deletion/rotation events

**Reliability**:
- Graceful degradation: If agent disconnects mid-request, return 503
- Request timeout: 30 seconds, configurable
- Health checks: Management API included in `/health` endpoint

**Observability**:
- Structured logging: All API calls, proxy decisions, errors
- Metrics: tunnel count, agent count, request rate, error rate
- Distributed tracing: Request ID propagation

---

## Architecture & Dependencies *(optional)*

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│ Azure Container Apps - Protogate Server                     │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ Azure Ingress (HTTPS on :443)                          │ │
│  └───────────────────────┬────────────────────────────────┘ │
│                          │ HTTP (TLS terminated)            │
│                          ▼                                   │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ HTTPServer (port 8080)                                 │ │
│  │                                                         │ │
│  │  ┌──────────────────────────────────────────────────┐ │ │
│  │  │ ManagementAPI                                     │ │ │
│  │  │ - POST /v1/tunnels (register)                    │ │ │
│  │  │ - GET /v1/tunnels (list)                         │ │ │
│  │  │ - GET /v1/tunnels/{id} (details)                 │ │ │
│  │  │ - DELETE /v1/tunnels/{id} (remove)               │ │ │
│  │  └──────────────────────────────────────────────────┘ │ │
│  │                          │                              │ │
│  │                          ▼                              │ │
│  │  ┌──────────────────────────────────────────────────┐ │ │
│  │  │ TunnelRegistry                                    │ │ │
│  │  │ - Store tunnel metadata                          │ │ │
│  │  │ - Generate/validate tokens                       │ │ │
│  │  │ - Link tunnels ↔ agents                          │ │ │
│  │  └──────────────────────────────────────────────────┘ │ │
│  │                          │                              │ │
│  │                          ▼                              │ │
│  │  ┌──────────────────────────────────────────────────┐ │ │
│  │  │ HTTPProxy                                         │ │ │
│  │  │ - Parse Host header                              │ │ │
│  │  │ - Lookup tunnel by Host                          │ │ │
│  │  │ - Route to AgentConnection                       │ │ │
│  │  │ - Handle errors (503/502/504)                    │ │ │
│  │  └──────────────────────────────────────────────────┘ │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ AgentServer (port 8443)                                │ │
│  │  ├─ AgentHandshake (validates tokens)                 │ │
│  │  ├─ AgentRegistry (tracks connections)                │ │
│  │  └─ AgentConnection (HTTP/2 sessions)     [EXISTING] │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### New Components

**1. ManagementAPI** (`src/api/management_api.h/cpp`):
- REST endpoint handlers using Boost.Beast
- JSON parsing/serialization with nlohmann::json
- Input validation and error handling
- Interacts with TunnelRegistry

**2. TunnelRegistry** (`src/registry/tunnel_registry.h/cpp`):
- Thread-safe tunnel storage (std::unordered_map with mutex)
- Token generation (secure random, Base64 encoding)
- Token validation and lookup
- Link tunnel ↔ agent mappings via AgentRegistry

**3. HTTPProxy** (`src/proxy/http_proxy.h/cpp`):
- Parse incoming HTTP requests
- Extract Host header
- Lookup tunnel in TunnelRegistry
- Find agent via AgentRegistry
- Call `AgentConnection::send_http_request()`
- Build HTTP response from agent's response
- Error handling and timeouts

### Integration Points

**Existing Components**:
- `AgentRegistry` - Already exists, tracks connected agents
- `AgentConnection` - Already exists, has `send_http_request()` method
- `AgentHandshake` - Needs modification to validate tokens via TunnelRegistry
- `HTTPServer` - Needs modification to add ManagementAPI routes and HTTPProxy handler

### Dependencies

**New**:
- None - all required libraries already in project (Boost.Beast, nlohmann::json, OpenSSL)

**Modified**:
- `src/server/http_server.cpp` - Add route handlers for Management API and HTTP Proxy
- `src/agent/agent_handshake.cpp` - Validate tokens against TunnelRegistry instead of hardcoded
- `src/server/main.cpp` - Initialize TunnelRegistry and pass to components

---

## Implementation Plan *(optional)*

### Phase 1: Management API (Days 1-2)

**Files to Create**:
- `src/models/tunnel.h` - Tunnel data structure
- `src/registry/tunnel_registry.h/cpp` - Tunnel storage and token management
- `src/api/management_api.h/cpp` - REST endpoint handlers
- `tests/unit/tunnel_registry_test.cpp` - Unit tests
- `tests/integration/management_api_test.cpp` - Integration tests

**Tasks**:
1. Define `Tunnel` struct in models
2. Implement TunnelRegistry with in-memory storage
3. Add token generation (256-bit random, Base64, "tnl_" prefix)
4. Implement POST /v1/tunnels endpoint
5. Implement GET /v1/tunnels endpoint
6. Implement GET /v1/tunnels/{id} endpoint
7. Implement DELETE /v1/tunnels/{id} endpoint
8. Add JSON serialization/deserialization
9. Add input validation
10. Wire up routes in HTTPServer
11. Write unit tests
12. Write integration tests
13. Test with curl against deployed server

**Success Criteria**:
- All Management API endpoints return correct responses
- Tokens generated with proper entropy
- Can create/list/delete tunnels via API
- Tests pass with >90% coverage

### Phase 2: HTTP Proxy (Days 3-4)

**Files to Create**:
- `src/proxy/http_proxy.h/cpp` - HTTP request routing logic
- `tests/unit/http_proxy_test.cpp` - Unit tests
- `tests/integration/http_proxy_test.cpp` - End-to-end tests

**Tasks**:
1. Implement HTTPProxy class with route method
2. Parse HTTP request, extract Host header
3. Lookup tunnel in TunnelRegistry
4. Find agent in AgentRegistry
5. Format HTTP request for agent (HTTP/1.1 format)
6. Call AgentConnection::send_http_request()
7. Parse agent response, build HTTP response
8. Handle errors: 503 (no agent), 502 (agent error), 504 (timeout)
9. Add timeout handling (30s default)
10. Wire up HTTPProxy in HTTPServer
11. Write unit tests with mock agent
12. Write integration tests with real agent
13. Test end-to-end: curl → server → agent → local service

**Success Criteria**:
- HTTP requests routed correctly based on Host header
- Responses returned with correct status/headers/body
- Error cases handled properly (503/502/504)
- Latency <10ms server overhead
- Integration test: curl → tunnel → agent → echo server

### Phase 3: Token Integration (Day 5)

**Files to Modify**:
- `src/agent/agent_handshake.cpp` - Validate tokens via TunnelRegistry
- `src/server/main.cpp` - Initialize and wire components

**Tasks**:
1. Modify AgentHandshake to accept TunnelRegistry reference
2. Replace hardcoded token validation with TunnelRegistry lookup
3. Extract tunnel_id from validated token
4. Pass tunnel_id to AgentConnection
5. Update AgentRegistry to link tunnel ↔ agent
6. Add audit logging for authentication events
7. Test agent connection with real tokens
8. Test rejection of invalid tokens

**Success Criteria**:
- Agents can connect with tokens from Management API
- Invalid tokens rejected with 401
- AgentRegistry correctly links tunnels to agents
- Audit logs show authentication events

### Phase 4: End-to-End Testing (Day 6)

**Tasks**:
1. Deploy updated server to Azure
2. Build and run tunnel-agent locally
3. Create tunnel via POST /v1/tunnels
4. Start agent with token
5. Send HTTP request to tunnel endpoint
6. Verify response from local service
7. Test error cases (agent offline, timeout, etc.)
8. Load test: 100 concurrent requests
9. Document usage in QUICKSTART.md

**Success Criteria**:
- Complete HTTP tunnel flow working end-to-end
- All acceptance scenarios pass
- Performance meets requirements (<10ms overhead)
- Documentation updated

---

## Testing Strategy *(optional)*

### Unit Tests

**TunnelRegistry**:
- Token generation (entropy, format, uniqueness)
- CRUD operations (create, read, update, delete)
- Token validation and lookup
- Thread safety (concurrent access)

**HTTPProxy**:
- Host header parsing (valid, missing, malformed)
- Tunnel lookup (found, not found, multiple)
- Request formatting for agent
- Response parsing from agent
- Error handling (timeout, agent error, no agent)

**ManagementAPI**:
- JSON parsing (valid, invalid, malformed)
- Input validation (required fields, format)
- Error responses (400, 404, 409)
- Success responses (201, 200, 204)

### Integration Tests

**Management API**:
```bash
# Create tunnel
curl -X POST http://localhost:8080/v1/tunnels \
  -d '{"tunnel_id":"test","protocol":"HTTP","local_url":"http://localhost:3000"}'

# List tunnels
curl http://localhost:8080/v1/tunnels

# Get tunnel
curl http://localhost:8080/v1/tunnels/test

# Delete tunnel
curl -X DELETE http://localhost:8080/v1/tunnels/test
```

**HTTP Proxy**:
```bash
# Terminal 1: Start echo server
python3 -m http.server 3000

# Terminal 2: Create tunnel and start agent
curl -X POST http://localhost:8080/v1/tunnels \
  -d '{"tunnel_id":"echo","protocol":"HTTP","local_url":"http://localhost:3000"}'
./tunnel-agent/build/tunnel-agent --tunnel-id echo --tunnel-token tnl_...

# Terminal 3: Test proxy
curl -H "Host: echo" http://localhost:8080/
# Expected: HTML from Python server
```

### Performance Tests

**Load Testing**:
- 100 concurrent HTTP requests to same tunnel
- 10 tunnels with 10 requests each (100 total)
- Measure: throughput, latency (p50, p95, p99), error rate

**Stress Testing**:
- Create 100 tunnels via API
- Connect 100 agents simultaneously
- Send 10,000 requests across all tunnels
- Monitor: memory usage, CPU, connection count

---

## Success Metrics *(optional)*

**Feature Completion**:
- ✅ All Management API endpoints implemented and tested
- ✅ HTTP Proxy routes requests based on Host header
- ✅ Tokens generated and validated correctly
- ✅ End-to-end test: internet → server → agent → local service
- ✅ Documentation updated in QUICKSTART.md

**Quality Metrics**:
- Unit test coverage: >90%
- Integration tests: All acceptance scenarios pass
- Performance: <10ms proxy overhead, <100ms API latency
- Reliability: 99.9% success rate under load

**User Value**:
- DevOps can register tunnels via REST API
- Developers can access local services via HTTPS tunnel
- System is production-ready for MVP launch

---

## Open Questions & Risks

**Questions**:
1. Should tunnel metadata be persisted (database) or in-memory only for MVP?
   - **Recommendation**: In-memory for MVP, design for external storage in Phase 2
2. How to handle multiple agents per tunnel (load balancing)?
   - **Recommendation**: Round-robin for MVP, add health-based routing later
3. Should Management API require authentication (API keys, Entra ID)?
   - **Recommendation**: No auth for MVP (internal only), add OAuth2 in Phase 2
4. DNS integration: Require wildcard domain or support Host header only?
   - **Recommendation**: Host header only for MVP, add DNS in Phase 3

**Risks**:
1. **Agent compatibility**: Tunnel-agent expects specific HTTP/2 frame format
   - **Mitigation**: Review agent code, ensure server matches protocol
2. **Performance**: HTTP proxy adds latency
   - **Mitigation**: Profile and optimize, target <10ms overhead
3. **State management**: In-memory state lost on server restart
   - **Mitigation**: Document limitation, plan for persistent storage in Phase 2
4. **Security**: No authentication on Management API
   - **Mitigation**: Deploy behind firewall, add auth in Phase 2

---

## Definition of Done

- [ ] All Management API endpoints implemented and tested
- [ ] HTTPProxy routes requests correctly based on Host header
- [ ] TunnelRegistry stores tunnels and validates tokens
- [ ] AgentHandshake integrates with TunnelRegistry
- [ ] Unit tests pass with >90% coverage
- [ ] Integration tests pass (all acceptance scenarios)
- [ ] End-to-end test working: curl → server → agent → echo server
- [ ] Performance tests meet requirements (<10ms overhead)
- [ ] Code deployed to Azure and verified working
- [ ] QUICKSTART.md updated with new workflow
- [ ] All acceptance criteria from user stories met
