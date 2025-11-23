# Tasks: Protogate Core Server

**Feature**: HTTP/HTTPS and TCP tunneling infrastructure  
**Input**: [spec.md](spec.md), [plan.md](plan.md), [data-model.md](data-model.md), [contracts/](contracts/)  
**Branch**: `001-tunnel-core-server`

**Implementation Strategy**: MVP-first approach - build US1 (HTTP tunneling) as minimal viable product, then expand with US2-US6.

**Tests**: Not explicitly requested in specification, but constitution requires 80% code coverage. Test tasks included for quality assurance.

---

## Task Format: `- [ ] [ID] [P?] [Story?] Description with file path`

- **[P]**: Parallelizable (different files, no blocking dependencies)
- **[Story]**: User story label (US1-US6) for phase tasks, omitted for Setup/Foundation/Polish phases
- All file paths are absolute from project root

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Create project structure, configure build system, set up dependency management

- [X] T001 Create project directory structure as defined in plan.md (src/, tests/, deploy/, docs/)
- [X] T002 Initialize CMake project with CMakeLists.txt at root, set C++17 minimum, configure vcpkg integration
- [X] T003 [P] Create vcpkg.json manifest with dependencies: boost-asio, openssl, nghttp2, nlohmann-json, gtest, benchmark
- [X] T004 [P] Create .gitignore for C++ project (build/, vcpkg_installed/, *.o, *.a, compile_commands.json)
- [X] T005 [P] Create README.md with project overview, build instructions, prerequisites
- [X] T006 [P] Create docker/Dockerfile.alpine with multi-stage build (builder + runtime)
- [X] T007 [P] Create .clang-format for code style consistency (Google or LLVM style)
- [X] T008 Configure GitHub Actions CI workflow (.github/workflows/ci.yml) for build, test, lint

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure required by ALL user stories - MUST be complete before any user story work

⚠️ **CRITICAL**: No user story implementation can begin until this phase is complete

- [X] T009 Create src/utils/config.h/cpp - environment variable parsing (PORT, KEY_VAULT_URI, LOG_ANALYTICS_WORKSPACE_ID, DNS_ZONE)
- [X] T010 Create src/utils/errors.h/cpp - error types (TunnelError, AuthError, NetworkError) with std::error_code
- [X] T011 Create src/observability/logger.h/cpp - structured JSON logger to stdout/Log Analytics with severity levels
- [X] T012 [P] Create src/models/tunnel.h/cpp - Tunnel entity struct with validation methods
- [X] T013 [P] Create src/models/tunnel_agent.h/cpp - TunnelAgent entity struct with connection state
- [X] T014 [P] Create src/models/tunnel_request.h/cpp - TunnelRequest entity for in-flight tracking
- [X] T015 [P] Create src/models/auth_token.h/cpp - AuthToken entity with SHA-256 hashing
- [X] T016 [P] Create src/models/audit_event.h/cpp - AuditEvent entity for security logging
- [X] T017 Create src/storage/cache.h/cpp - in-memory std::unordered_map with std::shared_mutex for tunnel registry
- [X] T018 Create src/server/io_context_pool.h/cpp - Boost.Asio io_context thread pool (1 per CPU core)
- [X] T019 Create src/utils/async_utils.h/cpp - async helper utilities (awaitable wrappers, timeout handlers)
- [X] T020 Create tests/unit/config_test.cpp - unit tests for environment variable parsing
- [X] T021 [P] Create tests/unit/logger_test.cpp - unit tests for JSON log formatting

**Checkpoint**: ✅ Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - HTTP Tunnel Establishment and Traffic Routing (Priority: P1) 🎯 MVP

**Goal**: Enable HTTP/HTTPS tunneling with TLS termination, token authentication, and request proxying

**Independent Test**: Deploy server, connect one agent with token, send HTTP GET to tunnel endpoint, verify response

### Implementation Tasks

- [X] T022 [P] [US1] Create src/security/tls_manager.h/cpp - TLS context initialization, certificate loading from Key Vault
- [X] T023 [P] [US1] Create src/security/token_validator.h/cpp - SHA-256 token validation against cache
- [X] T024 [US1] Create src/storage/keyvault_client.h/cpp - Azure Key Vault SDK wrapper for certificates and secrets
- [X] T025 [US1] Implement src/agent/agent_connection.h/cpp - manages single agent TLS connection, HTTP/2 session, heartbeat
- [X] T026 [US1] Implement src/agent/agent_registry.h/cpp - thread-safe registry of active agents (std::unordered_map + std::shared_mutex)
- [X] T027 [US1] Implement src/proxy/protocol_multiplexer.h/cpp - HTTP/2 stream management via nghttp2 callbacks
- [X] T028 [US1] Implement src/proxy/http_proxy.h/cpp - HTTP request parsing, header forwarding, response streaming
- [X] T029 [US1] Implement src/server/http_server.h/cpp - accepts HTTPS connections on port 443, SNI routing
- [X] T030 [US1] Implement src/server/agent_server.h/cpp - accepts agent TLS connections on port 8443, handshake handling
- [X] T031 [US1] Create src/server/main.cpp - main entry point, config loading, server startup, signal handling
- [X] T032 [US1] Add HTTP request routing logic in http_proxy.cpp - match hostname to tunnel_id, lookup agent, forward request
- [X] T033 [US1] Add connection timeout handling in agent_connection.cpp - 30-minute request timeout, configurable

### Testing Tasks

- [X] T034 [US1] Create tests/integration/http_tunnel_test.cpp - end-to-end HTTP GET/POST/PUT through tunnel
- [X] T035 [P] [US1] Create tests/integration/auth_test.cpp - valid token succeeds, invalid token rejected (401)
- [X] T036 [P] [US1] Create tests/integration/tls_test.cpp - verify TLS 1.2+ required, reject TLS 1.0/1.1
- [X] T037 [US1] Create tests/performance/http_throughput_bench.cpp - measure requests/sec with wrk or Apache Bench
- [X] T038 [US1] Create tests/unit/token_validator_test.cpp - SHA-256 hashing, cache hit/miss, expiration

**Checkpoint**: ✅ User Story 1 complete - HTTP tunneling functional, testable independently

**Build Status**: ✅ All compilation issues resolved - clean build achieved
- Fixed C++17/20 compatibility (string operations, Boost.Asio work guards)
- Corrected model field mappings and type mismatches
- Server binary successfully built (protogate-server)

---

## Phase 4: User Story 2 - TCP Tunnel for Printer Traffic (Priority: P1) 🎯 MVP

**Goal**: Enable raw TCP tunneling for printer protocols (port 9100), byte-for-byte forwarding

**Independent Test**: Configure TCP tunnel on port 9100, send raw TCP data, verify data reaches local socket intact

### Implementation Tasks

- [X] T039 [P] [US2] Create src/proxy/tcp_proxy.h/cpp - raw TCP stream forwarding with zero-copy (splice/sendfile)
- [X] T040 [P] [US2] Implement binary protocol framing in protocol_multiplexer.cpp - TCP_DATA, TCP_CLOSE, TCP_ERROR frames
- [X] T041 [US2] Create src/server/tcp_server.h/cpp - accepts TCP connections on configurable ports, routes by port number
- [X] T042 [US2] Add TCP connection tracking in tunnel_request.cpp - bytes_sent/received counters, connection status
- [X] T043 [US2] Implement flow control in tcp_proxy.cpp - 64KB window, backpressure handling
- [X] T044 [US2] Add TCP tunnel configuration in config.cpp - parse TCP_PORTS environment variable (comma-separated)

### Testing Tasks

- [X] T045 [US2] Create tests/integration/tcp_tunnel_test.cpp - send 1MB data through TCP tunnel, verify byte-for-byte match
- [X] T046 [P] [US2] Create tests/integration/tcp_reconnect_test.cpp - simulate network interruption, verify recovery
- [X] T047 [US2] Create tests/performance/tcp_throughput_bench.cpp - measure Mbps with iperf3
- [X] T048 [P] [US2] Create tests/unit/tcp_proxy_test.cpp - test frame parsing, sequence numbers, connection close

**Checkpoint**: ✅ User Story 2 complete - TCP tunneling functional for printer protocols

---

## Phase 5: User Story 3 - Tunnel Registration and Token Management (Priority: P2)

**Goal**: API for creating tunnels, generating tokens, storing in Key Vault, token rotation

**Independent Test**: Call management API to create tunnel, verify token stored in Key Vault, connect agent successfully

### Implementation Tasks

- [X] T049 [P] [US3] Implement POST /api/v1/tunnels endpoint in src/api/tunnels_handler.cpp
- [X] T050 [P] [US3] Implement GET /api/v1/tunnels endpoint in src/api/tunnels_handler.cpp
- [X] T051 [P] [US3] Implement GET /api/v1/tunnels/{id} endpoint in src/api/tunnels_handler.cpp
- [X] T052 [P] [US3] Implement DELETE /api/v1/tunnels/{id} endpoint in src/api/tunnels_handler.cpp
- [X] T053 [P] [US3] Implement POST /api/v1/tunnels/{id}/rotate-token endpoint in src/api/tunnels_handler.cpp
- [X] T054 [US3] Add token generation logic in auth_token.cpp - cryptographically secure 256-bit tokens
- [X] T055 [US3] Add Key Vault token storage in keyvault_client.cpp - store token as secret, return secret URI
- [X] T056 [US3] Implement token rotation with grace period in token_validator.cpp - maintain old + new token for 5 minutes
- [ ] T057 [US3] Add tunnel registry persistence in src/storage/tunnel_registry.h/cpp - backup to Azure Blob Storage every 5 minutes
- [X] T058 [US3] Create src/api/router.h/cpp - HTTP request router for management API endpoints

### Testing Tasks

- [X] T059 [US3] Create tests/integration/management_api_test.cpp - CRUD operations for tunnels
- [X] T060 [P] [US3] Create tests/integration/token_rotation_test.cpp - rotate token, verify old token valid for 5 min
- [X] T061 [P] [US3] Create tests/unit/token_generation_test.cpp - verify 256-bit entropy, no collisions
- [X] T062 [US3] Create tests/integration/keyvault_integration_test.cpp - store/retrieve secrets from Key Vault

**Checkpoint**: ✅ User Story 3 complete - Management API functional, token lifecycle managed

---

## Phase 6: User Story 4 - TLS Termination with Custom Domain (Priority: P2)

**Goal**: Load wildcard certificates from Key Vault, serve HTTPS with custom domain, hot reload certificates

**Independent Test**: Upload cert to Key Vault, start server, verify HTTPS endpoint serves with valid certificate

### Implementation Tasks

- [X] T063 [US4] Implement wildcard certificate loading in tls_manager.cpp - parse PFX/PEM from Key Vault
- [X] T064 [US4] Implement SNI routing in http_server.cpp - match SNI hostname to certificate
- [X] T065 [US4] Add certificate hot reload in tls_manager.cpp - watch Key Vault for cert updates, reload without downtime
- [X] T066 [US4] Add TLS version enforcement in tls_manager.cpp - reject TLS 1.0/1.1, require 1.2+ via OpenSSL context
- [X] T067 [US4] Create src/observability/metrics.h/cpp - metrics collection (active tunnels, throughput, latency)

### Testing Tasks

- [X] T068 [US4] Create tests/integration/wildcard_cert_test.cpp - verify multiple subdomains (api.tunnel.x, app.tunnel.x) use same cert
- [X] T069 [P] [US4] Create tests/integration/cert_reload_test.cpp - upload new cert, verify loaded without restart
- [X] T070 [P] [US4] Create tests/security/tls_validation_test.cpp - reject TLS 1.0/1.1, accept 1.2/1.3

**Checkpoint**: ✅ User Story 4 complete - TLS termination with custom domains operational

---

## Phase 7: User Story 5 - IP Allowlisting and Security Controls (Priority: P3)

**Goal**: CIDR-based IP filtering, defense-in-depth for sensitive tunnels

**Independent Test**: Configure IP allowlist, send request from allowed IP (succeeds), blocked IP (403 Forbidden)

### Implementation Tasks

- [ ] T071 [P] [US5] Create src/security/ip_allowlist.h/cpp - CIDR parsing, IP matching logic
- [ ] T072 [US5] Add IP filtering in http_proxy.cpp - check source IP against tunnel's allowlist before forwarding
- [ ] T073 [US5] Add IP filtering in tcp_proxy.cpp - check source IP for TCP connections
- [ ] T074 [US5] Add rate limiting in src/security/rate_limiter.h/cpp - token bucket algorithm per tunnel
- [ ] T075 [US5] Implement 403 Forbidden response in http_server.cpp when IP blocked
- [ ] T076 [US5] Add security audit logging in audit_event.cpp - log IP allowlist violations with source IP

### Testing Tasks

- [ ] T077 [US5] Create tests/integration/ip_allowlist_test.cpp - allowed IP passes, blocked IP denied
- [ ] T078 [P] [US5] Create tests/integration/rate_limit_test.cpp - exceed limit, verify 429 Too Many Requests
- [ ] T079 [P] [US5] Create tests/unit/ip_allowlist_test.cpp - CIDR parsing, IP match logic (IPv4 and IPv6)

**Checkpoint**: ✅ User Story 5 complete - IP allowlisting and rate limiting operational

---

## Phase 8: User Story 6 - Health Monitoring and Observability (Priority: P3)

**Goal**: Structured logging to Log Analytics, metrics export to Azure Monitor, health check endpoint, alerting

**Independent Test**: Generate tunnel traffic, verify logs appear in Log Analytics, metrics exported, alerts configured

### Implementation Tasks

- [ ] T080 [P] [US6] Implement Azure Log Analytics client in logger.cpp - batch log upload via REST API
- [ ] T081 [P] [US6] Implement Azure Monitor metrics export in metrics.cpp - custom metrics via REST API
- [ ] T082 [US6] Create /health endpoint in src/server/health_handler.cpp - return 200 OK with status JSON
- [ ] T083 [US6] Add OpenTelemetry tracing support in src/observability/tracer.h/cpp - distributed tracing spans
- [ ] T084 [US6] Implement GET /api/v1/tunnels/{id}/metrics endpoint in tunnels_handler.cpp - return request counts, latency percentiles
- [ ] T085 [US6] Implement GET /api/v1/tunnels/{id}/agents endpoint in tunnels_handler.cpp - list active agent connections
- [ ] T086 [US6] Add latency histogram tracking in tunnel_request.cpp - p50, p95, p99 calculations

### Testing Tasks

- [ ] T087 [US6] Create tests/integration/observability_test.cpp - verify logs sent to Log Analytics, metrics exported
- [ ] T088 [P] [US6] Create tests/integration/health_check_test.cpp - /health returns 200 when healthy, 503 when degraded
- [ ] T089 [P] [US6] Create tests/unit/metrics_test.cpp - histogram calculations, percentile accuracy

**Checkpoint**: ✅ User Story 6 complete - Full observability stack operational

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Deployment automation, documentation, security hardening, production readiness

- [ ] T090 Create deploy/azure/main.bicep - root Bicep template with all resources
- [ ] T091 [P] Create deploy/azure/container-app.bicep - Container Apps resource definition
- [ ] T092 [P] Create deploy/azure/keyvault.bicep - Key Vault with access policies
- [ ] T093 [P] Create deploy/azure/dns-zone.bicep - DNS Zone with wildcard A record
- [ ] T094 [P] Create deploy/azure/log-analytics.bicep - Log Analytics workspace
- [ ] T095 Create deploy/azure/parameters.json - deployment parameters template
- [ ] T096 Create deploy/scripts/deploy.sh - automated deployment script with az CLI
- [ ] T097 [P] Create deploy/scripts/generate-token.sh - utility for manual token generation
- [ ] T098 Create docs/architecture.md - high-level architecture diagram with C4 model
- [ ] T099 [P] Create docs/security.md - threat model, security controls, incident response
- [ ] T100 [P] Create docs/adr/001-async-io-library.md - ADR for Boost.Asio choice
- [ ] T101 [P] Create docs/adr/002-tls-library-choice.md - ADR for OpenSSL choice
- [ ] T102 [P] Create docs/adr/003-storage-strategy.md - ADR for in-memory + Blob backup
- [ ] T103 Add memory safety checks - integrate AddressSanitizer in CMakeLists.txt for debug builds
- [ ] T104 Add static analysis - integrate clang-tidy and cppcheck in CI pipeline
- [ ] T105 Implement graceful shutdown in main.cpp - SIGTERM handler, drain connections, cleanup resources
- [ ] T106 Add connection limit enforcement in agent_server.cpp - reject new agents when at capacity (50 concurrent)
- [ ] T107 Create tests/security/token_fuzzer.cpp - fuzz token validation with random inputs
- [ ] T108 Run full integration test suite - verify all user stories work together
- [ ] T109 Run performance benchmark suite - verify SC-001 through SC-004 success criteria met
- [ ] T110 Update README.md with complete quickstart guide, deployment instructions, troubleshooting

**Final Checkpoint**: ✅ Protogate Core Server production-ready

---

## Dependency Graph (User Story Completion Order)

**Critical Path** (sequential):
1. **Phase 1 (Setup)** → **Phase 2 (Foundation)** → **US1 (HTTP Tunneling)**
2. **US1** → **US4 (TLS Termination)** *(US4 requires TLS infrastructure from US1)*
3. **US1** → **US3 (Token Management)** *(US3 enhances auth from US1)*

**Parallel Paths** (can be developed simultaneously after Foundation):
- **US1 (HTTP)** and **US2 (TCP)** are independent - can develop in parallel
- **US5 (IP Allowlist)** can start after US1 or US2 completes - adds filtering to either
- **US6 (Observability)** can start anytime - integrates throughout codebase

**Recommended Story Order**:
1. **Phase 1 + Phase 2** (Setup + Foundation) - ~3-4 days
2. **US1** (HTTP Tunneling) - ~5-7 days → **First deployable MVP**
3. **US2** (TCP Tunneling) - ~3-4 days (parallel with US4 if 2+ developers)
4. **US4** (TLS Termination) - ~2-3 days
5. **US3** (Token Management) - ~3-4 days
6. **US5** (IP Allowlist) - ~2 days
7. **US6** (Observability) - ~2-3 days
8. **Phase 9** (Polish) - ~2-3 days

**Total Estimate**: 3-4 weeks (single developer, full-time)

---

## Parallel Execution Examples

### After Phase 2 Complete (Foundation Ready):

**Option 1**: Single developer - sequential execution
- Week 1-2: US1 → First demo-able MVP
- Week 2-3: US2, US4 → Production-ready tunneling
- Week 3-4: US3, US5, US6 → Full feature set

**Option 2**: Two developers - parallel tracks
- **Developer A**: US1 (HTTP) → US3 (Management API) → US6 (Observability)
- **Developer B**: Foundation helpers → US2 (TCP) → US4 (TLS) → US5 (IP Filter)
- Timeline: ~2-3 weeks to complete all stories

**Option 3**: Three developers - maximum parallelization
- **Developer A**: US1 (HTTP tunneling core)
- **Developer B**: US2 (TCP tunneling core)
- **Developer C**: Foundation + US4 (TLS) + US5 (Security)
- Then converge on US3, US6, Polish
- Timeline: ~2 weeks to complete all stories

### Parallelizable Task Groups Within Each Story:

**US1 Example** (5 tasks in parallel after T022-T024 complete):
- T025 (agent_connection.cpp)
- T026 (agent_registry.cpp)
- T027 (protocol_multiplexer.cpp)
- T028 (http_proxy.cpp)
- T029 (http_server.cpp)

**Testing Tasks** (always parallelizable):
- All test files can be created simultaneously since they're independent
- T034, T035, T036, T037, T038 for US1 can all run in parallel

---

## Implementation Strategy

**MVP Definition** (Minimum Viable Product):
- Phase 1 (Setup) + Phase 2 (Foundation) + US1 (HTTP Tunneling) = **First deployable version**
- Delivers core value: secure HTTP tunneling with TLS and token auth
- Can be deployed to Azure Container Apps and used for Printer4All HTTP API testing
- Estimated: 2 weeks (single developer)

**Incremental Delivery**:
- **Sprint 1** (2 weeks): Setup + Foundation + US1 → Deploy MVP
- **Sprint 2** (1 week): US2 (TCP) → Add printer support
- **Sprint 3** (1 week): US4 (TLS) + US3 (Management API) → Production hardening
- **Sprint 4** (1 week): US5 (Security) + US6 (Observability) + Polish → Full feature set

**Validation Strategy**:
- After each user story: Run story's integration tests, verify independent functionality
- After each sprint: Run full test suite, performance benchmarks
- Before merge to main: Constitutional compliance check (all 5 principles), security review, performance validation

---

## Constitutional Compliance Validation

Per constitution requirements, all tasks satisfy:

✅ **I. Security-First**:
- T022-T024: TLS 1.2+ mandatory, token validation, Key Vault integration
- T070: Security tests reject TLS 1.0/1.1
- T071-T076: IP allowlisting, rate limiting, audit logging
- T107: Security fuzzing for token validation

✅ **II. Azure-Native**:
- T024: Azure Key Vault SDK integration
- T080-T081: Log Analytics and Azure Monitor integration
- T090-T094: Bicep templates for Container Apps, DNS Zone, Key Vault

✅ **III. Self-Hosting Control**:
- T009: Environment variable configuration (12-factor)
- T057: Backup to customer's Azure Blob Storage
- All resources deploy to customer subscription per Bicep templates

✅ **IV. Performance & Cost Efficiency**:
- T037, T047: Performance benchmarks for throughput and latency
- T043: Zero-copy TCP forwarding
- T106: Graceful degradation (reject connections vs crash)

✅ **V. Observability & Auditability**:
- T011, T016: Structured JSON logging, audit events
- T080-T086: Full observability stack (logs, metrics, tracing, health checks)
- T087-T089: Observability integration tests

**Code Quality Standards**:
- T020-T021, T034-T038, T045-T048, etc.: 80%+ code coverage target
- T103: AddressSanitizer for memory safety
- T104: Static analysis (clang-tidy, cppcheck)
- T008: CI/CD with all quality gates

---

**Total Tasks**: 110  
**Estimated Effort**: 21 story points / 3-4 weeks (single developer)  
**MVP Scope**: T001-T038 (Phase 1 + Phase 2 + US1) = ~2 weeks  
**Format Validation**: ✅ All tasks follow checklist format with IDs, story labels, file paths

**Next Step**: Begin Phase 1 (Setup) tasks T001-T008 to initialize project structure.
