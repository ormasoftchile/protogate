# Phase 1: Data Model - Protogate Core Server

**Created**: 2025-11-22  
**Purpose**: Define entities, relationships, validation rules, and state transitions for the tunnel server.

---

## Entity Definitions

### 1. Tunnel

**Description**: Represents a logical tunnel configuration that defines how traffic should be routed to a tunnel agent.

**Attributes**:
```cpp
struct Tunnel {
    std::string tunnel_id;           // Unique identifier (e.g., "api", "printer1")
    TunnelProtocol protocol;         // HTTP or TCP
    std::string target_host;         // Local target on agent side (e.g., "localhost")
    uint16_t target_port;            // Local target port (e.g., 5000, 9100)
    std::vector<std::string> ip_allowlist;  // CIDR ranges (e.g., ["10.0.0.0/24"])
    uint32_t rate_limit_rpm;         // Requests per minute (0 = unlimited)
    std::chrono::system_clock::time_point created_at;
    TunnelStatus status;             // ACTIVE, INACTIVE, SUSPENDED
};

enum class TunnelProtocol { HTTP, TCP };
enum class TunnelStatus { ACTIVE, INACTIVE, SUSPENDED };
```

**Validation Rules**:
- `tunnel_id`: Must be lowercase alphanumeric + hyphens, 3-63 chars, unique
- `protocol`: Must be HTTP or TCP
- `target_host`: Valid hostname or IP address
- `target_port`: 1-65535
- `ip_allowlist`: Valid CIDR notation (validated with regex or parsing library)
- `rate_limit_rpm`: 0 (unlimited) or 1-1000000

**Relationships**:
- 1:N with TunnelAgent (one tunnel config, multiple historical agent connections)
- 1:N with AuthToken (one tunnel, multiple tokens during rotation)

**State Transitions**:
```
INACTIVE --> ACTIVE       (tunnel created and agent connects)
ACTIVE   --> INACTIVE     (agent disconnects, no reconnect within timeout)
ACTIVE   --> SUSPENDED    (admin manually suspends, e.g., for security incident)
SUSPENDED --> ACTIVE      (admin resumes)
```

---

### 2. TunnelAgent

**Description**: Represents an active connection from a tunnel agent to the server.

**Attributes**:
```cpp
struct TunnelAgent {
    std::string connection_id;       // UUID for this connection instance
    std::string tunnel_id;           // References Tunnel
    std::string source_ip;           // IP address of agent
    std::string token_hash;          // SHA-256 hash of auth token
    std::chrono::system_clock::time_point connected_at;
    std::chrono::system_clock::time_point last_heartbeat_at;
    AgentConnectionStatus status;    // CONNECTED, DISCONNECTED, RECONNECTING
    std::string agent_version;       // E.g., "1.0.0" (for compatibility checks)
    
    // Runtime state (not persisted)
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> tls_stream;
    std::shared_ptr<nghttp2_session> http2_session;  // If HTTP protocol
};

enum class AgentConnectionStatus { CONNECTED, DISCONNECTED, RECONNECTING };
```

**Validation Rules**:
- `connection_id`: Must be valid UUID v4
- `tunnel_id`: Must reference existing Tunnel
- `source_ip`: Valid IPv4 or IPv6 address
- `token_hash`: 64-char hex string (SHA-256 output)
- `agent_version`: Semantic versioning format (x.y.z)

**Relationships**:
- N:1 with Tunnel (many agents over time for one tunnel config)
- 1:N with TunnelRequest (one agent connection handles many requests)

**State Transitions**:
```
DISCONNECTED --> CONNECTED      (agent establishes TLS connection, auth succeeds)
CONNECTED    --> RECONNECTING   (heartbeat timeout, keep connection open briefly)
RECONNECTING --> CONNECTED      (agent reconnects within grace period)
RECONNECTING --> DISCONNECTED   (grace period expires, cleanup connection)
CONNECTED    --> DISCONNECTED   (agent explicitly disconnects, auth failure, or error)
```

**Heartbeat Logic**:
- Agent sends heartbeat every 30 seconds
- Server expects heartbeat within 60 seconds
- If no heartbeat for 60s, transition to RECONNECTING (wait 30s more)
- If no heartbeat for 90s total, transition to DISCONNECTED

---

### 3. TunnelRequest

**Description**: Represents an in-flight request being proxied through a tunnel. Used for logging and metrics.

**Attributes**:
```cpp
struct TunnelRequest {
    std::string request_id;          // UUID for this request
    std::string tunnel_id;           // References Tunnel
    std::string connection_id;       // References TunnelAgent
    std::string source_ip;           // Client IP address
    std::string destination;         // Target (hostname:port or URL path)
    TunnelProtocol protocol;         // HTTP or TCP
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    uint64_t bytes_sent;             // Client -> Agent
    uint64_t bytes_received;         // Agent -> Client
    RequestStatus status;            // IN_PROGRESS, COMPLETED, FAILED, TIMEOUT
    std::optional<uint16_t> http_status_code;  // If HTTP protocol
    std::optional<std::string> error_message;  // If FAILED or TIMEOUT
};

enum class RequestStatus { IN_PROGRESS, COMPLETED, FAILED, TIMEOUT };
```

**Validation Rules**:
- `request_id`: Must be valid UUID v4
- `tunnel_id`, `connection_id`: Must reference existing entities
- `source_ip`: Valid IPv4 or IPv6
- `bytes_sent`, `bytes_received`: Non-negative
- `http_status_code`: 100-599 (valid HTTP status codes)

**Relationships**:
- N:1 with Tunnel (many requests for one tunnel)
- N:1 with TunnelAgent (many requests via one agent connection)

**Lifecycle**:
```
IN_PROGRESS --> COMPLETED   (request/response cycle finishes successfully)
IN_PROGRESS --> FAILED      (network error, agent disconnect, auth failure)
IN_PROGRESS --> TIMEOUT     (request exceeds configured timeout)
```

**Logging**:
- Record created when request starts (IN_PROGRESS)
- Updated when request completes (COMPLETED/FAILED/TIMEOUT)
- Logged to Azure Log Analytics as structured JSON

---

### 4. AuthToken

**Description**: Authentication credentials for a tunnel agent. Stored in Azure Key Vault.

**Attributes**:
```cpp
struct AuthToken {
    std::string token_id;            // UUID for this token (Key Vault secret name)
    std::string tunnel_id;           // References Tunnel
    std::string token_hash;          // SHA-256 hash (what's validated on connection)
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> expires_at;  // Optional expiry
    TokenRotationStatus rotation_status;  // ACTIVE, GRACE_PERIOD, REVOKED
};

enum class TokenRotationStatus { ACTIVE, GRACE_PERIOD, REVOKED };
```

**Validation Rules**:
- `token_id`: Must be valid UUID v4
- `tunnel_id`: Must reference existing Tunnel
- `token_hash`: 64-char hex string (SHA-256)
- `expires_at`: If set, must be future timestamp
- Token generation: 256-bit random (32 bytes), base64url-encoded

**Relationships**:
- N:1 with Tunnel (multiple tokens during rotation for one tunnel)

**State Transitions (Token Rotation)**:
```
ACTIVE --> GRACE_PERIOD   (new token generated, old token enters grace period)
GRACE_PERIOD --> REVOKED  (grace period expires, default 5 minutes)
ACTIVE --> REVOKED        (admin manually revokes token)
```

**Token Format**:
```
tnl_<base64url(32-random-bytes)>
Example: tnl_a7f3k9m2p5q8r1s4t6u9v2w5x8y1z4a7
```

**Storage**:
- Plain token stored in Key Vault as secret
- Only hash stored in application memory/logs
- Never log plain token

---

### 5. AuditEvent

**Description**: Security and operational events for compliance and troubleshooting.

**Attributes**:
```cpp
struct AuditEvent {
    std::string event_id;            // UUID for this event
    EventType event_type;
    std::string tunnel_id;           // References Tunnel (if applicable)
    std::optional<std::string> connection_id;  // References TunnelAgent (if applicable)
    std::string source_ip;           // IP address involved in event
    std::chrono::system_clock::time_point timestamp;
    Severity severity;               // INFO, WARNING, ERROR, CRITICAL
    std::string message;             // Human-readable description
    nlohmann::json context;          // Additional structured data
};

enum class EventType {
    AUTH_SUCCESS,
    AUTH_FAILURE,
    TUNNEL_CONNECTED,
    TUNNEL_DISCONNECTED,
    TUNNEL_RECONNECTING,
    RATE_LIMIT_EXCEEDED,
    IP_BLOCKED,
    TOKEN_ROTATED,
    TOKEN_REVOKED,
    KEYVAULT_ERROR,
    CONFIGURATION_LOADED,
    SERVER_STARTED,
    SERVER_STOPPED
};

enum class Severity { INFO, WARNING, ERROR, CRITICAL };
```

**Validation Rules**:
- `event_id`: Must be valid UUID v4
- `timestamp`: Must be accurate system time (UTC)
- `message`: Max 1024 characters
- `context`: Valid JSON object (arbitrary structure)

**Relationships**:
- Optional N:1 with Tunnel
- Optional N:1 with TunnelAgent

**Logging**:
- Every audit event emitted to Azure Log Analytics
- Structured JSON format for queryability
- Retention: minimum 90 days (per constitution)

**Example JSON**:
```json
{
  "event_id": "550e8400-e29b-41d4-a716-446655440000",
  "event_type": "AUTH_FAILURE",
  "tunnel_id": "api",
  "source_ip": "203.0.113.45",
  "timestamp": "2025-11-22T14:32:15Z",
  "severity": "WARNING",
  "message": "Authentication failed: invalid token",
  "context": {
    "token_hash": "a7f3...",
    "user_agent": "tunnel-agent/1.0.0"
  }
}
```

---

## Entity Relationships Diagram

```
┌─────────────┐
│   Tunnel    │
│ (tunnel_id) │
└──────┬──────┘
       │ 1
       │
       │ N
┌──────┴──────────┐
│  TunnelAgent    │ 1       N ┌─────────────────┐
│ (connection_id) ├───────────┤  TunnelRequest  │
└─────────────────┘           │  (request_id)   │
                              └─────────────────┘

┌─────────────┐
│   Tunnel    │ 1       N ┌──────────────┐
│ (tunnel_id) ├───────────┤  AuthToken   │
└─────────────┘           │  (token_id)  │
                          └──────────────┘

┌─────────────┐
│   Tunnel    │ 1    0..N ┌──────────────┐
│ (tunnel_id) ├───────────┤  AuditEvent  │
└─────────────┘           │  (event_id)  │
                          └──────────────┘
```

---

## Data Storage Strategy

### In-Memory (Hot Path)

**Tunnel Registry**:
```cpp
std::unordered_map<std::string, Tunnel> tunnel_registry_;
std::shared_mutex registry_mutex_;  // Reader-writer lock
```

**Active Agents**:
```cpp
std::unordered_map<std::string, std::shared_ptr<TunnelAgent>> active_agents_;
std::mutex agents_mutex_;
```

**Token Cache**:
```cpp
struct CachedToken {
    std::string token_hash;
    std::chrono::steady_clock::time_point expires_at;
};
std::unordered_map<std::string, CachedToken> token_cache_;
std::mutex token_cache_mutex_;
```

### Persistent Storage

**Azure Key Vault**:
- Secrets: AuthToken (plain token)
- Certificates: TLS wildcard cert

**Azure Blob Storage** (MVP):
- Backup of tunnel registry (JSON file, updated every 5 minutes)
- File format: `tunnels-<timestamp>.json`

**Azure Log Analytics**:
- All AuditEvents (via structured stdout)
- All TunnelRequest completions (for metrics)

### Future: Azure Cosmos DB

For multi-instance HA:
- Tunnel Registry → Cosmos DB Table API
- Active Agents → Azure Cache for Redis (shared state)

---

## Concurrency Model

### Thread Safety

**Read-heavy data** (Tunnel registry, token cache):
- `std::shared_mutex` for reader-writer locks
- Multiple concurrent readers, exclusive writer

**Write-heavy data** (Active agents, in-flight requests):
- `std::mutex` for exclusive access
- Short critical sections (<10µs)

**Async I/O**:
- Boost.Asio `io_context` with thread pool (4-8 threads)
- Strands for per-agent serialization

### Lock-Free Structures (Optional Future Optimization)

- `std::atomic<uint64_t>` for counters (active tunnels, request count)
- Lock-free queues for logging (avoid mutex contention)

---

## Validation and Invariants

**Invariants** (must always be true):
1. Every active TunnelAgent references a valid Tunnel
2. Token hash in TunnelAgent matches an ACTIVE or GRACE_PERIOD token
3. No two agents have same connection_id
4. Every TunnelRequest has start_time <= end_time
5. Heartbeat timestamp always increases monotonically

**Validation on data entry**:
- Tunnel ID: Regex `^[a-z0-9-]{3,63}$`
- IP address: Boost.Asio `ip::address::from_string()` (throws on invalid)
- CIDR: Custom parser with validation
- Token hash: Regex `^[0-9a-f]{64}$` (SHA-256 hex)

---

## Summary

All entities defined with attributes, validation rules, relationships, and state transitions. Data storage strategy clarified: in-memory hot path with Azure Key Vault and Blob backup. Ready for Phase 1 contract generation.
