# Tasks: ProtoGate v0 - WebSocket-Based Tunnel Fabric

**Feature ID**: 003-protogate-v0-websocket  
**Created**: 2025-11-29  
**Tech Stack**: C++20, Boost.Beast (WebSocket), nlohmann/json, yaml-cpp, spdlog, CMake  
**Estimated Duration**: 16-20 hours

---

## Task Format: `- [ ] [ID] [P?] [Story?] Description with file path`

- **Checkbox**: `- [ ]` (REQUIRED - marks task completion)
- **[ID]**: Sequential task number (T001, T002, T003...)
- **[P]**: Parallelizable (different files, no blocking dependencies)
- **[Story]**: User story label (US1, US2, US3, US4) - only for story phases

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Establish CMake build system, directory structure, and basic infrastructure

- [X] T001 Create directory structure: src/{server,agent,common}, include/protogate, tests, examples, cmake
- [X] T002 Create root CMakeLists.txt with C++20 standard, dependency finding (Boost, nlohmann_json, yaml-cpp, spdlog)
- [X] T003 Create src/server/CMakeLists.txt for protogate-server executable
- [X] T004 Create src/agent/CMakeLists.txt for protogate-agent executable
- [X] T005 Create src/common/CMakeLists.txt for shared library (protogate-common)
- [X] T006 [P] Create stub src/server/main.cpp with "Hello from server" message
- [X] T007 [P] Create stub src/agent/main.cpp with "Hello from agent" message
- [X] T008 [P] Create examples/server.yaml with example configuration (agent_port: 8080, tunnel_port: 9000, shared_secret, log_level)
- [X] T009 [P] Create examples/agent.yaml with example configuration (server_url, agent_id, shared_secret, tunnels, heartbeat_interval)
- [X] T010 Create README.md with build instructions, dependencies list, usage examples
- [X] T011 Test build: cmake -B build && cmake --build build (verify both executables compile)

**Checkpoint**: ✅ Project structure created, build system works, stub executables run

---

## Phase 2: Foundational (Core Infrastructure - BLOCKS All User Stories)

**Purpose**: Implement protocol definitions, configuration, and logging that ALL user stories depend on

**⚠️ CRITICAL**: No user story implementation can begin until this phase is 100% complete

- [ ] T012 [P] Create include/protogate/types.h with common types (agent_id, stream_id, timestamps)
- [ ] T013 [P] Create include/protogate/protocol.h with MessageType enum and all message struct definitions (Register, RegisterAck, Heartbeat, OpenTunnel, CloseTunnel, Job, JobResult)
- [ ] T014 Create src/common/protocol.cpp with JSON serialization/deserialization for all message types using nlohmann/json
- [ ] T015 [P] Implement DataFrame class in src/common/protocol.cpp with binary encoding/decoding (stream_id in network byte order + payload)
- [ ] T016 [P] Create include/protogate/config.h with Config struct and load() method declaration
- [ ] T017 Create src/common/config.cpp with YAML parsing using yaml-cpp, config validation, command-line override support
- [ ] T018 [P] Create include/protogate/logger.h with Logger wrapper interface
- [ ] T019 Create src/common/logger.cpp with spdlog integration, structured logging support, log level configuration
- [ ] T020 Create tests/test_protocol.cpp with unit tests for all message types (JSON round-trip, DataFrame encoding/decoding)
- [ ] T021 Test protocol: cd build && ctest (verify all protocol tests pass)

**Checkpoint**: Foundation complete - protocol messages work, config parses correctly, logging available

---

## Phase 3: User Story 1 - Agent Registration and Authentication (Priority: P0) 🎯 MVP

**Goal**: Agents can connect to server, authenticate with pre-shared token, and register successfully

**Independent Test**: 
```bash
# Terminal 1: Start server
./build/protogate-server --config examples/server.yaml
# Terminal 2: Start agent  
./build/protogate-agent --config examples/agent.yaml
# Verify: Server logs show "Agent agent-123 registered"
# Verify: Agent logs show "Registration successful"
```

### Implementation for User Story 1

- [ ] T022 [P] [US1] Create include/protogate/agent_registry.h with AgentRegistry class (register_agent, unregister_agent, get_agent, update_heartbeat methods)
- [ ] T023 [US1] Implement src/server/agent_registry.cpp with thread-safe agent tracking using std::mutex and std::map<agent_id, AgentInfo>
- [ ] T024 [P] [US1] Create src/server/agent_connection.h with AgentConnection class (WebSocket handling, message routing, authentication)
- [ ] T025 [US1] Implement src/server/agent_connection.cpp with Boost.Beast WebSocket accept, upgrade, read loop
- [ ] T026 [US1] Implement handle_register() in src/server/agent_connection.cpp with token validation against config.shared_secret
- [ ] T027 [US1] Implement RegisterMessage sending and RegisterAckMessage receiving in src/agent/agent_client.cpp
- [ ] T028 [US1] Update src/server/main.cpp to create AgentRegistry, TCP acceptor on agent_port, accept loop calling AgentConnection
- [ ] T029 [P] [US1] Create src/agent/agent_client.h with AgentClient class (connect, send_message, message callbacks)
- [ ] T030 [US1] Implement src/agent/agent_client.cpp with Boost.Beast WebSocket client, connection, handshake
- [ ] T031 [US1] Implement do_register() in src/agent/agent_client.cpp to send RegisterMessage on connection
- [ ] T032 [US1] Update src/agent/main.cpp to create AgentClient, connect to server, handle register_ack
- [ ] T033 [US1] Add structured logging for registration events (both server and agent) with agent_id context

**Checkpoint**: Agent connects, registers with token, server acknowledges, connection maintained

---

## Phase 4: User Story 2 - Persistent Connection with Heartbeat (Priority: P0)

**Goal**: Maintain long-lived WebSocket connections and detect agent disconnections within 90 seconds

**Independent Test**:
```bash
# Start server and agent (from US1)
# Wait 35 seconds
# Verify: Server logs show "Heartbeat received from agent-123"
# Kill agent process
# Wait 95 seconds
# Verify: Server logs show "Agent agent-123 removed (heartbeat timeout)"
```

### Implementation for User Story 2

- [ ] T034 [P] [US2] Implement heartbeat timer in src/agent/agent_client.cpp using boost::asio::steady_timer with configurable interval
- [ ] T035 [US2] Implement send_heartbeat() in src/agent/agent_client.cpp to send HeartbeatMessage every 30 seconds
- [ ] T036 [US2] Implement handle_heartbeat() in src/server/agent_connection.cpp to update last_heartbeat timestamp in AgentRegistry
- [ ] T037 [US2] Implement timeout checking timer in src/server/main.cpp to call AgentRegistry::get_timed_out_agents() every 30 seconds
- [ ] T038 [US2] Implement cleanup logic in src/server/main.cpp to close connections and remove timed-out agents from registry
- [ ] T039 [US2] Add structured logging for heartbeat events with timestamps in both server and agent

**Checkpoint**: Heartbeats sent/received, timeout detection works, agents cleaned up after 90 seconds silence

---

## Phase 5: User Story 3 - Stream Multiplexing and Tunnel Opening (Priority: P0)

**Goal**: Support multiple concurrent TCP tunnels over single WebSocket connection with unique stream IDs

**Independent Test**:
```bash
# Start local HTTP server: python3 -m http.server 3000
# Start server and agent (from US1/US2)
# Terminal 1: curl http://localhost:9000/ (should see directory listing)
# Terminal 2: curl http://localhost:9000/ (simultaneously)
# Verify: Both requests succeed independently
# Verify: Server logs show two different stream_ids assigned
```

### Implementation for User Story 3

- [ ] T040 [P] [US3] Create include/protogate/tunnel_manager.h for server with TunnelManager class (accept clients, assign stream_ids, route data frames)
- [ ] T041 [US3] Implement src/server/tunnel_manager.cpp with TCP acceptor on tunnel_port, atomic stream_id counter
- [ ] T042 [US3] Implement handle_client() in src/server/tunnel_manager.cpp to assign unique stream_id and send OpenTunnelMessage to agent
- [ ] T043 [US3] Implement data frame routing in src/server/tunnel_manager.cpp to map stream_id → client socket
- [ ] T044 [P] [US3] Create src/agent/tunnel_manager.h with AgentTunnelManager class (handle open_tunnel, manage local connections)
- [ ] T045 [US3] Implement src/agent/tunnel_manager.cpp to parse OpenTunnelMessage and connect to local target_host:target_port
- [ ] T046 [US3] Implement local connection tracking in src/agent/tunnel_manager.cpp with std::map<stream_id, LocalConnection>
- [ ] T047 [US3] Wire OpenTunnelMessage handling into src/agent/agent_client.cpp message callback to call AgentTunnelManager
- [ ] T048 [US3] Implement CloseTunnelMessage handling in both server and agent to clean up stream state
- [ ] T049 [US3] Update src/server/main.cpp to create TunnelManager and integrate with AgentRegistry
- [ ] T050 [US3] Update src/agent/main.cpp to create AgentTunnelManager and wire into message callbacks
- [ ] T051 [US3] Add structured logging for tunnel open/close events with stream_id and agent_id context

**Checkpoint**: Clients connect to tunnel port, stream_ids assigned, open_tunnel messages sent, local connections established

---

## Phase 6: User Story 4 - TCP Data Forwarding (Priority: P0)

**Goal**: Forward TCP data bidirectionally through WebSocket tunnel using binary data frames

**Independent Test**:
```bash
# Start local HTTP server: python3 -m http.server 3000
# Start server and agent (from previous phases)
# curl http://localhost:9000/ -v
# Verify: Complete HTTP response received (headers + body)
# Verify: Response matches direct connection to port 3000
# Test large transfer: curl http://localhost:9000/large-file.bin -o /tmp/test
# Verify: File transfers correctly (checksum matches)
```

### Implementation for User Story 4

- [ ] T052 [P] [US4] Implement read_from_client() in src/server/tunnel_manager.cpp to read TCP data and create DataFrame with stream_id
- [ ] T053 [US4] Implement send_binary() in src/server/agent_connection.cpp to send DataFrame as WebSocket binary frame
- [ ] T054 [US4] Implement binary frame handling in src/agent/agent_client.cpp to decode DataFrame and extract stream_id + payload
- [ ] T055 [US4] Implement write_to_local() in src/agent/tunnel_manager.cpp to write DataFrame payload to local TCP socket
- [ ] T056 [P] [US4] Implement read_from_local() in src/agent/tunnel_manager.cpp to read from local socket and create DataFrame
- [ ] T057 [US4] Implement DataFrame sending from agent to server in src/agent/agent_client.cpp
- [ ] T058 [US4] Implement write_to_client() in src/server/tunnel_manager.cpp to write DataFrame payload to client socket
- [ ] T059 [US4] Add backpressure handling in both directions (pause reading when write buffer full)
- [ ] T060 [US4] Add error handling for partial reads/writes with proper buffer management
- [ ] T061 [US4] Handle connection close events (client disconnect, local disconnect) and send CloseTunnelMessage
- [ ] T062 [US4] Add structured logging for data transfer events with byte counts

**Checkpoint**: Full HTTP request/response flows through tunnel, large transfers work, both directions functional

---

## Phase 7: User Story 5 - Job Message Protocol (Priority: P1)

**Goal**: Send JSON job messages to agents and receive results (stub implementation for v0)

**Independent Test**:
```bash
# Modify server to send test job after agent registers
# Start server and agent
# Verify: Server logs show "Sent job abc123 to agent-123"
# Verify: Agent logs show "Received job abc123: {type: print, payload: {...}}"
# Verify: Agent logs show "Sent job_result abc123: {status: ok}"
# Verify: Server logs show "Received job_result abc123"
```

### Implementation for User Story 5

- [ ] T063 [P] [US5] Create include/protogate/job_manager.h for server with JobManager class (send_job, handle_job_result, timeout tracking)
- [ ] T064 [US5] Implement src/server/job_manager.cpp with UUID generation for job_id, pending job tracking with std::map
- [ ] T065 [US5] Implement send_job() in src/server/job_manager.cpp to send JobMessage through AgentConnection
- [ ] T066 [US5] Implement timeout checking in src/server/job_manager.cpp with std::chrono timestamps (60 second default)
- [ ] T067 [P] [US5] Create src/agent/job_handler.h with JobHandler class (handle_job, execute_job stub, send_result)
- [ ] T068 [US5] Implement src/agent/job_handler.cpp with stub execute_job() that logs payload and returns success
- [ ] T069 [US5] Implement send_result() in src/agent/job_handler.cpp to send JobResultMessage with job_id
- [ ] T070 [US5] Wire JobMessage handling into src/agent/agent_client.cpp message callback to call JobHandler
- [ ] T071 [US5] Wire JobResultMessage handling into src/server/agent_connection.cpp to call JobManager
- [ ] T072 [US5] Update src/server/main.cpp to create JobManager (optional for v0, can be added later)
- [ ] T073 [US5] Add structured logging for job lifecycle (sent, received, result, timeout) with job_id context

**Checkpoint**: Jobs can be sent to agents, agents log payloads, results returned, timeout detection works

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Error handling, configuration validation, graceful shutdown, documentation

- [ ] T074 [P] Add comprehensive error handling in src/server/agent_connection.cpp for WebSocket errors (catch boost::system::system_error)
- [ ] T075 [P] Add comprehensive error handling in src/agent/agent_client.cpp for connection failures with exponential backoff reconnection
- [ ] T076 [P] Add configuration validation in src/common/config.cpp (required fields, valid port ranges, non-empty secrets)
- [ ] T077 [P] Add command-line argument parsing in both src/server/main.cpp and src/agent/main.cpp using argc/argv
- [ ] T078 [P] Implement graceful shutdown handlers (SIGINT/SIGTERM) in src/server/main.cpp to close all connections
- [ ] T079 [P] Implement graceful shutdown handlers (SIGINT/SIGTERM) in src/agent/main.cpp to send close_tunnel for active streams
- [ ] T080 [P] Add RAII wrappers and smart pointers review across all components (ensure no raw pointers, no manual delete)
- [ ] T081 Update README.md with complete build instructions (dependencies installation per OS)
- [ ] T082 Update README.md with configuration reference (all config options documented)
- [ ] T083 Update README.md with usage examples (multiple scenarios from spec.md)
- [ ] T084 Create tests/integration_test.sh with automated test script covering all 4 user scenarios from spec.md
- [ ] T085 Run integration test script and verify all scenarios pass
- [ ] T086 Test on clean system: Fresh Ubuntu VM or macOS machine to verify README instructions

**Checkpoint**: All error cases handled gracefully, configuration validated, shutdown clean, documentation complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    ↓
Phase 2 (Foundational) ← BLOCKS everything below
    ↓
    ├─→ Phase 3 (US1: Agent Registration) ← MVP Core
    │       ↓
    ├─→ Phase 4 (US2: Heartbeat) ← Depends on US1 connection
    │       ↓
    ├─→ Phase 5 (US3: Tunnel Opening) ← Depends on US1 registry
    │       ↓
    ├─→ Phase 6 (US4: Data Forwarding) ← Depends on US3 streams
    │       ↓
    └─→ Phase 7 (US5: Jobs) ← Can start after US1 (independent of tunnels)
            ↓
       Phase 8 (Polish) ← Depends on all user stories
```

### User Story Dependencies

- **US1 (Registration)**: Can start immediately after Foundational phase
- **US2 (Heartbeat)**: Requires US1 (needs active connection)
- **US3 (Tunnel Opening)**: Requires US1 (needs agent registry)
- **US4 (Data Forwarding)**: Requires US3 (needs stream IDs and open_tunnel)
- **US5 (Jobs)**: Only requires US1 (independent of tunnel functionality)

### Critical Path

T001 → T011 → T012-T021 → T022-T033 → T034-T039 → T040-T051 → T052-T062 → T085

**Estimated Time on Critical Path**: 
- Setup: 2-3 hours
- Foundational: 3-4 hours
- US1: 3-4 hours
- US2: 2 hours
- US3: 4-5 hours
- US4: 5-6 hours
- US5: 2-3 hours (optional, can be deferred)
- Polish: 2-3 hours
- **Total**: 23-30 hours (with jobs), 21-27 hours (MVP without jobs)

### Parallelization Opportunities

**Within Phase 1 (Setup)**:
- T006, T007, T008, T009 can run in parallel (different files)

**Within Phase 2 (Foundational)**:
- T012, T013, T016, T018 can run in parallel (header files)
- T015 can run in parallel after T014

**Within Each User Story**:
- Header files can be created in parallel
- Test writing (if added) can happen in parallel with planning

**Across User Stories (with team)**:
- After US1 completes: US2 and US5 can proceed in parallel
- After US3 completes: US4 continues the tunnel work

---

## Parallel Example: Foundational Phase

```bash
# All header files can be created simultaneously:
T012: Create include/protogate/types.h
T013: Create include/protogate/protocol.h  
T016: Create include/protogate/config.h
T018: Create include/protogate/logger.h

# Then implementation files:
T014: Implement src/common/protocol.cpp (uses T013)
T015: Implement DataFrame in protocol.cpp (can be parallel with T014 if careful)
T017: Implement src/common/config.cpp (uses T016)
T019: Implement src/common/logger.cpp (uses T018)
```

---

## Implementation Strategy

### Minimal MVP (Recommended First Deliverable)

**Scope**: US1 + US2 + US3 + US4 (no jobs)

**Tasks**: T001-T062, T074-T086

**Delivers**: Working tunnel system with authentication, heartbeat, multiplexing, and data forwarding

**Time Estimate**: 21-27 hours

**Validation**: 
```bash
# Complete integration test
python3 -m http.server 3000 &
./build/protogate-server --config examples/server.yaml &
./build/protogate-agent --config examples/agent.yaml &
curl http://localhost:9000/  # Should work!
```

### Full v0 (With Jobs)

**Scope**: US1 + US2 + US3 + US4 + US5

**Tasks**: T001-T086 (all tasks)

**Delivers**: Complete ProtoGate v0 per specification

**Time Estimate**: 23-30 hours

### Incremental Delivery Milestones

1. **After Phase 2**: Foundation builds and tests pass
2. **After US1 (T033)**: Agent can register and authenticate ✅
3. **After US2 (T039)**: Heartbeat mechanism working ✅
4. **After US3 (T051)**: Tunnels open, stream IDs assigned ✅
5. **After US4 (T062)**: **MVP COMPLETE** - Full data forwarding works ✅
6. **After US5 (T073)**: Jobs protocol implemented (optional) ✅
7. **After Phase 8 (T086)**: **v0 COMPLETE** - Production ready ✅

---

## Task Completion Checklist (Per Task)

Before marking any task complete:

- [ ] Code compiles without warnings
- [ ] Added appropriate error handling
- [ ] Added structured logging with context
- [ ] Followed RAII principles (no manual memory management)
- [ ] Used smart pointers (no raw pointers to owned resources)
- [ ] Thread-safe if accessed from multiple threads (use std::mutex)
- [ ] Tested manually (ran the code, verified behavior)
- [ ] Git commit with descriptive message

---

## Success Metrics

### Quantitative Goals

- Build time: < 5 minutes on modern hardware ✅
- Binary size: < 5MB per executable ✅
- Memory usage: < 50MB per component at idle ✅
- Tunnel latency: < 10ms added overhead ✅
- Concurrent streams: > 100 per agent ✅

### Qualitative Goals

- Code is readable and well-commented ✅
- Error messages are clear and actionable ✅
- Logs provide useful debugging information ✅
- Configuration is straightforward ✅
- README instructions work on fresh system ✅

---

## Notes

- **[P] marker**: Tasks that can run in parallel (different files, no blocking dependencies)
- **[Story] marker**: Maps task to user story for traceability and independent testing
- **File paths**: All paths are absolute from repository root (/Volumes/Projects/protogate/)
- **MVP boundary**: Tasks T001-T062 + T074-T086 (excluding US5 jobs)
- **Test strategy**: Manual integration testing (automated tests optional for v0)
- **Commit strategy**: Commit after each completed task or logical group
- **Validation**: Each user story has independent test criteria in phase description

---

**Total Tasks**: 86  
**MVP Tasks**: 75 (excluding US5)  
**Parallelizable Tasks**: ~20 marked with [P]  
**Estimated Duration**: 21-30 hours depending on scope

