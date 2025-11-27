# Protogate Quickstart Guide

Your server is deployed and running in Azure! Here's how to use it.

## Current Deployment Status

✅ **Production URL**: https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io

✅ **Health Endpoint**: Working (HTTP 200)

✅ **Infrastructure**: Azure Container Apps (East US)

✅ **Deployment**: Single-command automated via `./build-and-push-local.sh`

## What You Have

Protogate is a **reverse tunnel server** deployed in Azure with a fully implemented C++ agent. Current status:

1. **Server Infrastructure** ✅ (Running in Azure Container Apps, East US)
2. **Health Monitoring** ✅ (Health checks, metrics, error tracking)
3. **TLS Configuration** ✅ (Azure handles TLS termination)
4. **Auto-scaling** ✅ (1-3 replicas based on CPU usage)
5. **Tunnel Agent** ✅ (Complete C++ implementation in `tunnel-agent/`)
6. **Agent Protocol** ✅ (TLS + HTTP/2 + ALPN support)
7. **HTTP Request Forwarding** ✅ (Agent forwards to local services)

## What Still Needs Integration

To enable end-to-end tunneling:

1. **Management API** ⚠️ (Server needs POST/GET/DELETE `/v1/tunnels` endpoints)
2. **HTTP Proxy** ⚠️ (Server needs to route by Host header to agents)
3. **TCP Proxy** ⚠️ (Server needs TCP stream forwarding)
4. **Agent Deployment** ❌ (Build agent Docker image, deploy alongside server)

Think of the full architecture:
```
Internet Client → Azure Ingress (HTTPS) → Protogate Server → Tunnel Agent → Local Service
                      ✅                         ⚠️               ✅              (your app)
                                            (needs API)      (implemented)
```

## Testing Current Deployment

### 1. Health Check (Working)
```bash
curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health
```

**Expected Response**:
```json
{
  "status": "healthy",
  "version": "1.0.0",
  "service": "protogate",
  "timestamp": 1764243784596,
  "metrics": {
    "active_tunnels": 0,
    "total_http_requests": 0,
    "active_http_connections": 0,
    "active_tcp_connections": 0
  }
}
```

### 2. Test Tunnel Agent Locally

The tunnel agent is fully implemented and can be tested locally:

```bash
# Build the agent (if not already built)
cd tunnel-agent
cmake -B build -S . -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build --parallel 8

# Create configuration
cp config.example.json config.json

# Edit config.json with your settings:
# {
#   "server_host": "protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io",
#   "server_port": 8443,
#   "tunnel_id": "my-test-tunnel",
#   "tunnel_token": "tnl_your_token_here",
#   "local_url": "http://localhost:3000"
# }

# Run the agent
./build/tunnel-agent --config config.json
```

**What the agent does**:
- Connects to server via TLS on port 8443
- Authenticates with Bearer token
- Maintains HTTP/2 session with heartbeats
- Forwards incoming HTTP requests to local service
- Returns responses back through tunnel
- Auto-reconnects with exponential backoff

**Current limitation**: Server needs Management API to register tunnels before agents can connect productively.

### 2. Management API (Not Yet Implemented)

These endpoints will be available once implementation is complete:

```bash
# Create a Tunnel (Future)
curl -X POST https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "my-test-app",
    "protocol": "HTTP",
    "target": "localhost:3000",
    "dns_subdomain": "my-test-app"
  }'

# List All Tunnels (Future)
curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels

# Get Specific Tunnel (Future)
curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels/my-test-app

# Delete Tunnel (Future)
curl -X DELETE https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/v1/tunnels/my-test-app
```

## Deployment

### Deploy to Azure (Single Command)

```bash
./build-and-push-local.sh
```

This script:
1. Logs into Azure Container Registry
2. Builds Docker image for AMD64 architecture
3. Pushes to ACR with unique tag (commit SHA + timestamp)
4. Deploys to Azure Container Apps
5. Waits for health checks to pass
6. Verifies endpoint is working

**Typical deployment time**: ~2 minutes

### Manual Deployment Steps

If you need to deploy manually:

```bash
# 1. Login to ACR
az acr login --name protogatedevacr

# 2. Build and push image
docker buildx build \
  --platform linux/amd64 \
  --build-arg CACHEBUST=$(date +%s) \
  --no-cache \
  -f docker/Dockerfile.alpine \
  -t protogatedevacr.azurecr.io/protogate:v1.0.0 \
  --push \
  .

# 3. Update Container App
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --image protogatedevacr.azurecr.io/protogate:v1.0.0

# 4. Check health
az containerapp revision list \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "[0].{name:name, health:properties.healthState}"
```

## Implementation Status

### ✅ Completed (Phase 1 - Infrastructure + Agent)

- **Server Framework**: C++ server with Boost.Asio
- **Docker Build**: Multi-stage AMD64 builds from Mac
- **Azure Deployment**: Container Apps with auto-scaling
- **Health Monitoring**: `/health` endpoint with metrics
- **Logging**: Structured JSON logging with observability
- **TLS Setup**: Azure ingress handles TLS termination
- **CI/CD**: Single-command automated deployment
- **Tunnel Agent**: Complete C++ client implementation (100% done!)
  - TLS connection with ALPN support (RFC 7540)
  - HTTP/2 session management with nghttp2
  - Bearer token authentication
  - HTTP request forwarding to local services
  - Heartbeat monitoring (30s interval, 60s timeout)
  - Auto-reconnect with exponential backoff
  - Structured logging with spdlog
  - Configurable via JSON/CLI/environment

### ⚠️ Partially Implemented (Phase 2 - Server Integration)

- **AgentConnection**: Server can accept agent connections via `AgentHandshake`
- **AgentRegistry**: Server tracks connected agents
- **HTTP/2 Protocol**: Server supports HTTP/2 sessions with agents
- **Configuration**: Environment variables setup, needs Key Vault integration

### ❌ Not Yet Implemented (Phase 3 - End-to-End Functionality)

- **Management API**: Tunnel CRUD endpoints (POST/GET/DELETE `/v1/tunnels`)
- **HTTP Proxy**: Request routing from internet → agent based on Host header
- **TCP Proxy**: Raw TCP stream forwarding for printer use case
- **Token Management**: Secure token generation and storage in Key Vault
- **DNS Integration**: Azure DNS wildcard domain routing
- **Agent Deployment**: Docker image and deployment for tunnel-agent

## Next Implementation Steps

Based on [Feature Spec 001](specs/001-tunnel-core-server/spec.md), the priority order is:

### Priority 1: Management API (Enables tunnel registration)

Implement REST endpoints for tunnel management:
- `POST /v1/tunnels` - Create tunnel, generate token, return connection details
- `GET /v1/tunnels` - List all registered tunnels
- `GET /v1/tunnels/{id}` - Get tunnel details and connected agents
- `DELETE /v1/tunnels/{id}` - Delete tunnel and disconnect agents

**This unblocks**: Agent connections with valid tokens, tunnel tracking, monitoring

### Priority 2: HTTP Proxy (Enables HTTP tunneling use case)

Implement HTTP traffic routing from internet to agents:
- Parse `Host` header from incoming requests
- Match to registered tunnel and find connected agent
- Forward request through agent's HTTP/2 session
- Return response to client
- Handle errors (agent offline, timeout, etc.)

**This enables**: User Story 1 - HTTP tunnel for web apps

### Priority 3: Deploy Tunnel Agent (Make agent accessible)

Package and deploy the tunnel-agent:
- Create Dockerfile for tunnel-agent
- Build AMD64 image
- Deploy to Azure Container Apps or distribute as standalone binary
- Document agent deployment for customer premises

**This enables**: Customers to run agents in their networks

### Priority 4: TCP Proxy (Enables printer use case)

Implement TCP stream forwarding:
- Accept TCP connections on configured ports
- Route to appropriate agent based on tunnel config
- Bidirectional byte streaming
- Connection state management

**This enables**: User Story 2 - TCP tunnel for printer traffic


## Reference: Tunnel Agent Details

The tunnel agent is **fully implemented** in `/Volumes/Projects/protogate/tunnel-agent/`. 

### Agent Features

- **Secure Connection**: TLS 1.2+ with certificate verification
- **ALPN Support**: RFC 7540 compliant HTTP/2 negotiation
- **Authentication**: Bearer token authentication
- **HTTP/2 Protocol**: Efficient multiplexed connections using nghttp2
- **Request Forwarding**: Forwards HTTP requests to local services using Boost.Beast
- **Health Monitoring**: Automatic heartbeat (30s) with timeout detection (60s)
- **Auto Reconnect**: Exponential backoff (1s → 60s max)
- **Structured Logging**: JSON logs with spdlog
- **Flexible Config**: JSON file, CLI arguments, or environment variables

### Agent Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Tunnel Agent                        │
├─────────────────────────────────────────────────────┤
│  Main Entry (main.cpp)                              │
│  ├─ Configuration (JSON/CLI/ENV)                    │
│  └─ Event Loop (Boost.Asio)                         │
├─────────────────────────────────────────────────────┤
│  TLS Client (tls_client.h/cpp)                      │
│  ├─ OpenSSL integration                             │
│  ├─ ALPN support (h2, http/1.1)                     │
│  └─ Cipher suite configuration                      │
├─────────────────────────────────────────────────────┤
│  HTTP/2 Session (http2_session.h/cpp)               │
│  ├─ nghttp2 integration                             │
│  ├─ CONNECT handshake                               │
│  ├─ Stream management                               │
│  └─ Frame send/receive                              │
├─────────────────────────────────────────────────────┤
│  Request Forwarder (request_forwarder.h/cpp)        │
│  ├─ HTTP client (Boost.Beast)                       │
│  ├─ Header preservation                             │
│  └─ Response encoding                               │
├─────────────────────────────────────────────────────┤
│  Health Monitor (heartbeat.h/cpp)                   │
│  ├─ PING frame sender (30s)                         │
│  └─ Timeout detector (60s)                          │
├─────────────────────────────────────────────────────┤
│  Reconnection (reconnect.h/cpp)                     │
│  └─ Exponential backoff (1s → 60s)                  │
└─────────────────────────────────────────────────────┘
```

### Building the Agent

```bash
cd tunnel-agent

# Install dependencies (macOS)
brew install cmake boost openssl nghttp2 nlohmann-json spdlog

# Build
cmake -B build -S . -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build --config Release

# Binary location
./build/tunnel-agent
```

### Agent Configuration Example

```json
{
  "server_host": "protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io",
  "server_port": 8443,
  "tunnel_id": "my-app",
  "tunnel_token": "tnl_abc123...",
  "local_url": "http://localhost:3000",
  "log_level": "info",
  "heartbeat_interval": 30,
  "heartbeat_timeout": 60,
  "reconnect_initial_delay": 1,
  "reconnect_max_delay": 60
}
```

### Running the Agent

```bash
# Using config file
./build/tunnel-agent --config config.json

# Using environment variables
export PROTOGATE_SERVER_HOST=protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io
export PROTOGATE_SERVER_PORT=8443
export PROTOGATE_TUNNEL_ID=my-app
export PROTOGATE_TUNNEL_TOKEN=tnl_abc123...
export PROTOGATE_LOCAL_URL=http://localhost:3000
./build/tunnel-agent

# With CLI arguments
./build/tunnel-agent \
  --server-host protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io \
  --server-port 8443 \
  --tunnel-id my-app \
  --tunnel-token tnl_abc123... \
  --local-url http://localhost:3000
```

### Agent Testing

The agent includes comprehensive testing infrastructure:

```bash
cd tunnel-agent

# Local testing with mock server
./test-local.sh

# Integration testing
./test-integration.sh
```

**Documentation**:
- `tunnel-agent/README.md` - Complete agent documentation
- `tunnel-agent/IMPLEMENTATION_COMPLETE.md` - Implementation details
- `tunnel-agent/TESTING.md` - Testing guide
- `tunnel-agent/HTTP2_COMPATIBILITY.md` - HTTP/2 protocol details
- `tunnel-agent/LOCAL_TESTING_STATUS.md` - Test results

### Connection Flow

Once the Management API is implemented:

```
1. Admin creates tunnel via POST /v1/tunnels → receives token
2. Agent connects to server:8443 with tunnel_id and token
3. TLS handshake (TLS 1.2+)
4. ALPN negotiation (prefers "h2")
5. HTTP/1.1 CONNECT authentication (MVP compatibility)
6. Server validates token, responds "200 Connection established"
7. Server registers agent in AgentRegistry
8. HTTP/2 session begins with heartbeat
9. Internet requests → Server → Agent (HTTP/2 stream) → Local service
10. Response flows back through tunnel
```

## Architecture Overview

### Current Production Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Azure Container Apps                                         │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ Azure Ingress (HTTPS, port 443)                        │ │
│  │ - TLS termination                                      │ │
│  │ - Load balancing                                       │ │
│  └───────────────────────┬────────────────────────────────┘ │
│                          │ Plain HTTP                       │
│                          ▼                                   │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ Protogate Server (port 8080)                           │ │
│  │                                                         │ │
│  │  ✅ HealthServer      - /health endpoint               │ │
│  │  ⚠️ HTTPServer        - Management API (not impl)      │ │
│  │  ⚠️ AgentServer       - Port 8443 (accepts agents)     │ │
│  │  ⚠️ AgentConnection   - HTTP/2 session handling        │ │
│  │  ⚠️ AgentRegistry     - Tracks connected agents        │ │
│  │  ❌ HTTPProxy         - Traffic routing (planned)      │ │
│  │  ❌ TCPProxy          - Stream forwarding (planned)    │ │
│  │                                                         │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  Auto-scaling: 1-3 replicas based on CPU (70% threshold)    │
│  Health probes: HTTP GET /health every 10s                  │
│                                                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ Customer Premises (or any network)                          │
│                                                              │
│  ✅ Tunnel Agent (tunnel-agent/)                            │
│     - TLS client with ALPN                                  │
│     - HTTP/2 session manager                                │
│     - Request forwarder                                     │
│     - Health monitoring                                     │
│     - Auto-reconnect                                        │
│                                                              │
│  Status: Fully implemented, ready to deploy                 │
└─────────────────────────────────────────────────────────────┘
```

### Target Architecture (After Full Implementation)

```
Internet Client
     │
     │ HTTPS
     ▼
┌────────────────────────────────────────┐
│ Azure Ingress                          │
│ protogate-dev-app.*.azurecontainerapps.io │
└────────────┬───────────────────────────┘
             │ HTTP (TLS terminated)
             ▼
┌────────────────────────────────────────┐
│ Protogate Server (Container Apps)     │
│                                        │
│  Port 8080: HTTP/Management            │
│  - POST /v1/tunnels (create)          │
│  - GET /v1/tunnels (list)             │
│  - DELETE /v1/tunnels/{id}            │
│  - HTTP Proxy (route by Host header)  │
│                                        │
│  Port 8443: Agent Connections          │
│  - TLS handshake                       │
│  - Token authentication                │
│  - Bidirectional tunnel                │
└────────┬───────────────────────────────┘
         │ Persistent TLS tunnel
         ▼
┌────────────────────────────────────────┐
│ Tunnel Agent (Customer premises)      │
│  - Connects outbound to port 8443     │
│  - Maintains persistent connection    │
│  - Forwards to local services         │
└────────┬───────────────────────────────┘
         │ Local network
         ▼
┌────────────────────────────────────────┐
│ Local Service (HTTP/TCP)               │
│  - Web app on localhost:3000          │
│  - Printer on localhost:9100          │
│  - Database on localhost:5432         │
└────────────────────────────────────────┘
```

## Useful Commands

### Check Deployment Status
```bash
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "{status:properties.runningStatus, health:properties.latestRevisionName, url:properties.configuration.ingress.fqdn}"
```

### View Logs
```bash
az containerapp logs show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --tail 50 \
  --follow
```

### List All Revisions
```bash
az containerapp revision list \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "[].{revision:name, health:properties.healthState, traffic:properties.trafficWeight, created:properties.createdTime}"
```

### Check Container Metrics
```bash
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "properties.template.scale.{min:minReplicas, max:maxReplicas, rules:rules}"
```

### Test Health Endpoint
```bash
# Quick check
curl -s https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health | jq '.status'

# Full response with metrics
curl -s https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health | jq
```

## Development Workflow

### Make Code Changes

1. Edit source files in `src/`
2. Test locally (optional - requires local build)
3. Commit changes to Git
4. Deploy: `./build-and-push-local.sh`
5. Verify: Check health endpoint

### Local Testing (Optional)

```bash
# Build locally
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target protogate-server

# Run tests
./build/unit_tests          # 85 unit tests
./build/integration_tests   # 124 integration tests
./build/security_tests      # Security fuzzing tests

# Run server locally
./build/protogate-server
```

**Note**: Local testing requires environment variables for Azure resources (Key Vault, DNS). For most development, deploying to Azure is easier.

## Troubleshooting

### Deployment Issues

**Problem**: `exec format error`
- **Cause**: Wrong CPU architecture (ARM64 instead of AMD64)
- **Solution**: Deployment script already uses `--platform linux/amd64`

**Problem**: Container App shows "Unhealthy"
- **Check**: `az containerapp logs show --name protogate-dev-app --resource-group protogate-dev-rg --tail 100`
- **Verify**: Health endpoint returns HTTP 200

**Problem**: 502 Bad Gateway
- **Cause**: Container not listening on port 8080
- **Check**: Logs for "server ready" message
- **Verify**: Health probes are configured for port 8080

### Connection Issues

**Problem**: Cannot reach public URL
- **Verify**: `curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health`
- **Check**: Container App status is "Running"
- **Check**: At least 1 replica is active

**Problem**: Slow response times
- **Check**: Current replica count vs load
- **Adjust**: Auto-scaling rules if needed
- **Monitor**: Azure Container Apps metrics in portal




## Documentation Resources

- **Feature Specification**: [specs/001-tunnel-core-server/spec.md](specs/001-tunnel-core-server/spec.md) - Full requirements and user stories
- **Implementation Plan**: [specs/001-tunnel-core-server/plan.md](specs/001-tunnel-core-server/plan.md) - Architecture and technical decisions  
- **Agent Protocol**: [specs/001-tunnel-core-server/contracts/agent-protocol.md](specs/001-tunnel-core-server/contracts/agent-protocol.md) - Agent connection specification
- **Management API**: [specs/001-tunnel-core-server/contracts/management-api.yaml](specs/001-tunnel-core-server/contracts/management-api.yaml) - REST API specification
- **Deployment Guide**: [DEPLOYMENT.md](DEPLOYMENT.md) - Azure Container Apps deployment details
- **Task List**: [specs/001-tunnel-core-server/tasks.md](specs/001-tunnel-core-server/tasks.md) - Implementation checklist

## Need Help?

Current deployment is functional but incomplete. Next steps for full functionality:

1. **Implement Management API** - REST endpoints for tunnel management
2. **Build Agent Protocol** - TLS connection and authentication
3. **Add HTTP Proxy** - Route traffic based on Host header
4. **Add TCP Proxy** - Forward raw TCP streams
5. **Integrate Key Vault** - Secure token storage
6. **Setup DNS** - Wildcard domain routing

Refer to the [tasks.md](specs/001-tunnel-core-server/tasks.md) file for detailed implementation steps.

