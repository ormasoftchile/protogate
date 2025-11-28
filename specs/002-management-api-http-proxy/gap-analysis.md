# Gap Analysis: Management API Integration

**Date**: 2025-11-27  
**Feature**: 002-management-api-http-proxy  
**Status**: ⚠️ **Components Built, Integration Missing**

## Executive Summary

**The Problem**: All components are implemented, but Management API endpoints return 404 because the wiring between components is incomplete.

**What Works**:
- ✅ HTTP tunnel flow (server → agent → local service)
- ✅ Token validation
- ✅ Tunnel storage (TunnelRegistry)
- ✅ API handlers (TunnelsHandler)
- ✅ HTTP proxy routing

**What Doesn't Work**:
- ❌ `POST /v1/tunnels` - 404 Not Found
- ❌ `GET /v1/tunnels` - 404 Not Found
- ❌ `GET /v1/tunnels/{id}` - 404 Not Found
- ❌ `DELETE /v1/tunnels/{id}` - 404 Not Found

**Root Cause**: Router and TunnelsHandler never get initialized or connected to HTTPServer in `main.cpp`.

---

## Detailed Gap Analysis

### 1. Router Not Instantiated ❌

**File**: `src/server/main.cpp`  
**Current State**: Router class exists (`src/api/router.h/cpp`) but is never created  
**Impact**: No routing infrastructure for `/v1/*` paths

**What's Missing**:
```cpp
// MISSING from main.cpp:
auto router = std::make_shared<api::Router>();
```

**Why It Matters**: Without a Router instance, there's no way to map HTTP paths to handlers.

---

### 2. TunnelsHandler Not Registered ❌

**File**: `src/server/main.cpp`  
**Current State**: TunnelsHandler exists with all route handlers implemented, but never instantiated or registered  
**Impact**: Routes like `POST /v1/tunnels` have no handler

**What's Missing**:
```cpp
// MISSING from main.cpp:
auto keyvault_client = std::make_shared<storage::KeyVaultClient>(config.key_vault_uri);

auto tunnels_handler = std::make_shared<api::TunnelsHandler>(
    tunnel_cache,
    token_cache,
    keyvault_client,
    agent_registry);

tunnels_handler->register_routes(*router);
```

**Why It Matters**: TunnelsHandler.register_routes() maps paths like `/v1/tunnels` to handler methods. Without this call, Router doesn't know what to do with Management API requests.

---

### 3. HTTPServer Doesn't Accept Router ❌

**File**: `src/server/http_server.h`, `src/server/http_server.cpp`  
**Current State**: HTTPServer constructor doesn't have router parameter  
**Impact**: Even if Router existed, HTTPServer couldn't use it

**What's Missing**:

In `http_server.h`:
```cpp
HTTPServer(
    std::shared_ptr<IOContextPool> io_pool,
    std::shared_ptr<security::TLSManager> tls_manager,
    std::shared_ptr<proxy::HTTPProxy> http_proxy,
    std::shared_ptr<api::Router> router,  // MISSING
    unsigned short port,
    bool use_tls);
```

In `http_server.cpp`:
```cpp
// MISSING member variable:
std::shared_ptr<api::Router> router_;
```

**Why It Matters**: HTTPServer needs to hold a reference to Router to dispatch `/v1/*` requests.

---

### 4. HTTPServer Doesn't Route to Management API ❌

**File**: `src/server/http_server.cpp`  
**Function**: `handle_plain_http()`  
**Current State**: All requests go to HTTPProxy, no check for `/v1/*` paths  
**Impact**: Management API requests are sent to proxy instead of Router

**What's Missing**:
```cpp
void HTTPServer::handle_plain_http(...) {
    // Parse request...
    
    // MISSING: Check if this is a Management API request
    if (path.rfind("/v1/", 0) == 0 && router_) {
        // Route to Management API
        api::HttpRequest api_request;
        // ... populate request ...
        
        api::HttpResponse api_response;
        router_->handle_request(api_request, api_response);
        
        // ... send response ...
        return;
    }
    
    // Existing: Route to HTTPProxy
    http_proxy_->handle_request(...);
}
```

**Why It Matters**: This is the critical decision point - requests must be routed to either Management API or HTTP Proxy based on path.

---

### 5. Request Body Parsing Not Implemented ❌

**File**: `src/server/http_server.cpp`  
**Function**: `handle_plain_http()`  
**Current State**: Only headers parsed, body ignored  
**Impact**: `POST /v1/tunnels` can't read JSON body

**What's Missing**:
```cpp
// MISSING: Body parsing for POST/PUT requests
if (method == "POST" || method == "PUT") {
    auto content_length_it = request.headers.find("Content-Length");
    if (content_length_it != request.headers.end()) {
        size_t body_length = std::stoul(content_length_it->second);
        // Read body_length bytes into request.body
    }
}
```

**Why It Matters**: Without body parsing, TunnelsHandler can't read tunnel configuration from POST requests.

---

## Implementation Checklist

### Critical Path (Blocks Everything)

- [ ] **Gap 1**: Instantiate Router in `main.cpp`
- [ ] **Gap 2**: Initialize TunnelsHandler and register routes in `main.cpp`
- [ ] **Gap 3**: Add router parameter to HTTPServer constructor
- [ ] **Gap 4**: Route `/v1/*` requests to Router in `handle_plain_http()`
- [ ] **Gap 5**: Implement request body parsing in `handle_plain_http()`

### Verification Tests

- [ ] `curl -X POST http://localhost:443/v1/tunnels -d '{...}'` returns 201 Created
- [ ] `curl http://localhost:443/v1/tunnels` returns JSON array
- [ ] `curl http://localhost:443/v1/tunnels/test-api` returns tunnel details
- [ ] `curl -X DELETE http://localhost:443/v1/tunnels/test-api` returns 204 No Content

---

## Estimated Effort

| Gap | Task | Effort | Complexity |
|-----|------|--------|------------|
| 1 | Instantiate Router | 5 min | Trivial |
| 2 | Register TunnelsHandler | 10 min | Simple |
| 3 | Add router to HTTPServer | 15 min | Simple |
| 4 | Route /v1/* to Router | 30 min | Medium |
| 5 | Parse request body | 20 min | Medium |
| **Total** | | **80 min** | |

**Plus**:
- Build and test: 20 min
- Deploy to Azure: 15 min
- Verification: 10 min

**Grand Total**: ~2 hours

---

## Why This Matters

The spec says "100% complete" but that's **component completion**, not **system integration**. It's like building a car with all the parts manufactured but never assembled.

**Current State**: Car parts in boxes  
**What We Need**: Assembled, running car

**Analogy**:
- Engine (TunnelsHandler): ✅ Built
- Wheels (Router): ✅ Built  
- Chassis (HTTPServer): ✅ Built
- **Assembly**: ❌ Not done

---

## Next Steps

1. Run `/speckit.implement` on this gap analysis
2. Verify all 5 gaps are fixed
3. Test Management API endpoints
4. Deploy to Azure
5. Update tasks.md with correct status

---

## Success Criteria

✅ **Done When**:
- `POST /v1/tunnels` creates tunnel and returns token
- `GET /v1/tunnels` lists all tunnels
- `GET /v1/tunnels/{id}` returns tunnel details
- `DELETE /v1/tunnels/{id}` removes tunnel
- End-to-end workflow: Create tunnel → Start agent → Route traffic → Delete tunnel

**Not Done Until**: All Management API endpoints accessible and functional.
