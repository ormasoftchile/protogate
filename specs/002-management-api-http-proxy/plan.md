# Implementation Plan - Management API and HTTP Proxy

**Feature**: 002-management-api-http-proxy  
**Goal**: Enable end-to-end HTTP tunneling with Management API and HTTP Proxy  
**Timeline**: 6 days  
**Complexity**: Medium-High

## Architecture Overview

### Current State
```
✅ Server Infrastructure (Azure Container Apps)
✅ Tunnel Agent (100% complete, local binary)
✅ AgentConnection (HTTP/2 sessions working)
✅ AgentRegistry (tracks connected agents)
✅ AgentHandshake (authenticates agents)
```

### Target State
```
✅ TunnelRegistry (stores tunnels, generates tokens)
✅ ManagementAPI (REST endpoints for CRUD)
✅ HTTPProxy (routes internet → agent by Host header)
✅ Token Validation (AgentHandshake uses TunnelRegistry)
✅ End-to-end: curl → server → agent → local service
```

## Component Design

### 1. TunnelRegistry (`src/registry/tunnel_registry.h/cpp`)

**Purpose**: Central storage and management for tunnel metadata

**Responsibilities**:
- Store tunnel configurations (tunnel_id, protocol, local_url, token)
- Generate cryptographically secure tokens (256-bit)
- Validate tokens on agent connection
- Link tunnel_id ↔ agent mappings (via AgentRegistry)
- Thread-safe operations (mutex-protected)

**Public Interface**:
```cpp
class TunnelRegistry {
public:
    // Lifecycle
    TunnelRegistry();
    ~TunnelRegistry();
    
    // Tunnel CRUD
    std::optional<Tunnel> create_tunnel(const TunnelConfig& config);
    std::optional<Tunnel> get_tunnel(const std::string& tunnel_id);
    std::vector<Tunnel> list_tunnels(const ListOptions& options = {});
    bool delete_tunnel(const std::string& tunnel_id);
    
    // Token operations
    std::string generate_token();
    std::optional<std::string> validate_token(const std::string& token);  // Returns tunnel_id
    std::optional<std::string> rotate_token(const std::string& tunnel_id, 
                                            std::chrono::seconds grace_period = std::chrono::seconds(300));
    
    // Agent linking
    void register_agent_connection(const std::string& tunnel_id, 
                                   std::shared_ptr<agent::AgentConnection> conn);
    void unregister_agent_connection(const std::string& tunnel_id, 
                                     const std::string& agent_id);
    std::vector<std::shared_ptr<agent::AgentConnection>> get_agents(const std::string& tunnel_id);
    
    // Metrics
    TunnelMetrics get_metrics(const std::string& tunnel_id);
    void record_request(const std::string& tunnel_id, size_t bytes_sent, size_t bytes_received);
};
```

**Storage**:
- Phase 1: In-memory (`std::unordered_map<std::string, Tunnel>`)
- Phase 2: Redis or Azure Cosmos DB for multi-instance support

**Thread Safety**:
- All public methods protected by `std::mutex`
- Lock-free reads for performance (if needed, use shared_mutex)

### 2. ManagementAPI (`src/api/management_api.h/cpp`)

**Purpose**: REST API handlers for tunnel management

**Responsibilities**:
- Parse HTTP requests, extract JSON bodies
- Validate input (required fields, format, constraints)
- Call TunnelRegistry methods
- Format JSON responses
- Handle errors (400, 404, 409, 500)

**Route Handlers**:
```cpp
class ManagementAPI {
public:
    ManagementAPI(std::shared_ptr<TunnelRegistry> registry,
                  std::shared_ptr<agent::AgentRegistry> agent_registry);
    
    // Handlers return HTTP response (status, headers, body)
    HttpResponse handle_create_tunnel(const HttpRequest& request);
    HttpResponse handle_list_tunnels(const HttpRequest& request);
    HttpResponse handle_get_tunnel(const HttpRequest& request);
    HttpResponse handle_delete_tunnel(const HttpRequest& request);
    HttpResponse handle_list_agents(const HttpRequest& request);
    HttpResponse handle_rotate_token(const HttpRequest& request);
    
private:
    std::shared_ptr<TunnelRegistry> tunnel_registry_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    
    // Helper methods
    nlohmann::json validate_create_request(const nlohmann::json& body);
    HttpResponse error_response(int status, const std::string& error, const std::string& message);
};
```

**Integration with HTTPServer**:
- HTTPServer routes requests starting with `/v1/tunnels/` to ManagementAPI
- Use Boost.Beast for HTTP parsing and response building
- JSON handling via nlohmann::json (already in project)

### 3. HTTPProxy (`src/proxy/http_proxy.h/cpp`)

**Purpose**: Route incoming HTTP requests to appropriate tunnel agents

**Responsibilities**:
- Parse HTTP request headers (Host, method, path)
- Extract Host header value (tunnel identifier)
- Lookup tunnel in TunnelRegistry
- Find connected agent via AgentRegistry
- Format HTTP/1.1 request for agent
- Call `AgentConnection::send_http_request()`
- Build HTTP response from agent's response
- Handle errors: 503 (no agent), 502 (agent error), 504 (timeout)

**Public Interface**:
```cpp
class HTTPProxy {
public:
    HTTPProxy(std::shared_ptr<TunnelRegistry> tunnel_registry,
              std::shared_ptr<agent::AgentRegistry> agent_registry);
    
    // Route HTTP request through tunnel
    // Returns: HTTP response (status, headers, body) or error
    HttpResponse route_request(const HttpRequest& request);
    
private:
    std::shared_ptr<TunnelRegistry> tunnel_registry_;
    std::shared_ptr<agent::AgentRegistry> agent_registry_;
    std::chrono::seconds timeout_{30};
    
    // Helper methods
    std::optional<std::string> extract_host_header(const HttpRequest& request);
    std::optional<std::shared_ptr<agent::AgentConnection>> find_agent(const std::string& tunnel_id);
    std::string format_request_for_agent(const HttpRequest& request);
    HttpResponse parse_agent_response(const std::string& response);
    HttpResponse error_response(int status, const std::string& message);
};
```

**Request Flow**:
1. HTTPServer receives request on port 8080
2. Check if request path starts with `/v1/tunnels/` → ManagementAPI
3. Otherwise → HTTPProxy
4. HTTPProxy extracts Host header
5. Lookup tunnel_id by Host value
6. Find connected agent for tunnel
7. Forward request via AgentConnection::send_http_request()
8. Wait for response (with timeout)
9. Return response to client

**Error Handling**:
- No Host header → 400 Bad Request
- Tunnel not found → 404 Not Found
- No agent connected → 503 Service Unavailable
- Agent returns error → 502 Bad Gateway
- Timeout (30s) → 504 Gateway Timeout
- Agent exception → 502 Bad Gateway

### 4. Modified Components

**AgentHandshake** (`src/agent/agent_handshake.cpp`):
- Add TunnelRegistry reference to constructor
- Replace hardcoded token validation with `tunnel_registry_->validate_token(token)`
- Extract tunnel_id from validated token
- Pass tunnel_id to AgentConnection on successful auth
- Notify TunnelRegistry when agent connects: `register_agent_connection()`

**HTTPServer** (`src/server/http_server.cpp`):
- Add TunnelRegistry and ManagementAPI instances
- Route `/v1/tunnels/*` requests to ManagementAPI
- Route all other requests to HTTPProxy
- Keep existing `/health` endpoint

**Main** (`src/server/main.cpp`):
- Create TunnelRegistry instance
- Create ManagementAPI instance with TunnelRegistry
- Create HTTPProxy instance with TunnelRegistry and AgentRegistry
- Pass TunnelRegistry to AgentHandshake
- Pass ManagementAPI and HTTPProxy to HTTPServer

## Data Models

### Tunnel
```cpp
struct Tunnel {
    std::string tunnel_id;
    std::string protocol;  // "HTTP" or "TCP"
    std::string local_url; // "http://localhost:3000" for HTTP
    int local_port;        // 9100 for TCP
    std::string token;     // "tnl_abc123..."
    
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    
    // Token rotation support
    std::optional<std::string> old_token;
    std::optional<std::chrono::system_clock::time_point> old_token_expires_at;
    
    // Metrics (updated at runtime)
    uint64_t total_requests{0};
    uint64_t total_bytes_sent{0};
    uint64_t total_bytes_received{0};
    std::optional<std::chrono::system_clock::time_point> last_request_at;
};
```

### TunnelConfig
```cpp
struct TunnelConfig {
    std::string tunnel_id;
    std::string protocol;
    std::optional<std::string> local_url;
    std::optional<int> local_port;
};
```

### TunnelMetrics
```cpp
struct TunnelMetrics {
    uint64_t total_requests;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
    std::optional<std::chrono::system_clock::time_point> last_request_at;
    int connected_agent_count;
};
```

## Dependencies

**No new external dependencies required**. All libraries already in project:
- Boost.Asio (async I/O)
- Boost.Beast (HTTP parsing)
- OpenSSL (random number generation for tokens)
- nghttp2 (HTTP/2 for agent communication)
- nlohmann::json (JSON parsing/serialization)

## Testing Strategy

### Unit Tests

**TunnelRegistry** (`tests/unit/tunnel_registry_test.cpp`):
- ✅ Token generation (entropy, format "tnl_", uniqueness)
- ✅ Create tunnel (valid config, duplicate ID error)
- ✅ Get tunnel (found, not found)
- ✅ List tunnels (empty, multiple, filtering)
- ✅ Delete tunnel (success, not found)
- ✅ Token validation (valid, invalid, expired)
- ✅ Token rotation (grace period, old token expiration)
- ✅ Thread safety (concurrent creates, deletes, lookups)

**ManagementAPI** (`tests/unit/management_api_test.cpp`):
- ✅ POST /v1/tunnels (success 201, duplicate 409, invalid 400)
- ✅ GET /v1/tunnels (empty list, multiple tunnels)
- ✅ GET /v1/tunnels/{id} (found 200, not found 404)
- ✅ DELETE /v1/tunnels/{id} (success 204, not found 404)
- ✅ JSON parsing (valid, malformed, missing fields)
- ✅ Error responses (correct status codes and messages)

**HTTPProxy** (`tests/unit/http_proxy_test.cpp`):
- ✅ Host header extraction (present, missing, multiple)
- ✅ Tunnel lookup (found, not found)
- ✅ Agent selection (single agent, multiple agents, round-robin)
- ✅ Request formatting (GET, POST with body, headers preserved)
- ✅ Response parsing (status codes, headers, body)
- ✅ Error handling (no agent 503, timeout 504, agent error 502)

### Integration Tests

**End-to-End HTTP Tunnel** (`tests/integration/http_tunnel_test.cpp`):
```bash
# 1. Start server with TunnelRegistry and HTTPProxy
./build/protogate-server

# 2. Create tunnel via API
curl -X POST http://localhost:8080/v1/tunnels \
  -d '{"tunnel_id":"test","protocol":"HTTP","local_url":"http://localhost:3000"}'
# Returns: {"token": "tnl_abc123..."}

# 3. Start echo server on port 3000
python3 -m http.server 3000 &

# 4. Start tunnel agent
./tunnel-agent/build/tunnel-agent \
  --tunnel-id test \
  --tunnel-token tnl_abc123... \
  --local-url http://localhost:3000

# 5. Send request through tunnel
curl -H "Host: test" http://localhost:8080/
# Expected: HTML from Python server

# 6. Verify response matches direct access
diff <(curl -H "Host: test" http://localhost:8080/) \
     <(curl http://localhost:3000/)
```

**Management API Integration** (`tests/integration/management_api_test.sh`):
```bash
#!/bin/bash
# Test all Management API endpoints

# Create tunnel
RESPONSE=$(curl -s -X POST http://localhost:8080/v1/tunnels \
  -d '{"tunnel_id":"api1","protocol":"HTTP","local_url":"http://localhost:3000"}')
TOKEN=$(echo $RESPONSE | jq -r '.token')
assert_not_empty "$TOKEN"

# List tunnels
curl -s http://localhost:8080/v1/tunnels | jq '.tunnels | length' | grep 1

# Get tunnel details
curl -s http://localhost:8080/v1/tunnels/api1 | jq -r '.tunnel_id' | grep api1

# Delete tunnel
curl -s -X DELETE http://localhost:8080/v1/tunnels/api1 -w '%{http_code}' | grep 204

# Verify deleted
curl -s http://localhost:8080/v1/tunnels/api1 -w '%{http_code}' | grep 404
```

### Performance Tests

**Load Test** (`tests/performance/http_proxy_load.cpp`):
- 100 concurrent requests to same tunnel
- Measure: throughput (req/s), latency (p50, p95, p99), success rate
- Target: >1000 req/s, p99 <50ms, success rate >99.9%

**Stress Test** (`tests/performance/tunnel_registry_stress.cpp`):
- Create 1000 tunnels via API
- Connect 1000 agents simultaneously
- Send 10,000 requests across all tunnels
- Measure: memory usage, CPU, connection count
- Target: <500MB memory, <80% CPU, all requests succeed

## Deployment Plan

### Phase 1: Local Development (Days 1-3)
1. Implement TunnelRegistry with in-memory storage
2. Implement ManagementAPI handlers
3. Write unit tests, verify locally
4. Test with curl against localhost:8080

### Phase 2: HTTP Proxy (Days 4-5)
1. Implement HTTPProxy routing logic
2. Integrate with AgentConnection
3. Write unit tests with mock agents
4. Write integration tests with real agents
5. Test end-to-end: curl → server → agent → echo server

### Phase 3: Integration and Deployment (Day 6)
1. Modify AgentHandshake to use TunnelRegistry
2. Wire up all components in main.cpp
3. Run full integration test suite
4. Build Docker image with new code
5. Deploy to Azure: `./build-and-push-local.sh`
6. Verify in production with real tunnel-agent

### Rollback Plan
- Keep previous Azure Container App revision active
- If issues detected, revert to previous revision: 
  ```bash
  az containerapp revision set-mode --mode single --revision protogate-dev-app--0000020
  ```
- Fix issues locally, redeploy when ready

## Metrics and Monitoring

**Success Metrics**:
- ✅ All Management API endpoints return correct responses
- ✅ HTTP Proxy routes requests with <10ms overhead
- ✅ End-to-end test passes: curl → server → agent → echo
- ✅ Unit test coverage >90%
- ✅ No memory leaks (Valgrind clean)

**Production Monitoring** (Azure Application Insights):
- HTTP request count by endpoint
- HTTP response latency (p50, p95, p99)
- Error rate by status code
- Tunnel count (active, total)
- Agent count (connected)
- Memory usage, CPU usage

**Alerting**:
- Error rate >1% → Send alert
- p99 latency >100ms → Send alert
- Agent disconnect rate >10/min → Send alert
- Memory usage >80% → Send alert

## Risks and Mitigations

**Risk 1: Agent compatibility with HTTP/2 protocol**
- Mitigation: Review tunnel-agent code, ensure request format matches
- Test: Integration test with real agent before production deployment

**Risk 2: Performance degradation under load**
- Mitigation: Profile critical paths, optimize hot spots
- Test: Load test with 1000 concurrent requests

**Risk 3: State loss on server restart (in-memory storage)**
- Mitigation: Document limitation, plan for persistent storage in Phase 2
- Workaround: Tunnels auto-recreated when agents reconnect (if needed)

**Risk 4: Token security (in-memory only, no Key Vault yet)**
- Mitigation: Generate high-entropy tokens, document security limitation
- Workaround: Plan Key Vault integration for Phase 2

## Success Criteria

- [ ] TunnelRegistry implemented and tested
- [ ] ManagementAPI handlers implemented and tested
- [ ] HTTPProxy routing implemented and tested
- [ ] AgentHandshake integrated with TunnelRegistry
- [ ] All unit tests pass (>90% coverage)
- [ ] All integration tests pass
- [ ] End-to-end test working: curl → tunnel → agent → echo
- [ ] Performance tests meet targets (<10ms overhead)
- [ ] Code deployed to Azure and verified
- [ ] QUICKSTART.md updated with new workflow
- [ ] All acceptance criteria met from spec.md
