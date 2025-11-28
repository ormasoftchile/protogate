# Feature Specification: Management API Integration Wiring

**Feature Branch**: `001-003-integration-wiring`  
**Created**: 2025-11-27  
**Status**: Active  
**Parent**: `002-management-api-http-proxy` (Components Built)  
**Input**: Gap analysis revealed components exist but aren't wired together

## Context

**Current State**:
- ✅ All Management API components implemented
  - `src/models/tunnel.h` - Tunnel data structure
  - `src/storage/tunnel_registry.h/cpp` - Tunnel CRUD and token management
  - `src/api/tunnels_handler.h/cpp` - REST endpoint handlers (create, list, get, delete, rotate)
  - `src/api/router.h/cpp` - HTTP routing infrastructure
  - `src/proxy/http_proxy.cpp` - HTTP request proxying to agents
- ✅ HTTP tunnel flow works (server → agent → local service)
- ✅ Test script verified working (`.specify/scripts/bash/test-local-tunnel.sh`)

**Gaps**:
- ❌ Router never instantiated in `main.cpp`
- ❌ TunnelsHandler never created or registered
- ❌ HTTPServer doesn't accept or use Router
- ❌ HTTPServer doesn't route `/v1/*` requests to Management API
- ❌ Request body parsing not implemented for POST/PUT

**Result**: All Management API endpoints return 404 Not Found

**Goal**: Wire components together so Management API endpoints are functional

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Create Tunnel via Management API (Priority: P0)

**Actor**: DevOps engineer  
**Context**: Register new tunnel before starting agent

**Scenario**: Engineer runs `curl -X POST http://localhost:443/v1/tunnels -H "Content-Type: application/json" -d '{"tunnel_id":"my-api","protocol":"HTTP","target_host":"localhost","target_port":3000}'`. Server routes request to TunnelsHandler, creates tunnel in TunnelRegistry, generates secure token, returns 201 Created with token.

**Why this priority**: Without tunnel registration, agents can't connect. This is the entry point for all workflows.

**Independent Test**:
```bash
curl -X POST http://localhost:443/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{"tunnel_id":"test-api","protocol":"HTTP","target_host":"localhost","target_port":3000}'
# Expected: HTTP 201 with {"tunnel_id":"test-api","token":"tnl_...","status":"active"}
```

**Acceptance Scenarios**:
1. **Given** server running with Router wired, **When** POST `/v1/tunnels` with valid JSON, **Then** returns 201 with tunnel details and token
2. **Given** tunnel exists, **When** POST `/v1/tunnels` with duplicate ID, **Then** returns 409 Conflict
3. **Given** invalid JSON (missing tunnel_id), **When** POST `/v1/tunnels`, **Then** returns 400 Bad Request with error details

---

### User Story 2 - List Tunnels (Priority: P0)

**Actor**: DevOps engineer, monitoring system  
**Context**: View all registered tunnels and their status

**Scenario**: Engineer runs `curl http://localhost:443/v1/tunnels`. Server routes to TunnelsHandler, queries TunnelRegistry, returns JSON array of all tunnels with status (active/connected/disconnected).

**Why this priority**: Essential for visibility. Can't manage what you can't see.

**Independent Test**:
```bash
# Create 2 tunnels first
curl -X POST http://localhost:443/v1/tunnels -d '{"tunnel_id":"api-1","protocol":"HTTP","target_host":"localhost","target_port":3000}'
curl -X POST http://localhost:443/v1/tunnels -d '{"tunnel_id":"api-2","protocol":"HTTP","target_host":"localhost","target_port":4000}'

# List all
curl http://localhost:443/v1/tunnels
# Expected: HTTP 200 with array of 2 tunnels
```

**Acceptance Scenarios**:
1. **Given** 2 tunnels registered, **When** GET `/v1/tunnels`, **Then** returns 200 with array of 2 tunnel objects
2. **Given** no tunnels, **When** GET `/v1/tunnels`, **Then** returns 200 with empty array

---

### User Story 3 - Get Tunnel Details (Priority: P1)

**Actor**: DevOps engineer  
**Context**: Check specific tunnel status and configuration

**Scenario**: Engineer runs `curl http://localhost:443/v1/tunnels/my-api`. Server routes to TunnelsHandler, looks up tunnel in TunnelRegistry, returns detailed info (created_at, agent_status, metrics).

**Why this priority**: Important for troubleshooting, but listing is more critical.

**Independent Test**:
```bash
curl http://localhost:443/v1/tunnels/test-api
# Expected: HTTP 200 with full tunnel details
```

**Acceptance Scenarios**:
1. **Given** tunnel exists, **When** GET `/v1/tunnels/{id}`, **Then** returns 200 with tunnel details
2. **Given** tunnel doesn't exist, **When** GET `/v1/tunnels/nonexistent`, **Then** returns 404 Not Found

---

### User Story 4 - Delete Tunnel (Priority: P1)

**Actor**: DevOps engineer  
**Context**: Decommission tunnel, revoke token

**Scenario**: Engineer runs `curl -X DELETE http://localhost:443/v1/tunnels/old-api`. Server routes to TunnelsHandler, removes from TunnelRegistry, disconnects any connected agents, returns 204 No Content.

**Why this priority**: Important for cleanup, but not critical for MVP.

**Independent Test**:
```bash
curl -X DELETE http://localhost:443/v1/tunnels/test-api
# Expected: HTTP 204 No Content

# Verify deleted
curl http://localhost:443/v1/tunnels/test-api
# Expected: HTTP 404 Not Found
```

**Acceptance Scenarios**:
1. **Given** tunnel exists, **When** DELETE `/v1/tunnels/{id}`, **Then** returns 204 and tunnel removed
2. **Given** tunnel doesn't exist, **When** DELETE `/v1/tunnels/{id}`, **Then** returns 404 Not Found

---

## Technical Requirements *(mandatory)*

### Integration Changes

**1. main.cpp - Instantiate Components**:
```cpp
// After creating tunnel_cache, token_cache, agent_registry:

// Create Router
auto router = std::make_shared<api::Router>();

// Create KeyVault client (or mock for local testing)
auto keyvault_client = std::make_shared<storage::KeyVaultClient>(config.key_vault_uri);

// Create TunnelsHandler
auto tunnels_handler = std::make_shared<api::TunnelsHandler>(
    tunnel_cache,
    token_cache,
    keyvault_client,
    agent_registry
);

// Register routes
tunnels_handler->register_routes(*router);

// Pass router to HTTPServer
auto http_server = std::make_shared<server::HTTPServer>(
    io_pool,
    tls_manager,
    http_proxy,
    router,  // NEW PARAMETER
    443,
    false
);
```

**2. http_server.h - Add Router Parameter**:
```cpp
class HTTPServer {
public:
    HTTPServer(
        std::shared_ptr<IOContextPool> io_pool,
        std::shared_ptr<security::TLSManager> tls_manager,
        std::shared_ptr<proxy::HTTPProxy> http_proxy,
        std::shared_ptr<api::Router> router,  // NEW
        unsigned short port,
        bool use_tls
    );
    
private:
    std::shared_ptr<api::Router> router_;  // NEW
    // ... existing members
};
```

**3. http_server.cpp - Route /v1/* to Router**:
```cpp
void HTTPServer::handle_plain_http(...) {
    // Parse request headers and method
    // ... existing code ...
    
    // NEW: Check if this is a Management API request
    if (path.rfind("/v1/", 0) == 0 && router_) {
        // Parse request body for POST/PUT
        std::string body;
        if (method == "POST" || method == "PUT") {
            auto content_length_it = headers.find("content-length");
            if (content_length_it != headers.end()) {
                size_t body_length = std::stoul(content_length_it->second);
                // Read body_length bytes
                body.resize(body_length);
                // ... read from socket into body ...
            }
        }
        
        // Build API request
        api::HttpRequest api_request{
            .method = method,
            .path = path,
            .headers = headers,
            .body = body
        };
        
        // Route to Management API
        api::HttpResponse api_response;
        router_->handle_request(api_request, api_response);
        
        // Send response
        // ... format and send api_response ...
        return;
    }
    
    // Existing: Route to HTTPProxy
    http_proxy_->handle_request(...);
}
```

### Functional Requirements

- Router instantiated and passed to HTTPServer
- TunnelsHandler registered with Router
- HTTPServer routes `/v1/*` paths to Router
- Request body parsed for POST/PUT requests
- All other requests routed to HTTPProxy (existing behavior)

### Non-Functional Requirements

**Performance**:
- No performance degradation (routing overhead <1ms)
- Request body parsing efficient (streaming where possible)

**Reliability**:
- Invalid JSON returns 400 Bad Request (not crash)
- Missing router gracefully falls back to 404

**Testing**:
- Unit tests for body parsing edge cases
- Integration tests for all 4 user stories
- Verify existing HTTP proxy still works

---

## Implementation Plan *(optional)*

### Phase 1: Add Router to HTTPServer (30 minutes)

**Files to Modify**:
- `src/server/http_server.h` - Add router parameter and member
- `src/server/http_server.cpp` - Accept router in constructor
- `src/server/main.cpp` - Create Router instance

**Tasks**:
1. Add `std::shared_ptr<api::Router> router_` member to HTTPServer
2. Add router parameter to HTTPServer constructor
3. Store router in constructor
4. Update main.cpp to create Router and pass to HTTPServer
5. Build and verify compiles

**Success Criteria**:
- Code compiles
- Server starts without errors
- Existing tests still pass

### Phase 2: Wire TunnelsHandler (20 minutes)

**Files to Modify**:
- `src/server/main.cpp` - Create TunnelsHandler and register routes

**Tasks**:
1. Create KeyVaultClient instance (or mock)
2. Create TunnelsHandler with dependencies
3. Call `tunnels_handler->register_routes(*router)`
4. Build and verify compiles

**Success Criteria**:
- Code compiles
- Server starts
- Router has routes registered (check logs)

### Phase 3: Route /v1/* Requests (30 minutes)

**Files to Modify**:
- `src/server/http_server.cpp` - Add routing logic to handle_plain_http

**Tasks**:
1. Add path check: `if (path.rfind("/v1/", 0) == 0)`
2. Implement request body parsing for POST/PUT
3. Build HttpRequest from parsed data
4. Call router->handle_request()
5. Format and send HttpResponse
6. Add error handling (invalid JSON, missing body)

**Success Criteria**:
- POST /v1/tunnels returns 201 (not 404)
- GET /v1/tunnels returns 200 with array
- Invalid JSON returns 400
- Existing HTTP proxy still works

### Phase 4: Testing & Verification (20 minutes)

**Tests**:
1. Run test script: `.specify/scripts/bash/test-local-tunnel.sh`
2. Test POST /v1/tunnels (create tunnel)
3. Test GET /v1/tunnels (list tunnels)
4. Test GET /v1/tunnels/{id} (get tunnel)
5. Test DELETE /v1/tunnels/{id} (delete tunnel)
6. Test invalid JSON returns 400
7. Test nonexistent tunnel returns 404
8. Verify HTTP proxy still routes non-/v1/* requests

**Success Criteria**:
- All 4 user stories pass acceptance scenarios
- Existing tunnel flow still works
- No regressions in HTTP proxy

---

## Testing Strategy *(optional)*

### Integration Tests

**Management API**:
```bash
# Start server
env KEY_VAULT_URI=https://mock-vault.vault.azure.net/ DNS_ZONE=tunnel.local ./build/protogate-server &

# Test create
curl -X POST http://localhost:443/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{"tunnel_id":"test-api","protocol":"HTTP","target_host":"localhost","target_port":3000}'

# Test list
curl http://localhost:443/v1/tunnels

# Test get
curl http://localhost:443/v1/tunnels/test-api

# Test delete
curl -X DELETE http://localhost:443/v1/tunnels/test-api
```

**HTTP Proxy (Regression)**:
```bash
# Start test service
python3 -m http.server 3000 &

# Start agent
cd tunnel-agent && ./build/tunnel-agent --config config.json &

# Test proxying still works
curl -H 'Host: test-api' http://localhost:443/
# Expected: HTML from localhost:3000
```

---

## Success Metrics *(optional)*

**Feature Completion**:
- ✅ Router instantiated and wired to HTTPServer
- ✅ TunnelsHandler registered with Router
- ✅ HTTPServer routes /v1/* to Management API
- ✅ Request body parsing implemented
- ✅ All 4 user stories pass acceptance tests
- ✅ Existing HTTP proxy still functional

**Quality Metrics**:
- Zero regressions in existing functionality
- All Management API endpoints return correct status codes
- Error handling graceful (no crashes on invalid input)

**User Value**:
- DevOps can create tunnels via API
- DevOps can list/view/delete tunnels
- System ready for production use

---

## Definition of Done

- [ ] Router instantiated in main.cpp
- [ ] TunnelsHandler created and routes registered
- [ ] HTTPServer accepts router parameter
- [ ] HTTPServer routes /v1/* to Router
- [ ] Request body parsing implemented for POST/PUT
- [ ] POST /v1/tunnels creates tunnel and returns 201
- [ ] GET /v1/tunnels lists all tunnels (200)
- [ ] GET /v1/tunnels/{id} returns tunnel details (200)
- [ ] DELETE /v1/tunnels/{id} removes tunnel (204)
- [ ] Invalid JSON returns 400 Bad Request
- [ ] Nonexistent tunnel returns 404 Not Found
- [ ] Existing HTTP proxy still works (regression test)
- [ ] All integration tests pass
- [ ] Code deployed and verified
