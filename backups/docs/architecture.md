# Architecture: Protogate Core Server

**Version**: 1.0  
**Last Updated**: 2025-01-23  
**Status**: Production

## Table of Contents

1. [Overview](#overview)
2. [C4 Model Diagrams](#c4-model-diagrams)
3. [Component Architecture](#component-architecture)
4. [Data Flow](#data-flow)
5. [Deployment Architecture](#deployment-architecture)
6. [Integration Points](#integration-points)

---

## Overview

Protogate Core Server is a high-performance reverse tunneling server written in C++17. It enables secure HTTP/HTTPS and TCP traffic routing through persistent outbound agent connections, solving the common problem of accessing services behind firewalls and NATs.

**Key Capabilities**:
- **HTTP/HTTPS Tunneling**: Routes inbound HTTP requests through agent connections with <10ms overhead
- **TCP Tunneling**: Supports raw TCP traffic forwarding with 100+ Mbps throughput
- **TLS Termination**: Handles TLS encryption/decryption at the server edge
- **Token Authentication**: Validates tunnel agents using cryptographically secure tokens
- **IP Allowlisting**: CIDR-based IP filtering for enhanced security
- **Azure Integration**: Native integration with Key Vault, DNS, and Log Analytics

**Technical Foundation**:
- **Language**: C++17 with Boost.Asio for async I/O
- **Performance**: <512MB memory for 50 concurrent tunnels, zero-copy TCP forwarding
- **Security**: TLS 1.2+ mandatory, 256-bit tokens, SHA-256 hashing
- **Observability**: Structured JSON logging to Azure Log Analytics

---

## C4 Model Diagrams

### Level 1: System Context

```
┌──────────────────────────────────────────────────────────────────┐
│                     External Internet Users                       │
│              (Web browsers, API clients, TCP clients)             │
└──────────────────┬───────────────────────────────────────────────┘
                   │ HTTPS/TCP
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│                     Protogate Core Server                         │
│         Reverse Tunneling Infrastructure (C++ Server)             │
│  - Routes HTTP/HTTPS traffic through tunnel agents                │
│  - Forwards TCP connections through persistent tunnels            │
│  - Authenticates agents with secure tokens                        │
└──────────────────┬───────────────────────────────────────────────┘
                   │ TLS + Token Auth
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│                     Tunnel Agents                                 │
│               (Behind customer firewalls/NATs)                    │
│  - Maintain persistent outbound connections to server             │
│  - Forward traffic to local services (printers, APIs, etc.)       │
└──────────────────┬───────────────────────────────────────────────┘
                   │ HTTP/TCP
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│                   Local Services                                  │
│            (Printers, APIs, Internal Web Apps)                    │
└──────────────────────────────────────────────────────────────────┘

Supporting Systems:
┌─────────────────┐  ┌──────────────────┐  ┌─────────────────────┐
│ Azure Key Vault │  │ Azure DNS Zone   │  │ Azure Log Analytics │
│ (Token Storage) │  │ (Wildcard DNS)   │  │ (Observability)     │
└─────────────────┘  └──────────────────┘  └─────────────────────┘
```

### Level 2: Container Diagram

```
┌───────────────────────────────────────────────────────────────────────┐
│                        Protogate Core Server                          │
│                     (Azure Container Apps)                            │
│                                                                       │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐   │
│  │   HTTP Server    │  │   Agent Server   │  │  Management API  │   │
│  │  (Port 443)      │  │  (Port 8443)     │  │  (Port 443)      │   │
│  │  - TLS Term      │  │  - Agent Auth    │  │  - REST API      │   │
│  │  - HTTP/2        │  │  - Token Valid   │  │  - OAuth 2.0     │   │
│  │  - Route Match   │  │  - Conn Pooling  │  │  - CRUD Ops      │   │
│  └────────┬─────────┘  └────────┬─────────┘  └──────────────────┘   │
│           │                     │                                     │
│           └──────────┬──────────┘                                     │
│                      │                                                │
│           ┌──────────▼──────────┐                                     │
│           │  Protocol Mux       │                                     │
│           │  - HTTP/TCP routing │                                     │
│           │  - Request matching │                                     │
│           │  - Load balancing   │                                     │
│           └──────────┬──────────┘                                     │
│                      │                                                │
│           ┌──────────▼──────────┐                                     │
│           │  Tunnel Registry    │                                     │
│           │  - In-memory cache  │                                     │
│           │  - Agent tracking   │                                     │
│           │  - Health checks    │                                     │
│           └──────────┬──────────┘                                     │
│                      │                                                │
│           ┌──────────▼──────────┐                                     │
│           │  Observability      │                                     │
│           │  - JSON logger      │                                     │
│           │  - Metrics exporter │                                     │
│           │  - Audit events     │                                     │
│           └─────────────────────┘                                     │
└───────────────────────────────────────────────────────────────────────┘
           │                      │                      │
           ▼                      ▼                      ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ Azure Key Vault  │  │ Azure DNS Zone   │  │Azure Log         │
│ - Token storage  │  │ - *.tunnel.com   │  │Analytics         │
│ - TLS certs      │  │ - Wildcard A rec │  │- Structured logs │
└──────────────────┘  └──────────────────┘  └──────────────────┘
```

### Level 3: Component Diagram (Core Server)

```
┌────────────────────────────────────────────────────────────────────┐
│                          HTTP Server                                │
│ ┌────────────────┐  ┌────────────────┐  ┌────────────────┐        │
│ │ TLS Acceptor   │──│ HTTP Parser    │──│ Request Router │        │
│ │ (OpenSSL)      │  │ (nghttp2)      │  │ (Host header)  │        │
│ └────────────────┘  └────────────────┘  └────────┬───────┘        │
└──────────────────────────────────────────────────│────────────────┘
                                                    │
┌────────────────────────────────────────────────────────────────────┐
│                         Agent Server                                │
│ ┌────────────────┐  ┌────────────────┐  ┌────────────────┐        │
│ │ Agent Acceptor │──│ Token Validator│──│ Agent Registry │        │
│ │ (TLS)          │  │ (SHA-256)      │  │ (Connection    │        │
│ └────────────────┘  └────────────────┘  │  Pool)         │        │
│                                          └────────┬───────┘        │
└──────────────────────────────────────────────────│────────────────┘
                                                    │
                    ┌───────────────────────────────┼───────────────┐
                    │         Protocol Multiplexer                  │
                    │ ┌──────────────┐  ┌─────────────────┐        │
                    │ │ HTTP Proxy   │  │ TCP Forwarder   │        │
                    │ │ - Frame fwd  │  │ - Zero-copy     │        │
                    │ │ - Buffering  │  │ - splice()      │        │
                    │ └──────────────┘  └─────────────────┘        │
                    └───────────────────────────────────────────────┘
                                        │
                    ┌───────────────────┼───────────────────────────┐
                    │         Storage Layer                         │
                    │ ┌──────────────┐  ┌─────────────────┐        │
                    │ │ Tunnel Cache │  │ Key Vault Client│        │
                    │ │ (std::map)   │  │ (Azure SDK)     │        │
                    │ └──────────────┘  └─────────────────┘        │
                    └───────────────────────────────────────────────┘
                                        │
                    ┌───────────────────┼───────────────────────────┐
                    │         Observability                         │
                    │ ┌──────────────┐  ┌─────────────────┐        │
                    │ │ Logger       │  │ Metrics Exporter│        │
                    │ │ (JSON)       │  │ (Azure Monitor) │        │
                    │ └──────────────┘  └─────────────────┘        │
                    └───────────────────────────────────────────────┘
```

---

## Component Architecture

### 1. HTTP Server (`http_server.cpp`)

**Responsibility**: Accept inbound HTTPS connections, terminate TLS, parse HTTP requests, route to appropriate tunnel agents.

**Key Classes**:
- `HttpServer`: Main server class, manages listener and TLS acceptor
- `HttpConnection`: Per-connection handler, owns socket and request parser
- `RequestRouter`: Maps Host header to tunnel agent connections

**Dependencies**:
- OpenSSL (TLS termination)
- nghttp2 (HTTP/2 parsing)
- Protocol Multiplexer (request forwarding)
- Tunnel Registry (agent lookup)

**Performance**:
- Async I/O with Boost.Asio
- Zero-copy for large payloads (sendfile)
- Connection pooling (keep-alive)

### 2. Agent Server (`agent_server.cpp`)

**Responsibility**: Accept agent connections, validate tokens, maintain persistent connections, manage agent health.

**Key Classes**:
- `AgentServer`: Listener for agent connections
- `AgentConnection`: Per-agent connection handler
- `AgentRegistry`: Connection pool and health tracker

**Dependencies**:
- OpenSSL (TLS for agent connections)
- Token Validator (authentication)
- Protocol Multiplexer (bidirectional traffic forwarding)

**Performance**:
- 50 concurrent agents per instance
- Heartbeat every 30 seconds
- Automatic reconnection on failure

### 3. Protocol Multiplexer (`protocol_multiplexer.cpp`)

**Responsibility**: Route HTTP/TCP traffic between HTTP server and agent connections. Supports both HTTP request/response proxying and raw TCP forwarding.

**Key Classes**:
- `ProtocolMultiplexer`: Main routing engine
- `HttpProxy`: HTTP-specific forwarding logic
- `TcpForwarder`: Zero-copy TCP stream forwarding

**Dependencies**:
- Tunnel Registry (routing table)
- Agent Registry (target agent selection)

**Performance**:
- Zero-copy TCP forwarding with splice()
- Request pipelining for HTTP
- Load balancing across multiple agents (same tunnel ID)

### 4. Tunnel Registry (`cache.cpp`)

**Responsibility**: In-memory storage of active tunnels, agent connection mappings, and routing metadata.

**Key Classes**:
- `TunnelCache`: Thread-safe map with shared_mutex
- `Tunnel`: Entity representing tunnel metadata
- `TunnelAgent`: Entity representing agent connection state

**Dependencies**:
- None (pure in-memory, no external storage for core routing)

**Performance**:
- O(1) lookup by tunnel ID or subdomain
- Shared locks for reads, exclusive locks for writes
- TTL-based eviction (optional)

### 5. Token Validator (`token_validator.cpp`)

**Responsibility**: Authenticate tunnel agents by validating Bearer tokens against Key Vault, compute SHA-256 hashes, prevent replay attacks.

**Key Classes**:
- `TokenValidator`: Main validation engine
- `AuthToken`: Entity with SHA-256 hash

**Dependencies**:
- Azure Key Vault SDK (token storage)
- OpenSSL (SHA-256 hashing)

**Performance**:
- Cached token hashes (no Key Vault hit on every request)
- Constant-time comparison (prevent timing attacks)
- Rate limiting (prevent brute force)

### 6. Observability (`logger.cpp`, `azure_monitor_metrics.cpp`)

**Responsibility**: Structured JSON logging to stdout (captured by Azure Container Apps), metrics export to Azure Monitor, audit event tracking.

**Key Classes**:
- `Logger`: JSON-formatted log output
- `AzureMonitorMetrics`: Metrics exporter
- `AuditEvent`: Security event entity

**Dependencies**:
- Azure Log Analytics SDK (HTTP Data Collector API)
- nlohmann/json (JSON serialization)

**Performance**:
- Async logging (non-blocking)
- Batched metrics export (every 60 seconds)
- Configurable log levels (DEBUG, INFO, WARN, ERROR)

---

## Data Flow

### HTTP Tunneling Flow

1. **Client Request**:
   ```
   Client → HTTPS (443) → HTTP Server
   ```

2. **TLS Termination**:
   ```
   HTTP Server → OpenSSL → Plaintext HTTP
   ```

3. **Routing**:
   ```
   HTTP Server → RequestRouter → Tunnel Registry
   → Lookup by Host header (subdomain.tunnel.com)
   ```

4. **Agent Selection**:
   ```
   Tunnel Registry → Agent Registry → Pick healthy agent
   ```

5. **Request Forwarding**:
   ```
   HTTP Server → Protocol Multiplexer → Agent Connection
   → Forward HTTP request
   ```

6. **Agent Processing**:
   ```
   Agent Connection → Local Service (printer API)
   → Receive HTTP response
   ```

7. **Response Proxying**:
   ```
   Agent Connection → Protocol Multiplexer → HTTP Server
   → Send HTTP response to client
   ```

### TCP Tunneling Flow

1. **Client Connection**:
   ```
   Client → TCP (custom port) → HTTP Server (TCP listener)
   ```

2. **Routing**:
   ```
   TCP Server → Tunnel Registry → Lookup by port mapping
   ```

3. **Zero-Copy Forwarding**:
   ```
   TCP Server → splice() → Agent Connection → Local Service
   ```

4. **Bidirectional Streaming**:
   ```
   Client ↔ TCP Server ↔ Agent Connection ↔ Local Service
   (full-duplex, zero-copy)
   ```

### Agent Authentication Flow

1. **Agent Connects**:
   ```
   Agent → TLS (8443) → Agent Server
   ```

2. **Token Extraction**:
   ```
   Agent Server → Read Authorization header
   → Extract Bearer token
   ```

3. **Token Validation**:
   ```
   Token Validator → Compute SHA-256(token)
   → Compare with stored hash from Key Vault
   ```

4. **Registration**:
   ```
   If valid: Agent Registry → Add to connection pool
   If invalid: Agent Server → Reject with 401 Unauthorized
   ```

---

## Deployment Architecture

### Azure Container Apps

```
┌─────────────────────────────────────────────────────────────────┐
│                     Azure Container Apps                        │
│                                                                 │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐   │
│  │ Replica 1 │  │ Replica 2 │  │ Replica 3 │  │ ...       │   │
│  │ (0.5 CPU) │  │ (0.5 CPU) │  │ (0.5 CPU) │  │           │   │
│  │ (1Gi RAM) │  │ (1Gi RAM) │  │ (1Gi RAM) │  │           │   │
│  └───────────┘  └───────────┘  └───────────┘  └───────────┘   │
│                                                                 │
│  Auto-scaling: 1-10 replicas based on CPU 70%                  │
└─────────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Azure Container Apps Environment               │
│  - Log Analytics integration                                    │
│  - Managed identity (Key Vault access)                          │
│  - Ingress: HTTPS (443), Agent (8443)                           │
└─────────────────────────────────────────────────────────────────┘
```

### Resource Topology

```
Resource Group: protogate-prod-rg
├── Container App: protogate-prod-app
├── Key Vault: protogateprodkv
│   ├── Secret: tunnel-token-default
│   ├── Secret: tunnel-token-printer1
│   └── Certificate: wildcard-tls-cert
├── DNS Zone: tunnel.example.com
│   └── A Record: *.tunnel.example.com → Container App IP
└── Log Analytics: protogate-prod-logs
    ├── Custom Logs: TunnelLogs_CL
    ├── Metrics: TunnelMetrics
    └── Alerts: HighErrorRate, HighLatency
```

### Cost Estimation (Production)

| Resource | SKU | Monthly Cost |
|----------|-----|--------------|
| Container Apps | 0.5 vCPU, 1Gi RAM, 1-10 replicas | $5-20 |
| Key Vault | Standard, <10k operations/month | $0.50 |
| DNS Zone | 1 zone, <1M queries/month | $0.50 |
| Log Analytics | 1GB/day cap, 30-day retention | $3-5 |
| **Total** | | **$9-26/month** |

*Light usage (1-5 tunnels, <10k requests/day): $9-12/month*

---

## Integration Points

### Azure Key Vault

**Purpose**: Secure storage for tunnel tokens and TLS certificates

**Integration**:
- SDK: Azure SDK for C++ (`azure-security-keyvault-secrets`)
- Authentication: Managed Identity (no credentials in code)
- Operations:
  - `GetSecret`: Retrieve token for validation
  - `ListSecrets`: Enumerate all tunnel tokens
  - `SetSecret`: Store new tunnel token (management API)

**Performance**:
- Token hash caching (1-hour TTL, no Key Vault hit per request)
- Async secret retrieval

### Azure DNS Zone

**Purpose**: Wildcard DNS routing for tunnel subdomains

**Configuration**:
- Zone: `tunnel.example.com`
- A Record: `*.tunnel.example.com` → Container App IP

**Management**:
- Bicep template creates zone and wildcard record
- No runtime integration (static DNS configuration)

### Azure Log Analytics

**Purpose**: Structured logging and metrics for observability

**Integration**:
- SDK: Azure Log Analytics HTTP Data Collector API
- Authentication: Workspace ID + Shared Key (from Bicep output)
- Operations:
  - `PostLogData`: Send JSON log batches (custom log table)
  - Metrics: HTTP latency, tunnel count, error rate

**Performance**:
- Async batching (logs every 10 seconds or 1000 entries)
- Backpressure handling (drop logs if buffer full)

---

## Security Architecture

### Threat Model Summary

(See [docs/security.md](security.md) for full threat model)

**Trust Boundaries**:
1. Internet → HTTP Server (public, untrusted)
2. HTTP Server → Agent Server (authenticated, TLS)
3. Agent Server → Local Services (trusted, customer network)

**Key Mitigations**:
- **DDoS**: Rate limiting, connection limits (50 agents, 10k HTTP conns)
- **Token Theft**: SHA-256 hashing, no plaintext token storage/logging
- **Man-in-the-Middle**: TLS 1.2+ mandatory, certificate validation
- **Replay Attacks**: Token rotation, timestamp validation
- **IP Spoofing**: CIDR-based allowlists (optional)

---

## Performance Characteristics

### Benchmarks (Target vs Actual)

| Metric | Target | Actual | Status |
|--------|--------|--------|--------|
| HTTP Latency (p95) | <10ms | TBD | 🟡 Pending |
| TCP Throughput | >100 Mbps | TBD | 🟡 Pending |
| Memory per 50 Tunnels | <512MB | TBD | 🟡 Pending |
| Concurrent HTTP Conns | 10,000 | TBD | 🟡 Pending |
| Tunnel Establishment | <500ms | TBD | 🟡 Pending |

*(Benchmarks to be run as part of T109)*

### Scalability

**Vertical Scaling**:
- 1 vCPU instance: 50 tunnels, 100k req/min
- 2 vCPU instance: 100 tunnels, 200k req/min

**Horizontal Scaling**:
- Azure Container Apps auto-scales 1-10 replicas
- DNS round-robin for inbound traffic
- Agent connections distributed across replicas

**Bottlenecks**:
- Key Vault throttling (2000 req/10s per vault)
- Log Analytics ingestion (6 MB/min per workspace)

---

## Future Enhancements

1. **Multi-Region Deployment**: Active-active across Azure regions
2. **TCP Load Balancing**: Distribute TCP connections across multiple agents
3. **Metrics Dashboard**: Grafana integration for real-time monitoring
4. **Agent Affinity**: Sticky routing for stateful TCP connections
5. **Custom Domain Support**: Per-tunnel custom domains (CNAME records)

---

**Document Version**: 1.0  
**Next Review**: After T108 (integration testing)
