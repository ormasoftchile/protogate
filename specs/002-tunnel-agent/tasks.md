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

- [X] T007 Implement src/config/agent_config.h - AgentConfig struct
- [X] T008 Implement src/config/agent_config.cpp - JSON parsing with nlohmann/json
- [X] T009 Add CLI argument parsing using program_options or custom parser
- [X] T010 Add environment variable support
- [X] T011 Add configuration validation
- [ ] T012 Add unit tests for configuration (tests/test_config.cpp)

**Dependencies**: T001-T006  
**Validation**: Config tests pass, CLI --help works

---

## Phase 3: Logging Module (Tasks 13-14)

- [X] T013 Implement src/utils/logger.h/cpp - Wrapper around spdlog
- [X] T014 Add structured logging (JSON format, log levels)

**Dependencies**: T001-T006  
**Validation**: Log messages output correctly

---

## Phase 4: TLS Client (Tasks 15-20)

- [X] T015 Implement src/client/tls_client.h - TLSClient class skeleton
- [X] T016 Add Boost.Asio SSL socket setup
- [X] T017 Add TLS handshake with certificate verification
- [X] T018 Add connection error handling and logging
- [ ] T019 Add graceful connection close
- [ ] T020 Add unit tests for TLS client

**Dependencies**: T007-T014  
**Validation**: Agent connects to server with TLS

---

## Phase 5: HTTP/2 Session (Tasks 21-27)

- [X] T021 Implement src/client/http2_session.h - HTTP2Session class
- [X] T022 Initialize nghttp2 session
- [X] T023 Implement CONNECT handshake with Authorization header
- [X] T024 Add server response validation (200 vs 401/403)
- [X] T025 Add HTTP/2 frame send/receive callbacks
- [X] T026 Add stream event handling
- [ ] T027 Add unit tests for HTTP/2 session

**Dependencies**: T015-T020  
**Validation**: Agent authenticates with server successfully

---

## Phase 6: Request Forwarding (Tasks 28-33)

- [X] T028 Implement src/forwarder/request_forwarder.h - RequestForwarder class
- [X] T029 Add HTTP/2 stream request parsing (headers + body)
- [X] T030 Implement HTTP client for local service using Boost.Beast
- [X] T031 Add request forwarding with header preservation
- [X] T032 Implement response capture and HTTP/2 encoding
- [X] T033 Add error response generation (502, 504)

**Dependencies**: T021-T027  
**Validation**: End-to-end request flow works

---

## Phase 7: Health Monitoring (Tasks 34-36)

- [X] T034 Implement src/health/heartbeat.h/cpp - HeartbeatManager class
- [X] T035 Add HTTP/2 PING frame sending (every 30 seconds)
- [X] T036 Add PING ACK timeout detection (60 seconds)

**Dependencies**: T021-T027  
**Validation**: Heartbeats sent, connection closes on timeout

---

## Phase 8: Reconnection Logic (Tasks 37-38)

- [X] T037 Implement src/utils/reconnect.h/cpp - ReconnectionManager class
- [X] T038 Add exponential backoff reconnection (1s, 2s, 4s, ..., max 60s)

**Dependencies**: T034-T036  
**Validation**: Agent reconnects after disconnect

---

## Phase 9: Main Entry Point (Task 39)

- [X] T039 Implement src/main.cpp - Parse config, start client, run event loop

**Dependencies**: All previous tasks  
**Validation**: `./tunnel-agent --config config.json` runs end-to-end

---

## Phase 10: Testing & Documentation (Task 40)

- [X] T040 Add integration test with real server, verify request flow

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

- [X] Agent connects to server with valid token
- [X] HTTP requests forwarded to local service
- [X] Responses returned to server
- [X] Heartbeats maintain connection health
- [X] Auto-reconnection works on disconnect
- [X] Integration test passes
- [X] Documentation complete
