# Tunnel Agent - Implementation Tasks (C++)

**Feature**: 002-tunnel-agent  
**Total Tasks**: 40  
**Estimated Time**: 3 weeks

---

## Phase 1: Project Setup (Tasks 1-6)

- [X] T001 Create tunnel-agent/ directory with CMake structure
- [X] T002 Create CMakeLists.txt with Boost, nghttp2, OpenSSL dependencies
- [X] T003 Create vcpkg.json for dependency management
- [X] T004 Create README.md with build instructions
- [X] T005 Create config.example.json configuration template
- [X] T006 Verify build system works (empty main.cpp compiles)

**Dependencies**: None  
**Validation**: `cmake --build build` succeeds

---

## Phase 2: Configuration Module (Tasks 7-12)

- [ ] T007 Implement src/config/agent_config.h - AgentConfig struct
- [ ] T008 Implement src/config/agent_config.cpp - JSON parsing with nlohmann/json
- [ ] T009 Add CLI argument parsing using program_options or custom parser
- [ ] T010 Add environment variable support
- [ ] T011 Add configuration validation
- [ ] T012 Add unit tests for configuration (tests/test_config.cpp)

**Dependencies**: T001-T006  
**Validation**: Config tests pass, CLI --help works

---

## Phase 3: Logging Module (Tasks 13-14)

- [ ] T013 Implement src/utils/logger.h/cpp - Wrapper around spdlog
- [ ] T014 Add structured logging (JSON format, log levels)

**Dependencies**: T001-T006  
**Validation**: Log messages output correctly

---

## Phase 4: TLS Client (Tasks 15-20)

- [ ] T015 Implement src/client/tls_client.h - TLSClient class skeleton
- [ ] T016 Add Boost.Asio SSL socket setup
- [ ] T017 Add TLS handshake with certificate verification
- [ ] T018 Add connection error handling and logging
- [ ] T019 Add graceful connection close
- [ ] T020 Add unit tests for TLS client

**Dependencies**: T007-T014  
**Validation**: Agent connects to server with TLS

---

## Phase 5: HTTP/2 Session (Tasks 21-27)

- [ ] T021 Implement src/client/http2_session.h - HTTP2Session class
- [ ] T022 Initialize nghttp2 session
- [ ] T023 Implement CONNECT handshake with Authorization header
- [ ] T024 Add server response validation (200 vs 401/403)
- [ ] T025 Add HTTP/2 frame send/receive callbacks
- [ ] T026 Add stream event handling
- [ ] T027 Add unit tests for HTTP/2 session

**Dependencies**: T015-T020  
**Validation**: Agent authenticates with server successfully

---

## Phase 6: Request Forwarding (Tasks 28-33)

- [ ] T028 Implement src/forwarder/request_forwarder.h - RequestForwarder class
- [ ] T029 Add HTTP/2 stream request parsing (headers + body)
- [ ] T030 Implement HTTP client for local service using Boost.Beast
- [ ] T031 Add request forwarding with header preservation
- [ ] T032 Implement response capture and HTTP/2 encoding
- [ ] T033 Add error response generation (502, 504)

**Dependencies**: T021-T027  
**Validation**: End-to-end request flow works

---

## Phase 7: Health Monitoring (Tasks 34-36)

- [ ] T034 Implement src/health/heartbeat.h/cpp - HeartbeatManager class
- [ ] T035 Add HTTP/2 PING frame sending (every 30 seconds)
- [ ] T036 Add PING ACK timeout detection (60 seconds)

**Dependencies**: T021-T027  
**Validation**: Heartbeats sent, connection closes on timeout

---

## Phase 8: Reconnection Logic (Tasks 37-38)

- [ ] T037 Implement src/utils/reconnect.h/cpp - ReconnectionManager class
- [ ] T038 Add exponential backoff reconnection (1s, 2s, 4s, ..., max 60s)

**Dependencies**: T034-T036  
**Validation**: Agent reconnects after disconnect

---

## Phase 9: Main Entry Point (Task 39)

- [ ] T039 Implement src/main.cpp - Parse config, start client, run event loop

**Dependencies**: All previous tasks  
**Validation**: `./tunnel-agent --config config.json` runs end-to-end

---

## Phase 10: Testing & Documentation (Task 40)

- [ ] T040 Add integration test with real server, verify request flow

**Dependencies**: T001-T039  
**Validation**: Integration test passes

---

## Task Dependencies Graph

```
T001-T006 (Setup)
    ↓
    ├─→ T007-T012 (Config)
    └─→ T013-T014 (Logging)
            ↓
        T015-T020 (TLS)
            ↓
        T021-T027 (HTTP/2)
            ↓
            ├─→ T028-T033 (Forwarding)
            └─→ T034-T036 (Heartbeat)
                    ↓
                T037-T038 (Reconnect)
                    ↓
                T039 (Main)
                    ↓
                T040 (Testing)
```

---

## Parallel Execution

Tasks that can run in parallel:
- T007-T012 and T013-T014 [P] (Config and Logging independent)
- T028-T033 and T034-T036 [P] (After HTTP/2 is ready)

---

## Estimated Effort

| Phase | Tasks | Days |
|-------|-------|------|
| 1. Setup | T001-T006 | 1 |
| 2. Config | T007-T012 | 2 |
| 3. Logging | T013-T014 | 0.5 |
| 4. TLS | T015-T020 | 3 |
| 5. HTTP/2 | T021-T027 | 4 |
| 6. Forwarding | T028-T033 | 3 |
| 7. Health | T034-T036 | 1.5 |
| 8. Reconnect | T037-T038 | 1 |
| 9. Main | T039 | 0.5 |
| 10. Testing | T040 | 0.5 |
| **Total** | **40 tasks** | **17 days** |

---

## Success Criteria

- [ ] Agent connects to server with valid token
- [ ] HTTP requests forwarded to local service
- [ ] Responses returned to server
- [ ] Heartbeats maintain connection health
- [ ] Auto-reconnection works on disconnect
- [ ] Integration test passes
- [ ] Documentation complete
