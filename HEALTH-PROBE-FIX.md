# Health Probe Fix - Port 8080 HTTP Endpoint

## What Was Fixed

Added a dedicated **HTTP health endpoint on port 8080** to solve Container App health probe issues.

## The Problem

Your server listens on **port 443 with HTTPS** (TLS required). Health probes were configured to send **plain HTTP to port 443**, which failed because:

1. Probe sends: `GET /health HTTP/1.1` (plain text)
2. Server expects: TLS handshake first
3. Result: Connection error → probe fails → revision never becomes healthy

## Why Port 8080 Is Better

### Compared to TCP Probes
**TCP probes** (port 443):
- ❌ Only check if port is open
- ❌ Don't verify app is actually healthy
- ❌ Can't detect deadlocks, OOM, database issues
- ❌ Less debugging info

**HTTP probes** (port 8080):
- ✅ Test actual application logic
- ✅ Return JSON health status
- ✅ Detect application-level failures
- ✅ Standard Kubernetes/container pattern
- ✅ No TLS overhead
- ✅ Fast responses

### Compared to HTTPS Probes (port 443)
**HTTPS probes**:
- ❌ Require certificate validation
- ❌ TLS handshake overhead
- ❌ Self-signed cert issues
- ❌ More complex config

**HTTP on 8080**:
- ✅ Simple, fast, no TLS
- ✅ Dedicated port for health
- ✅ Doesn't interfere with production traffic

## What Changed

### 1. Code Changes

**New Files**:
- `src/server/health_server.h` - Simple HTTP server class
- `src/server/health_server.cpp` - Handles `/health` requests on port 8080

**Modified Files**:
- `src/server/main.cpp` - Start health server alongside HTTPS servers
- `docker/Dockerfile.alpine` - Expose port 8080, update healthcheck
- `deploy/azure/container-app.bicep` - Update probes to port 8080
- `deploy/deploy-containerapp.sh` - Deploy with correct probe config

### 2. Architecture

```
┌─────────────────────────────────────┐
│   Azure Container App Ingress       │
│   (protogate-dev-app.*.io)          │
└────────────┬────────────────────────┘
             │
             │ HTTPS (public traffic)
             ▼
     ┌───────────────┐
     │  Port 443     │  ← HTTPS Server (production)
     │  (HTTPS/TLS)  │     + Client requests
     └───────────────┘     + /health endpoint
     
     ┌───────────────┐
     │  Port 8080    │  ← Health Server (probes only)
     │  (HTTP)       │     + /health endpoint
     └───────────────┘     + No TLS overhead
             ▲              + Fast probe response
             │
             │ HTTP (internal only)
     ┌───────────────┐
     │ Health Probes │  ← Container Apps Platform
     │ Liveness      │     Checks every 30s
     │ Readiness     │     Checks every 10s
     └───────────────┘
     
     ┌───────────────┐
     │  Port 8443    │  ← Agent Server
     │  (HTTPS/TLS)  │     + Tunnel agent connections
     └───────────────┘
```

### 3. Port Summary

| Port | Protocol | Purpose | Exposed Publicly |
|------|----------|---------|------------------|
| 443  | HTTPS    | Production traffic, /health | Yes (via ingress) |
| 8080 | HTTP     | Health probes only | No (internal only) |
| 8443 | HTTPS    | Tunnel agent connections | Yes (via ingress) |

## Deployment

### Option 1: Automated (Recommended)

```bash
# Commit changes
git add -A
git commit -m "Add HTTP health endpoint on port 8080"
git push origin 001-tunnel-core-server

# GitHub Actions will build and push AMD64 image automatically

# Then deploy using script
./deploy/deploy-containerapp.sh
```

### Option 2: Manual

```bash
# 1. Build AMD64 image
docker buildx build \
  --platform linux/amd64 \
  -f docker/Dockerfile.alpine \
  -t protogatedevacr.azurecr.io/protogate:v1.0.0 \
  --push \
  .

# 2. Deploy with updated probes
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --yaml /tmp/containerapp-deploy.yaml  # (see DEPLOYMENT.md)

# 3. Verify health
az containerapp revision list \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "[?properties.trafficWeight > \`0\`].{Name:name, Health:properties.healthState}" -o table
```

## Testing Locally

```bash
# Build and run container
docker build -f docker/Dockerfile.alpine -t protogate:test .
docker run -p 443:443 -p 8080:8080 -p 8443:8443 \
  -e KEY_VAULT_URI=mock://keyvault \
  -e DNS_ZONE=protogate.dev \
  protogate:test

# Test health endpoints
curl http://localhost:8080/health  # HTTP health port
curl -k https://localhost:443/health  # HTTPS production port
```

Expected response:
```json
{
  "status": "healthy",
  "timestamp": "2025-11-26T...",
  "version": "1.0.0"
}
```

## Expected Outcome

After deployment, you should see:

```bash
$ az containerapp revision list --name protogate-dev-app --resource-group protogate-dev-rg \
  --query "[].{Name:name, Health:properties.healthState, Traffic:properties.trafficWeight}" -o table

Name                        Health    Traffic
--------------------------  --------  ---------
protogate-dev-app--0000007  Healthy   100
```

✅ **Health: Healthy** - Probes passing on port 8080
✅ **Traffic: 100** - Receiving all production traffic
✅ **Both endpoints work**: HTTPS (443) for clients, HTTP (8080) for probes

## Summary

**Before**: Health probes → HTTP to port 443 → TLS mismatch → probe fails → no healthy revision

**After**: Health probes → HTTP to port 8080 → simple HTTP response → probe passes → healthy revision serves traffic

This is the **standard production pattern** used by Kubernetes and cloud platforms for health monitoring.
