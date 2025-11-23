# Agent Protocol Specification

**Version**: 1.0.0  
**Created**: 2025-11-22  
**Purpose**: Define the wire protocol between tunnel agents and the tunnel server.

---

## Overview

The tunnel agent establishes an outbound TLS connection to the server and authenticates using a bearer token. The connection is persistent and bi-directional: the server sends incoming client requests to the agent, and the agent returns responses. For HTTP tunnels, HTTP/2 is used for multiplexing. For TCP tunnels, a custom binary framing protocol is used.

---

## Connection Establishment

### 1. TLS Handshake

**Transport**: TCP over TLS 1.2 or 1.3  
**Port**: 443 (HTTPS)  
**SNI**: `tunnel-agent.tunnel.mycorp.com` (or similar, distinct from client-facing endpoints)

**Client Certificate**: Optional (for mTLS, not MVP)

### 2. Authentication

**Method**: HTTP/2 with Authorization header (for HTTP tunnels) or custom handshake frame (for TCP tunnels)

#### HTTP Tunnel Authentication

Agent sends HTTP/2 CONNECT request:

```
CONNECT tunnel-agent HTTP/2
Host: tunnel-agent.tunnel.mycorp.com
Authorization: Bearer tnl_<token>
X-Tunnel-ID: api
X-Agent-Version: 1.0.0
```

Server responds:

```
HTTP/2 200 Connection Established
X-Server-Version: 1.0.0
X-Tunnel-Status: ACTIVE
```

Or on failure:

```
HTTP/2 401 Unauthorized
X-Error-Code: INVALID_TOKEN
X-Error-Message: Token validation failed
```

#### TCP Tunnel Authentication

Agent sends handshake frame (binary):

```
+-------------------+
| Frame Type (1B)   | = 0x01 (HANDSHAKE)
+-------------------+
| Version (2B)      | = 0x0100 (v1.0)
+-------------------+
| Tunnel ID Len (1B)| = N
+-------------------+
| Tunnel ID (NB)    | = "printer1"
+-------------------+
| Token Len (1B)    | = M
+-------------------+
| Token (MB)        | = "tnl_..."
+-------------------+
```

Server responds:

```
+-------------------+
| Frame Type (1B)   | = 0x02 (HANDSHAKE_ACK)
+-------------------+
| Status Code (2B)  | = 0x0000 (SUCCESS) or error code
+-------------------+
| Message Len (2B)  | = K
+-------------------+
| Message (KB)      | = "Connection established"
+-------------------+
```

**Error Codes**:
- `0x0000`: SUCCESS
- `0x0001`: INVALID_TOKEN
- `0x0002`: TUNNEL_NOT_FOUND
- `0x0003`: TUNNEL_ALREADY_CONNECTED
- `0x0004`: INTERNAL_ERROR

---

## Heartbeat Protocol

**Interval**: Agent sends heartbeat every 30 seconds  
**Timeout**: Server expects heartbeat within 60 seconds

### HTTP/2 Heartbeat

Agent sends PING frame (built-in HTTP/2 mechanism):

```
PING (opaque data: 8 bytes, incremental counter)
```

Server responds with PING ACK.

### TCP Heartbeat

Agent sends:

```
+-------------------+
| Frame Type (1B)   | = 0x03 (HEARTBEAT)
+-------------------+
| Timestamp (8B)    | = Unix timestamp (ms)
+-------------------+
```

Server responds:

```
+-------------------+
| Frame Type (1B)   | = 0x04 (HEARTBEAT_ACK)
+-------------------+
| Server Time (8B)  | = Unix timestamp (ms)
+-------------------+
```

---

## Request/Response Protocol

### HTTP Tunnels (HTTP/2)

When a client request arrives, server creates an HTTP/2 stream to the agent:

**Server → Agent (HTTP/2 HEADERS + DATA)**:

```
:method: GET
:path: /users
:scheme: https
:authority: api.tunnel.mycorp.com
x-tunnel-request-id: 550e8400-e29b-41d4-a716-446655440000
x-client-ip: 203.0.113.45
<original headers>

<request body, if any>
```

**Agent → Server (HTTP/2 HEADERS + DATA)**:

```
:status: 200
content-type: application/json
<response headers>

<response body>
```

**Stream closure**: END_STREAM flag on final DATA frame.

**Error handling**: RST_STREAM with error code if agent fails to process.

### TCP Tunnels (Binary Protocol)

**Server → Agent (TCP DATA Frame)**:

```
+-------------------+
| Frame Type (1B)   | = 0x10 (TCP_DATA)
+-------------------+
| Connection ID (16B)| = UUID of TCP connection
+-------------------+
| Sequence Num (4B) | = Incremental per connection
+-------------------+
| Data Length (4B)  | = N bytes
+-------------------+
| Data (NB)         | = Raw TCP payload
+-------------------+
```

**Agent → Server (TCP DATA Frame)**:

Same format, agent sends response data.

**Connection close**:

```
+-------------------+
| Frame Type (1B)   | = 0x11 (TCP_CLOSE)
+-------------------+
| Connection ID (16B)| = UUID
+-------------------+
| Reason Code (2B)  | = 0 (normal), 1 (timeout), 2 (error)
+-------------------+
```

**Flow control**: Simple window-based (send max 64KB before waiting for ACK).

---

## Error Handling

### Agent Errors

If agent cannot process request (local service down, timeout, etc.), it responds with:

**HTTP/2**:
```
:status: 502
x-tunnel-error: LOCAL_SERVICE_UNAVAILABLE
x-error-message: Connection to localhost:5000 refused
```

**TCP**:
```
+-------------------+
| Frame Type (1B)   | = 0x12 (TCP_ERROR)
+-------------------+
| Connection ID (16B)|
+-------------------+
| Error Code (2B)   | = 0x0001 (CONNECTION_REFUSED)
+-------------------+
| Message Len (2B)  |
+-------------------+
| Message (NB)      | = "localhost:9100 refused"
+-------------------+
```

### Server Errors

If server encounters error (timeout, agent disconnect), it closes stream/connection:

**HTTP/2**: RST_STREAM with INTERNAL_ERROR  
**TCP**: TCP_CLOSE frame with error reason

---

## Protocol Versioning

**Current Version**: 1.0  
**Negotiation**: Agent sends version in handshake, server responds with supported version.

If version mismatch, server responds with error and closes connection.

**Future versions** can add:
- Compression (gzip, brotli)
- Multiplexing for multiple local targets per agent
- Priority/QoS hints

---

## Security Considerations

1. **TLS mandatory**: No plaintext communication
2. **Token in header/handshake**: Never in query params or URLs
3. **Token validation**: Server validates against Key Vault on first use, then caches with TTL
4. **No token logging**: Only hash logged for audit
5. **Rate limiting**: Per-tunnel, enforced before authentication to prevent DoS
6. **Connection limits**: Max 1 active agent per tunnel (future: allow multiple for HA)

---

## Example Session (HTTP Tunnel)

```
[Agent → Server]
CONNECT tunnel-agent HTTP/2
Authorization: Bearer tnl_abc123
X-Tunnel-ID: api

[Server → Agent]
HTTP/2 200 Connection Established

[Heartbeat every 30s]
Agent → Server: PING
Server → Agent: PING ACK

[Client request arrives]
[Server → Agent]
:method: POST
:path: /users
x-tunnel-request-id: 550e8400...
x-client-ip: 203.0.113.45
content-type: application/json
{"name":"Alice"}

[Agent processes, responds]
[Agent → Server]
:status: 201
content-type: application/json
{"id":1,"name":"Alice"}

[Stream closes]
```

---

## Implementation Notes

- **HTTP/2 library**: nghttp2 on server, any HTTP/2 client library on agent
- **Binary protocol**: Custom implementation, 100-200 lines of C++ for framing
- **Backpressure**: Use TCP flow control + HTTP/2 flow control (window updates)
- **Reconnection**: Agent retries with exponential backoff (1s, 2s, 4s, ... max 60s)
- **Graceful shutdown**: Server sends GOAWAY frame (HTTP/2) or DISCONNECT frame (TCP), waits for in-flight requests to complete

---

## Future Enhancements

1. **WebSocket protocol**: Alternative to HTTP/2 for broader agent compatibility
2. **UDP tunneling**: Add UDP_DATA frame type
3. **Multi-target agents**: Single agent handles multiple local services
4. **Load balancing**: Multiple agents per tunnel, server round-robins requests
5. **Request mirroring**: Send request to multiple agents for A/B testing

---

**End of Agent Protocol Specification**
