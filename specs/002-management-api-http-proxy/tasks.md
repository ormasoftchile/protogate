# Tasks: Management API and HTTP Proxy

**Feature**: Complete end-to-end HTTP tunneling with Management API and HTTP Proxy  
**Input**: [spec.md](spec.md), [plan.md](plan.md), [contracts/management-api.yaml](contracts/management-api.yaml)  
**Branch**: `002-management-api-http-proxy`  
**Parent**: `001-tunnel-core-server` (Complete)

**Implementation Strategy**: Build Management API first (enables tunnel registration), then HTTP Proxy (enables traffic routing), then integrate with existing agent infrastructure.

**Tests**: Constitution requires 80% code coverage. All components include comprehensive unit and integration tests.

**Progress**: 0/45 tasks complete (0%)
- Phase 1 (TunnelRegistry): 0/8 (0%)
- Phase 2 (ManagementAPI): 0/10 (0%)
- Phase 3 (HTTPProxy): 0/9 (0%)
- Phase 4 (Integration): 0/8 (0%)
- Phase 5 (Testing): 0/7 (0%)
- Phase 6 (Deployment): 0/3 (0%)

**Status**: 🚧 **READY TO START** - All prerequisites complete from Feature 001

---

## Task Format: `- [ ] [ID] [P?] Description with file path`

- **[P]**: Parallelizable (different files, no blocking dependencies)
- All file paths are absolute from project root
- Tasks marked [X] are complete

---

## Phase 1: TunnelRegistry (Foundation for all other components)

**Purpose**: Central storage and token management for tunnels

⚠️ **CRITICAL**: This phase blocks ALL other phases - must complete first

### Implementation Tasks

- [ ] T001 Create src/models/tunnel.h - Define Tunnel struct with tunnel_id, protocol, local_url, token, timestamps, metrics
- [ ] T002 [P] Create src/models/tunnel_config.h - Define TunnelConfig struct for creation requests
- [ ] T003 [P] Create src/models/tunnel_metrics.h - Define TunnelMetrics struct for runtime statistics
- [ ] T004 Create src/registry/tunnel_registry.h/cpp - Implement TunnelRegistry class with in-memory storage (std::unordered_map)
- [ ] T005 Add TunnelRegistry::create_tunnel() - Validate config, generate token, store tunnel, return Tunnel object
- [ ] T006 Add TunnelRegistry::generate_token() - Generate 256-bit random token, Base64 encode, add "tnl_" prefix using OpenSSL
- [ ] T007 Add TunnelRegistry::validate_token() - Lookup token in registry, check expiration, return tunnel_id or nullopt
- [ ] T008 Add TunnelRegistry thread safety - Protect all public methods with std::mutex, ensure no race conditions

### Testing Tasks

- [ ] T009 [P] Create tests/unit/tunnel_registry_test.cpp - Test CRUD operations, token generation, validation
- [ ] T010 [P] Add test for token entropy - Verify 256-bit randomness, uniqueness across 10,000 tokens
- [ ] T011 [P] Add test for thread safety - Concurrent creates/deletes from 10 threads, verify correctness

**Checkpoint**: ✅ TunnelRegistry complete - ManagementAPI and HTTPProxy can now proceed in parallel

---

## Phase 2: ManagementAPI (REST endpoints for tunnel management)

**Purpose**: Enable tunnel registration and management via REST API

**Dependencies**: Phase 1 (TunnelRegistry)

### Implementation Tasks

- [ ] T012 Create src/api/management_api.h/cpp - Implement ManagementAPI class with TunnelRegistry reference
- [ ] T013 Add ManagementAPI::handle_create_tunnel() - Parse JSON, validate, call TunnelRegistry::create_tunnel(), return 201 or error
- [ ] T014 Add ManagementAPI::handle_list_tunnels() - Get tunnels from registry, format JSON array with pagination support
- [ ] T015 Add ManagementAPI::handle_get_tunnel() - Lookup tunnel by ID, include agent status from AgentRegistry, return JSON
- [ ] T016 Add ManagementAPI::handle_delete_tunnel() - Remove tunnel, disconnect agents via AgentRegistry, return 204
- [ ] T017 Add ManagementAPI::handle_list_agents() - List agents for tunnel_id from AgentRegistry, return JSON array
- [ ] T018 Add ManagementAPI::handle_rotate_token() - Generate new token, set grace period, update tunnel, return tokens
- [ ] T019 Add input validation - Validate tunnel_id format (lowercase alphanumeric+hyphens), required fields, JSON schema
- [ ] T020 Add error responses - Format 400/404/409/500 errors with JSON body {error, message, details}
- [ ] T021 Integrate ManagementAPI with HTTPServer - Route /v1/tunnels/* requests to ManagementAPI handlers

### Testing Tasks

- [ ] T022 [P] Create tests/unit/management_api_test.cpp - Test all endpoints with mock TunnelRegistry
- [ ] T023 [P] Add test for POST /v1/tunnels - Valid request returns 201, duplicate returns 409, invalid returns 400
- [ ] T024 [P] Add test for GET /v1/tunnels - Returns correct JSON array, pagination works, empty list handled
- [ ] T025 [P] Add test for DELETE /v1/tunnels - Success returns 204, not found returns 404, agents disconnected
- [ ] T026 [P] Create tests/integration/management_api_integration_test.sh - End-to-end tests with curl against running server

**Checkpoint**: ✅ ManagementAPI complete - Tunnels can now be registered via REST API

---

## Phase 3: HTTPProxy (Route internet traffic to agents)

**Purpose**: Route incoming HTTP requests to appropriate tunnel agents based on Host header

**Dependencies**: Phase 1 (TunnelRegistry)

**Note**: Can develop in parallel with Phase 2 (ManagementAPI)

### Implementation Tasks

- [ ] T027 Create src/proxy/http_proxy.h/cpp - Implement HTTPProxy class with TunnelRegistry and AgentRegistry references
- [ ] T028 Add HTTPProxy::route_request() - Main routing method, returns HttpResponse or error
- [ ] T029 Add HTTPProxy::extract_host_header() - Parse HTTP headers, extract Host value, handle missing/malformed
- [ ] T030 Add HTTPProxy::find_agent() - Lookup tunnel by Host, get agents from AgentRegistry, select one (round-robin)
- [ ] T031 Add HTTPProxy::format_request_for_agent() - Convert HttpRequest to HTTP/1.1 format string for agent
- [ ] T032 Add HTTPProxy::parse_agent_response() - Parse agent's HTTP/1.1 response string into HttpResponse struct
- [ ] T033 Add timeout handling - Set 30-second timeout on AgentConnection::send_http_request(), return 504 on timeout
- [ ] T034 Add error handling - Return 503 if no agent, 502 if agent error, 400 if no Host header, 404 if tunnel not found
- [ ] T035 Integrate HTTPProxy with HTTPServer - Route non-/v1/tunnels requests to HTTPProxy

### Testing Tasks

- [ ] T036 [P] Create tests/unit/http_proxy_test.cpp - Test routing logic with mock AgentConnection
- [ ] T037 [P] Add test for Host header extraction - Valid, missing, multiple, malformed headers
- [ ] T038 [P] Add test for error responses - Verify 503/502/504/400/404 returned correctly
- [ ] T039 [P] Add test for request formatting - Verify HTTP/1.1 format correct, headers preserved, body included
- [ ] T040 [P] Create tests/integration/http_proxy_integration_test.cpp - End-to-end with real tunnel-agent

**Checkpoint**: ✅ HTTPProxy complete - HTTP requests can now be routed through tunnels

---

## Phase 4: Integration (Wire everything together)

**Purpose**: Connect all components and modify existing code to use TunnelRegistry

**Dependencies**: Phases 1, 2, 3 (all components complete)

### Implementation Tasks

- [ ] T041 Modify src/agent/agent_handshake.cpp - Add TunnelRegistry reference, validate tokens via TunnelRegistry::validate_token()
- [ ] T042 Update AgentHandshake to extract tunnel_id from validated token, pass to AgentConnection constructor
- [ ] T043 Add TunnelRegistry::register_agent_connection() - Link tunnel_id → AgentConnection in registry
- [ ] T044 Add TunnelRegistry::unregister_agent_connection() - Remove agent link on disconnect, update metrics
- [ ] T045 Modify src/server/main.cpp - Create TunnelRegistry, ManagementAPI, HTTPProxy instances, wire dependencies
- [ ] T046 Update HTTPServer constructor - Accept ManagementAPI and HTTPProxy references, route requests correctly
- [ ] T047 Add audit logging - Log all tunnel create/delete/token-rotation events with timestamp, source, action
- [ ] T048 Add metrics collection - Record request count, bytes sent/received per tunnel in TunnelRegistry

**Checkpoint**: ✅ Integration complete - All components working together

---

## Phase 5: Testing (Comprehensive end-to-end validation)

**Purpose**: Verify complete system works as specified

**Dependencies**: Phase 4 (integration complete)

### Testing Tasks

- [ ] T049 Create tests/integration/end_to_end_test.sh - Complete workflow: create tunnel → start agent → send request → verify response
- [ ] T050 Add test for agent connection with Management API token - Create tunnel, agent connects with returned token
- [ ] T051 Add test for HTTP proxy with real agent - Start Python server, agent forwards to it, curl gets correct response
- [ ] T052 Add test for concurrent requests - 50 simultaneous requests to same tunnel, all succeed
- [ ] T053 Add test for agent offline - Delete tunnel, verify requests return 503
- [ ] T054 Add test for multiple agents per tunnel - 2 agents connect, requests load-balanced (round-robin)
- [ ] T055 Create tests/performance/http_proxy_load_test.cpp - Benchmark with 1000 req/s, measure latency (p50/p95/p99)

**Checkpoint**: ✅ All tests passing - System ready for deployment

---

## Phase 6: Deployment and Documentation

**Purpose**: Deploy to Azure and update documentation

**Dependencies**: Phase 5 (all tests passing)

### Deployment Tasks

- [ ] T056 Update QUICKSTART.md - Add Management API usage examples, HTTP proxy workflow, tunnel agent configuration
- [ ] T057 Build and deploy to Azure - Run ./build-and-push-local.sh, verify health endpoint, test with real agent
- [ ] T058 Verify production deployment - Create tunnel via API, connect agent, send request through public URL, verify response

**Checkpoint**: ✅ Deployment complete - Feature 002 done!

---

## Summary by Phase

**Phase 1: TunnelRegistry** (T001-T011) - Foundation
- 8 implementation tasks
- 3 testing tasks
- **BLOCKS**: All other phases

**Phase 2: ManagementAPI** (T012-T026) - REST API
- 10 implementation tasks
- 5 testing tasks
- **REQUIRES**: Phase 1
- **PARALLEL**: Can develop alongside Phase 3

**Phase 3: HTTPProxy** (T027-T040) - Traffic Routing
- 9 implementation tasks
- 5 testing tasks
- **REQUIRES**: Phase 1
- **PARALLEL**: Can develop alongside Phase 2

**Phase 4: Integration** (T041-T048) - Wire Components
- 8 implementation tasks
- 0 testing tasks (tested in Phase 5)
- **REQUIRES**: Phases 1, 2, 3

**Phase 5: Testing** (T049-T055) - End-to-End Validation
- 0 implementation tasks
- 7 testing tasks
- **REQUIRES**: Phase 4

**Phase 6: Deployment** (T056-T058) - Production Release
- 3 documentation/deployment tasks
- **REQUIRES**: Phase 5

---

## Execution Strategy

### Week 1: Foundation and Parallel Development

**Days 1-2**: Phase 1 (TunnelRegistry)
- ✅ Complete T001-T011
- ✅ Checkpoint: TunnelRegistry tested and working

**Days 3-4**: Phases 2 & 3 (Parallel)
- Team A: ManagementAPI (T012-T026)
- Team B: HTTPProxy (T027-T040)
- ✅ Checkpoint: Both APIs tested independently

**Days 5-6**: Phases 4, 5, 6 (Integration → Testing → Deployment)
- Day 5 morning: Integration (T041-T048)
- Day 5 afternoon: Testing (T049-T055)
- Day 6: Deployment (T056-T058)
- ✅ Checkpoint: Production deployment verified

### Single Developer Timeline

If working alone (sequential execution):

**Day 1**: T001-T011 (TunnelRegistry)  
**Day 2**: T012-T026 (ManagementAPI)  
**Day 3**: T027-T040 (HTTPProxy)  
**Day 4**: T041-T048 (Integration)  
**Day 5**: T049-T055 (Testing)  
**Day 6**: T056-T058 (Deployment)

---

## Definition of Done

**Feature 002 is complete when**:

- [X] All 45 tasks marked complete
- [ ] TunnelRegistry stores tunnels and validates tokens
- [ ] ManagementAPI endpoints working (POST/GET/DELETE /v1/tunnels)
- [ ] HTTPProxy routes requests based on Host header
- [ ] Agent connects with Management API token
- [ ] End-to-end test passes: curl → server → agent → local service
- [ ] Unit test coverage >90%
- [ ] Integration tests pass (all acceptance scenarios)
- [ ] Performance tests meet targets (<10ms proxy overhead)
- [ ] Code deployed to Azure and verified working
- [ ] QUICKSTART.md updated with new workflow
- [ ] All acceptance criteria met from spec.md

---

## Notes for AI Implementation

**Critical Dependencies**:
1. Complete Phase 1 before starting any other phase
2. Phases 2 and 3 can run in parallel after Phase 1
3. Phase 4 requires Phases 1, 2, 3 complete
4. Follow the execution strategy for optimal efficiency

**Testing Philosophy**:
- Unit tests mock dependencies (TunnelRegistry, AgentRegistry, AgentConnection)
- Integration tests use real components but mock external services
- End-to-end tests run complete system with real tunnel-agent

**Code Quality**:
- Follow existing code style from Feature 001
- Use structured logging for all operations
- Add comprehensive error handling
- Document public APIs with Doxygen comments
- Ensure thread safety (mutexes where needed)
