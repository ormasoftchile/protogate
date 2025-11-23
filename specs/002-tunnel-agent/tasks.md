# Tunnel Agent - Implementation Tasks

**Feature**: 002-tunnel-agent  
**Total Tasks**: 35  
**Estimated Time**: 2-3 weeks

---

## Phase 1: Project Setup (Tasks 1-5)

- [X] T001 Create tunnel-agent directory structure with Python package layout
- [X] T002 Create requirements.txt with dependencies (h2, httpx, pyyaml, click)
- [X] T003 Create setup.py for package installation
- [X] T004 Create README.md with quick start instructions
- [X] T005 Create tunnel-agent.yaml.example configuration template

**Dependencies**: None  
**Validation**: `python setup.py install` succeeds, imports work

---

## Phase 2: Configuration Module (Tasks 6-10)

- [ ] T006 Implement agent/config.py - Config class with YAML parsing
- [ ] T007 Add CLI argument parsing in config.py using click
- [ ] T008 Add environment variable support (TUNNEL_TOKEN, TUNNEL_ID, etc.)
- [ ] T009 Add config validation (required fields, format checks)
- [ ] T010 Add unit tests for configuration loading (test_config.py)

**Dependencies**: T001-T005  
**Validation**: All config tests pass, CLI --help works

---

## Phase 3: TLS & HTTP/2 Client (Tasks 11-18)

- [ ] T011 Implement agent/client.py - TunnelClient class skeleton
- [ ] T012 Add TLS connection logic using ssl module
- [ ] T013 Add HTTP/2 session setup using h2 library
- [ ] T014 Implement authentication handshake (CONNECT request)
- [ ] T015 Add server response validation (200 vs 401/403)
- [ ] T016 Add connection error handling and logging
- [ ] T017 Add graceful connection close method
- [ ] T018 Add unit tests for client connection (test_client.py)

**Dependencies**: T006-T010  
**Validation**: Agent connects to local server, auth succeeds/fails correctly

---

## Phase 4: Request Forwarding (Tasks 19-25)

- [ ] T019 Implement agent/forwarder.py - RequestForwarder class
- [ ] T020 Add HTTP/2 stream event handler in client.py
- [ ] T021 Implement request parsing from HTTP/2 frames
- [ ] T022 Add HTTP request forwarding to local service using httpx
- [ ] T023 Implement response capture and HTTP/2 frame encoding
- [ ] T024 Add request/response logging with request IDs
- [ ] T025 Add error response generation (502, 504)

**Dependencies**: T011-T018  
**Validation**: End-to-end request flow works (server → agent → local → agent → server)

---

## Phase 5: Health & Monitoring (Tasks 26-30)

- [ ] T026 Implement agent/heartbeat.py - HeartbeatManager class
- [ ] T027 Add PING frame sending every 30 seconds
- [ ] T028 Add PING ACK timeout detection (60 seconds)
- [ ] T029 Add connection health state tracking
- [ ] T030 Add health status logging

**Dependencies**: T011-T018  
**Validation**: Heartbeats sent, connection closes on timeout

---

## Phase 6: Reconnection Logic (Tasks 31-33)

- [ ] T031 Implement agent/reconnect.py - ReconnectionManager class
- [ ] T032 Add exponential backoff logic (1s, 2s, 4s, ..., max 60s)
- [ ] T033 Add reconnection loop with max attempts handling

**Dependencies**: T026-T030  
**Validation**: Agent reconnects after disconnect, backoff timing correct

---

## Phase 7: CLI & Entry Point (Tasks 34-35)

- [ ] T034 Implement agent/main.py - CLI entry point with click
- [ ] T035 Add signal handling (SIGINT, SIGTERM) for graceful shutdown

**Dependencies**: All previous tasks  
**Validation**: `python -m agent --help` works, agent runs end-to-end

---

## Phase 8: Testing (Tasks 36-40)

- [ ] T036 Create tests/test_forwarder.py - Request forwarding tests
- [ ] T037 Create tests/test_heartbeat.py - Heartbeat tests
- [ ] T038 Create tests/test_reconnect.py - Reconnection tests
- [ ] T039 Create integration test with real server
- [ ] T040 Run all tests, ensure >80% coverage

**Dependencies**: T001-T035  
**Validation**: `pytest` passes all tests, coverage report generated

---

## Phase 9: Documentation & Packaging (Tasks 41-45)

- [ ] T041 Write README.md with installation instructions
- [ ] T042 Write QUICKSTART.md with usage examples
- [ ] T043 Add example configurations for common scenarios
- [ ] T044 Create scripts/build.sh for building distributable
- [ ] T045 Create scripts/install.sh for system installation

**Dependencies**: T001-T040  
**Validation**: Documentation complete, install script works

---

## Task Dependencies Graph

```
T001-T005 (Setup)
    ↓
T006-T010 (Config)
    ↓
T011-T018 (TLS/HTTP2)
    ↓
    ├─→ T019-T025 (Forwarding)
    └─→ T026-T030 (Health)
            ↓
        T031-T033 (Reconnect)
            ↓
        T034-T035 (CLI)
            ↓
        T036-T040 (Testing)
            ↓
        T041-T045 (Docs)
```

---

## Parallel Execution

Tasks that can run in parallel:
- T006-T010 [P] (Config module independent)
- T019-T025 and T026-T030 [P] (After client is ready)

---

## Estimated Effort

| Phase | Tasks | Days |
|-------|-------|------|
| 1. Setup | T001-T005 | 0.5 |
| 2. Config | T006-T010 | 1 |
| 3. Client | T011-T018 | 3 |
| 4. Forwarding | T019-T025 | 3 |
| 5. Health | T026-T030 | 2 |
| 6. Reconnect | T031-T033 | 1 |
| 7. CLI | T034-T035 | 0.5 |
| 8. Testing | T036-T040 | 2 |
| 9. Docs | T041-T045 | 1 |
| **Total** | **45 tasks** | **14 days** |

---

## Success Criteria

- [ ] Agent connects to server with valid token
- [ ] HTTP requests forwarded to local service
- [ ] Responses returned to server
- [ ] Heartbeats maintain connection health
- [ ] Auto-reconnection works on disconnect
- [ ] All tests pass with >80% coverage
- [ ] Documentation complete
