# Feature Specification: Protogate Core Server

**Feature Branch**: `001-tunnel-core-server`  
**Created**: 2025-11-21  
**Status**: Draft  
**Input**: User description: "Protogate Core Server - HTTP/HTTPS and TCP tunneling infrastructure"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - HTTP Tunnel Establishment and Traffic Routing (Priority: P1)

An operations engineer deploys the tunnel server in Azure Container Apps and starts a tunnel agent on their local machine. The agent authenticates with a secure token and establishes an outbound TLS connection. When an HTTP request arrives at `https://myapp.tunnel.mycorp.com`, the server routes it through the tunnel to the local application and returns the response to the caller.

**Why this priority**: This is the foundational capability - without HTTP tunneling, no other features matter. It delivers immediate value by allowing secure access to local services.

**Independent Test**: Deploy tunnel server, start one agent with token, send HTTP GET request to tunnel endpoint, verify response is proxied correctly from local service.

**Acceptance Scenarios**:

1. **Given** tunnel server is running and agent is connected with valid token, **When** HTTP GET request sent to `https://api.tunnel.mycorp.com/users`, **Then** request is routed to local `localhost:5000/users` and response returned with correct status code and body
2. **Given** tunnel agent disconnects, **When** HTTP request sent to tunnel endpoint, **Then** server returns 503 Service Unavailable with appropriate error message
3. **Given** invalid authentication token provided by agent, **When** agent attempts to connect, **Then** server rejects connection with 401 Unauthorized and logs authentication failure
4. **Given** tunnel is active, **When** multiple concurrent HTTP requests sent (50 simultaneous), **Then** all requests are handled correctly with <10ms added latency per request

---

### User Story 2 - TCP Tunnel for Printer Traffic (Priority: P1)

A retail store runs a thermal printer on port 9100. The store's tunnel agent connects to the cloud tunnel server. The Printer4All cloud service sends raw print data to `printer1.tunnel.mycorp.com:9100`. The tunnel server forwards the TCP stream to the agent, which delivers it to the local printer, and the receipt prints successfully.

**Why this priority**: TCP tunneling is essential for Printer4All use case (project's primary driver). Without this, remote printing doesn't work.

**Independent Test**: Deploy tunnel server, configure TCP tunnel on port 9100, send raw TCP data (simulated print job), verify data reaches local TCP socket intact.

**Acceptance Scenarios**:

1. **Given** TCP tunnel configured for port 9100 and agent connected, **When** cloud service sends print job data to `tunnel.mycorp.com:9100`, **Then** data is forwarded byte-for-byte to `localhost:9100` and printer receives complete job
2. **Given** print job is 5MB (large label sheet), **When** sent through TCP tunnel, **Then** transfer completes at minimum 100 Mbps throughput with no data corruption
3. **Given** TCP connection is established, **When** network interruption occurs for 10 seconds then recovers, **Then** tunnel agent reconnects within 5 seconds and queued data is transmitted
4. **Given** printer is offline (local port unreachable), **When** print job sent through tunnel, **Then** server receives connection refused error and returns appropriate failure to cloud service

---

### User Story 3 - Tunnel Registration and Token Management (Priority: P2)

An administrator uses the management API to create a new tunnel. They specify the tunnel ID (`myapp`), protocol (HTTP), and target (`localhost:5000`). The system generates a cryptographically secure token, stores it in Azure Key Vault, and returns it to the administrator. The agent uses this token to authenticate and establish the tunnel.

**Why this priority**: Required for operational management but can initially be manual (config files) before API is built. User Stories 1 and 2 could work with hard-coded tokens for MVP.

**Independent Test**: Call management API to create tunnel, verify token is generated and stored in Key Vault, use token to connect agent, verify tunnel works.

**Acceptance Scenarios**:

1. **Given** administrator has valid Entra ID credentials, **When** POST request to `/api/v1/tunnels` with tunnel ID and configuration, **Then** tunnel is created, token generated with 256-bit entropy, stored in Key Vault, and returned in response
2. **Given** tunnel exists, **When** administrator calls `/api/v1/tunnels/{id}/rotate-token`, **Then** new token generated, old token remains valid for 5-minute grace period, then invalidated
3. **Given** tunnel agent attempts connection, **When** invalid token provided, **Then** connection refused within 100ms and audit log records authentication failure with source IP and timestamp
4. **Given** multiple tunnels registered (50+), **When** agent connects with specific token, **Then** server correctly identifies tunnel ID and routes traffic to appropriate agent with <50ms lookup time

---

### User Story 4 - TLS Termination with Custom Domain (Priority: P2)

An enterprise wants tunnel endpoints at `*.tunnel.mycorp.com`. They configure Azure DNS Zone delegation and upload a wildcard TLS certificate to Key Vault. The tunnel server loads the certificate and serves HTTPS traffic. When users access `https://app1.tunnel.mycorp.com`, the browser shows a valid certificate and secure connection.

**Why this priority**: Critical for production security but can initially use self-signed certs or Let's Encrypt for MVP testing. Custom domain is expected feature per constitution.

**Independent Test**: Configure DNS zone, upload certificate to Key Vault, start tunnel server, verify HTTPS endpoint serves with valid certificate.

**Acceptance Scenarios**:

1. **Given** wildcard certificate for `*.tunnel.mycorp.com` stored in Key Vault, **When** tunnel server starts, **Then** certificate is loaded and HTTPS endpoints serve with TLS 1.2+ encryption
2. **Given** DNS CNAME points `api.tunnel.mycorp.com` to tunnel server, **When** HTTPS request sent to `https://api.tunnel.mycorp.com`, **Then** valid certificate presented and traffic encrypted end-to-end
3. **Given** certificate expires in 30 days, **When** new certificate uploaded to Key Vault, **Then** server reloads certificate without downtime (hot reload)
4. **Given** client uses TLS 1.0 (insecure), **When** connection attempted, **Then** server rejects connection and requires TLS 1.2 minimum

---

### User Story 5 - IP Allowlisting and Security Controls (Priority: P3)

An administrator configures IP allowlist for a sensitive tunnel: only requests from specific cloud service IP ranges (`10.0.0.0/24`, `52.168.100.0/22`) can access `secure-api.tunnel.mycorp.com`. When a request arrives from an allowed IP, it's forwarded. Requests from other IPs receive 403 Forbidden.

**Why this priority**: Important security feature per constitution, but authentication tokens provide baseline security. IP filtering adds defense-in-depth.

**Independent Test**: Configure IP allowlist for tunnel, send request from allowed IP (succeeds), send from blocked IP (denied).

**Acceptance Scenarios**:

1. **Given** tunnel has IP allowlist configured `["10.0.0.0/24"]`, **When** request from `10.0.0.50` arrives, **Then** request allowed and forwarded to agent
2. **Given** same IP allowlist, **When** request from `192.168.1.1` arrives, **Then** request denied with 403 Forbidden and audit log records blocked IP
3. **Given** no IP allowlist configured (default), **When** request from any IP arrives with valid tunnel, **Then** request allowed (permissive default for ease of setup)
4. **Given** IP allowlist includes Azure Front Door IP ranges, **When** malicious request bypasses Front Door, **Then** blocked at tunnel server level (defense in depth)

---

### User Story 6 - Health Monitoring and Observability (Priority: P3)

Operations team deploys tunnel server and enables Azure Monitor integration. The server emits structured JSON logs to Log Analytics: tunnel connections, request latency, byte counts, authentication events. Pre-built dashboards show active tunnels (23), aggregate throughput (45 Mbps), p95 latency (8ms), error rate (0.1%). Alerts trigger when error rate exceeds 5%.

**Why this priority**: Required per constitution for production operations, but basic functionality (Stories 1-2) can work without dashboards. Logs can initially go to stdout.

**Independent Test**: Enable Azure Monitor integration, generate tunnel traffic, verify logs appear in Log Analytics, verify metrics exported.

**Acceptance Scenarios**:

1. **Given** tunnel server configured with Log Analytics workspace ID, **When** tunnel agent connects, **Then** structured JSON log emitted with timestamp, tunnel ID, source IP, auth status
2. **Given** HTTP requests flowing through tunnel, **When** requests complete, **Then** metrics exported: request count, latency histogram (p50, p95, p99), byte counts (sent/received)
3. **Given** authentication failure occurs, **When** invalid token presented, **Then** security event logged with tunnel ID, source IP, failure reason, timestamp
4. **Given** error rate exceeds 5% over 5-minute window, **When** threshold crossed, **Then** Azure Monitor alert fires and notifies operations team via configured action group

---

### Edge Cases

- **What happens when tunnel agent loses connection mid-request?** Server detects broken connection via TCP keepalive or read timeout, returns 502 Bad Gateway to client, logs connection failure. Agent reconnects automatically and subsequent requests succeed.

- **How does system handle very large payloads (1GB+ file uploads)?** Server streams data through tunnel without buffering entire payload in memory. Request times out if transfer exceeds 30 minutes (configurable). Memory usage remains bounded (<512MB per instance).

- **What if two agents try to connect with same tunnel ID?** Server rejects second connection with error "Tunnel ID already in use", logs conflict, first agent remains connected. Administrator must resolve by using unique tunnel IDs or rotating tokens.

- **How does server handle agent connecting from multiple IPs (mobile agent)?** Agent presents same token, server accepts connection from new IP, terminates old connection gracefully. Routing updates within 1 second to point to new agent connection.

- **What if Azure Key Vault is temporarily unavailable?** Server caches decrypted tokens in memory for 1 hour. New tunnel creation fails with 503 Service Unavailable. Existing tunnels continue working with cached tokens. Audit log records Key Vault connectivity issues.

- **How does rate limiting prevent abuse?** Server enforces per-tunnel rate limit (default 1000 req/min). Requests exceeding limit receive 429 Too Many Requests. Rate limiting applied before authentication to prevent DoS. Limits configurable per tunnel.

## Requirements *(mandatory)*

### Functional Requirements

**Protocol Support:**
- **FR-001**: System MUST accept inbound HTTPS connections on port 443 with TLS 1.2+ encryption
- **FR-002**: System MUST accept inbound TCP connections on configurable ports (e.g., 9100, 515) for raw TCP tunneling
- **FR-003**: System MUST support HTTP/1.1 and HTTP/2 for HTTP tunnels
- **FR-004**: System MUST support WebSocket protocol upgrade for real-time applications
- **FR-005**: System MUST forward TCP streams byte-for-byte without protocol inspection or modification

**Tunnel Agent Connection:**
- **FR-006**: System MUST accept outbound-initiated TLS connections from tunnel agents on port 443
- **FR-007**: System MUST authenticate tunnel agents using secure tokens (256-bit minimum entropy)
- **FR-008**: System MUST maintain persistent connections with tunnel agents using HTTP/2 multiplexing or custom binary protocol
- **FR-009**: System MUST detect agent disconnection within 60 seconds via heartbeat mechanism
- **FR-010**: System MUST support graceful agent reconnection with automatic state recovery

**Routing and Traffic Management:**
- **FR-011**: System MUST route incoming requests to correct tunnel agent based on hostname (HTTP) or port (TCP)
- **FR-012**: System MUST maintain routing table mapping tunnel IDs to active agent connections
- **FR-013**: System MUST support concurrent requests on single tunnel (minimum 50 simultaneous requests)
- **FR-014**: System MUST stream request/response data without buffering entire payload in memory
- **FR-015**: System MUST enforce request timeout (default 30 minutes, configurable per tunnel)

**Security and Authentication:**
- **FR-016**: System MUST validate tunnel agent tokens against secrets stored in Azure Key Vault
- **FR-017**: System MUST support token rotation with configurable grace period (default 5 minutes)
- **FR-018**: System MUST enforce IP allowlists when configured for a tunnel (CIDR notation support)
- **FR-019**: System MUST reject TLS connections using protocols older than TLS 1.2
- **FR-020**: System MUST NOT log sensitive data (tokens, payload content, credentials)

**TLS and Certificate Management:**
- **FR-021**: System MUST load TLS certificates from Azure Key Vault on startup
- **FR-022**: System MUST support wildcard certificates (e.g., `*.tunnel.mycorp.com`)
- **FR-023**: System MUST support hot reload of certificates without downtime
- **FR-024**: System MUST present correct certificate based on SNI (Server Name Indication)

**Observability and Logging:**
- **FR-025**: System MUST emit structured JSON logs to Azure Log Analytics
- **FR-026**: System MUST log minimum data: timestamp, tunnel ID, source IP, destination, byte counts, duration, auth status
- **FR-027**: System MUST export metrics to Azure Monitor: active tunnels, throughput, error rates, connection latency
- **FR-028**: System MUST provide health check endpoint (`/health`) returning 200 OK when healthy, 503 when degraded
- **FR-029**: System MUST support distributed tracing via OpenTelemetry headers (optional but recommended)

**Scalability and Performance:**
- **FR-030**: System MUST support horizontal scaling (multiple server instances behind load balancer)
- **FR-031**: System MUST handle minimum 50 concurrent tunnel agent connections per instance
- **FR-032**: System MUST process minimum 100,000 HTTP requests per second per instance (1 vCPU)
- **FR-033**: System MUST add <10ms p95 latency overhead for HTTP tunnels
- **FR-034**: System MUST achieve minimum 100 Mbps throughput for TCP tunnels
- **FR-035**: System MUST use <512MB memory for 50 concurrent tunnels

**Error Handling:**
- **FR-036**: System MUST return 503 Service Unavailable when tunnel agent is disconnected
- **FR-037**: System MUST return 502 Bad Gateway when agent connection fails mid-request
- **FR-038**: System MUST return 401 Unauthorized when invalid token presented
- **FR-039**: System MUST return 403 Forbidden when request from non-allowlisted IP
- **FR-040**: System MUST return 429 Too Many Requests when rate limit exceeded
- **FR-041**: System MUST gracefully reject new connections when at capacity (vs. crashing)

**Configuration:**
- **FR-042**: System MUST read configuration from environment variables (12-factor app pattern)
- **FR-043**: System MUST support configuration of: Key Vault URI, DNS zone, Log Analytics workspace ID, default timeouts, rate limits
- **FR-044**: System MUST validate configuration on startup and fail fast with clear error messages

### Key Entities

- **Tunnel**: Represents a logical tunnel configuration. Attributes: tunnel ID (unique identifier), protocol (HTTP or TCP), target (local host:port on agent side), IP allowlist (optional CIDR ranges), rate limit (requests per minute), created timestamp, status (active/inactive).

- **TunnelAgent**: Represents a connected agent. Attributes: tunnel ID (references Tunnel), connection ID (unique per connection), source IP address, authentication token hash, connection timestamp, last heartbeat timestamp, connection status (connected/disconnected), agent version.

- **TunnelRequest**: Represents an in-flight request through a tunnel. Attributes: request ID, tunnel ID, source IP, destination, protocol, byte count (sent/received), start timestamp, end timestamp, status code (HTTP) or connection status (TCP), latency.

- **AuthToken**: Represents authentication credentials for a tunnel. Attributes: token hash (SHA-256), tunnel ID (references Tunnel), created timestamp, expires timestamp (optional), rotation status (active/grace-period/revoked).

- **AuditEvent**: Represents security and operational events. Attributes: event type (auth_success, auth_failure, tunnel_connected, tunnel_disconnected, rate_limit_exceeded), tunnel ID, source IP, timestamp, additional context (error messages, user agent).

## Success Criteria *(mandatory)*

### Measurable Outcomes

**Performance:**
- **SC-001**: Tunnel establishment completes in under 500ms p95 (from agent connect to first byte forwarded)
- **SC-002**: HTTP tunnel adds less than 10ms p95 latency overhead compared to direct connection
- **SC-003**: TCP tunnel achieves minimum 100 Mbps throughput for sustained data transfer
- **SC-004**: System processes 100,000 HTTP requests per second on single vCPU instance

**Reliability:**
- **SC-005**: Tunnel agent reconnects automatically within 5 seconds after network interruption
- **SC-006**: System handles 50 concurrent tunnels on single instance with <512MB memory usage
- **SC-007**: Zero data loss during agent reconnection (requests either complete or fail cleanly)
- **SC-008**: System operates for 30 days continuous runtime without memory leaks or degradation

**Security:**
- **SC-009**: 100% of tunnel connections use TLS 1.2+ encryption (no plaintext allowed)
- **SC-010**: Authentication failures logged with 100% accuracy (no missing audit events)
- **SC-011**: Invalid tokens rejected within 100ms with no further processing
- **SC-012**: IP allowlist enforcement blocks 100% of non-allowlisted requests when configured

**Operational:**
- **SC-013**: Deployment to Azure Container Apps completes in under 10 minutes from template
- **SC-014**: All tunnel events appear in Log Analytics within 30 seconds
- **SC-015**: Health check endpoint responds within 100ms with accurate status
- **SC-016**: Certificate rotation completes without dropping active connections (zero-downtime)

**Cost:**
- **SC-017**: Light usage deployment (10 tunnels, 10GB traffic) costs under $8/month on Azure
- **SC-018**: System utilizes Azure Container Apps free tier for initial 2 million requests

### Assumptions

- Azure Container Apps is the primary deployment target (AKS and VMs are alternative options but not prioritized for MVP)
- Customers own their Azure DNS Zone and can configure DNS delegation
- Wildcard TLS certificates are obtained externally (Let's Encrypt or commercial CA) and uploaded to Key Vault
- Management API for tunnel creation will be built in a separate feature (MVP can use configuration files or Azure CLI)
- BugBashing integration (Entra ID, Teams) will be added in a separate feature after core tunneling is stable
- Rate limiting is per-tunnel, not per-source-IP (source IP rate limiting can be added via Azure Front Door)
- Maximum payload size is 10GB (larger files require chunking or alternative transfer method)
