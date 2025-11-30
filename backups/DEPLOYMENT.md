# Protogate Deployment Guide

## Current Deployment Status ✅

**Working Configuration**:
- **Container App**: protogate-dev-app
- **Image**: protogatedevacr.azurecr.io/protogate:v1.0.0
- **Public Endpoint**: https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/
- **Status**: ✅ PRODUCTION READY

### Port Configuration

Azure Container Apps ingress terminates TLS automatically. The container only needs to listen on plain HTTP:

- **Port 8080**: HTTP for public traffic (Container App ingress forwards here after TLS termination)
- **Port 8443**: HTTPS for agent connections (direct TLS, not through ingress)

**Key Insight**: Azure Container Apps ingress handles all TLS termination. You specify `targetPort: 8080` in the ingress configuration, and Azure forwards HTTPS traffic to your container as plain HTTP on that port.

### Container App Ingress Settings

```bash
az containerapp ingress show --name protogate-dev-app --resource-group protogate-dev-rg
```

Output:
```json
{
  "targetPort": 8080,           # Container listens on HTTP port 8080
  "transport": "Http",          # Azure sends plain HTTP to container
  "allowInsecure": false,       # External traffic must use HTTPS
  "external": true,             # Publicly accessible
  "fqdn": "protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io"
}
```

## Deployment Process

**The proper solution** is to add a dedicated HTTP endpoint on port 8080 for health checks.

### What Changed

1. **New HealthServer** (`src/server/health_server.cpp`):
   - Simple HTTP server (no TLS) listening on port 8080
   - Responds to `/health` requests
   - Uses existing `HealthHandler` for status checks
   
2. **Dockerfile Updated**:
   - Exposes port 8080

### Prerequisites

1. **Azure CLI** installed and logged in
2. **Docker** with buildx support for cross-platform builds
3. **Azure Container Registry** access configured
4. **Git** repository access

### Quick Deploy Script

Use the provided `build-and-push-local.sh` script:

```bash
#!/bin/bash
set -e

# Configuration
ACR_NAME="protogatedevacr"
APP_NAME="protogate-dev-app"
RG="protogate-dev-rg"
COMMIT_SHA=$(git rev-parse --short HEAD)
TIMESTAMP=$(date +%s)
IMAGE_TAG="v1.0.0-${COMMIT_SHA}-${TIMESTAMP}"

# Login to ACR
az acr login --name ${ACR_NAME}

# Build AMD64 image and push to ACR
docker buildx build \
  --platform linux/amd64 \
  --build-arg CACHEBUST=${TIMESTAMP} \
  --no-cache \
  -f docker/Dockerfile.alpine \
  -t ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG} \
  --push \
  .

# Deploy to Container App
az containerapp update \
  --name ${APP_NAME} \
  --resource-group ${RG} \
  --image ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG}

# Wait and verify
sleep 30
LATEST_REV=$(az containerapp show --name ${APP_NAME} --resource-group ${RG} \
  --query "properties.latestRevisionName" -o tsv)
HEALTH=$(az containerapp revision show --name ${APP_NAME} --resource-group ${RG} \
  --revision ${LATEST_REV} --query "properties.healthState" -o tsv)

if [ "${HEALTH}" = "Healthy" ]; then
  echo "✓ Deployment successful!"
  curl -s https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health
else
  echo "✗ Deployment failed"
  exit 1
fi
```

### Manual Deployment Steps

If you prefer manual deployment:

```bash
# 1. Login to ACR
az acr login --name protogatedevacr

# 2. Build and push AMD64 image
docker buildx build \
  --platform linux/amd64 \
  -f docker/Dockerfile.alpine \
  -t protogatedevacr.azurecr.io/protogate:v1.0.0 \
  --push \
  .

# 3. Deploy to Container App
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --image protogatedevacr.azurecr.io/protogate:v1.0.0

# 4. Verify deployment
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "properties.{latest:latestRevisionName,status:runningStatus}"
```

## Health Checks Configuration

The Container App uses HTTP health probes on port 8080:

```yaml
probes:
  - type: Liveness
    httpGet:
      path: /health
      port: 8080
      scheme: HTTP
    initialDelaySeconds: 10
    periodSeconds: 30
    timeoutSeconds: 5
    failureThreshold: 3
    
  - type: Readiness
    httpGet:
      path: /health
      port: 8080
      scheme: HTTP
    initialDelaySeconds: 5
    periodSeconds: 10
    timeoutSeconds: 3
    failureThreshold: 3
```

Health endpoint returns:
```json
{
  "status": "healthy",
  "version": "1.0.0",
  "service": "protogate",
  "timestamp": 1764243133007,
  "checks": {
    "memory": {"status": "pass", "message": "Memory usage normal"},
    "error_rate": {"status": "pass", "message": "Error rate normal", "rate": 0.0}
  },
  "metrics": {
    "active_tunnels": 0,
    "total_http_requests": 0,
    "active_http_connections": 0
  }
}
```

## Troubleshooting

### Common Issues

1. **"exec format error"**
   - **Cause**: ARM64 binary deployed to AMD64 Container App
   - **Solution**: Always use `--platform linux/amd64` in Docker build
   
2. **Health probes failing**
   - **Cause**: Wrong port or scheme configuration
   - **Solution**: Verify probes use HTTP on port 8080

3. **502 Bad Gateway**
   - **Cause**: Ingress targetPort mismatch
   - **Solution**: Set ingress `targetPort: 8080`
   
4. **Container App secret errors**
   - **Cause**: Orphaned registry-password secret
   - **Solution**: Remove secret, use managed identity only

### Verification Commands

```bash
# Check revision health
az containerapp revision list \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "[].{name:name, health:properties.healthState, traffic:properties.trafficWeight}" \
  -o table

# Test public endpoint
curl -s https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health | jq

# View logs
az containerapp logs show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --tail 50 --follow

# Check ingress configuration
az containerapp ingress show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg
```

## Architecture Notes

### Why Port 8080 for Public Traffic?

Azure Container Apps ingress acts as a reverse proxy:

1. **External client** → HTTPS request to `protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io`
2. **Azure ingress** → Terminates TLS, forwards plain HTTP to container
3. **Container port 8080** → Receives plain HTTP request, processes it
4. **Container** → Sends HTTP response back
5. **Azure ingress** → Encrypts response with TLS, sends to client
6. **External client** → Receives HTTPS response

**Benefits**:
- No TLS certificate management in container
- Automatic cert rotation by Azure
- Simpler container configuration
- Better performance (Azure handles TLS)

### Port Usage Summary

| Port | Protocol | Purpose | Exposed Externally |
|------|----------|---------|-------------------|
| 8080 | HTTP | Public traffic (via Container App ingress) | Yes (as HTTPS) |
| 8443 | HTTPS | Agent connections (direct TLS) | Yes (port mapping) |

## Registry Authentication

Container App uses **UserAssigned Managed Identity** for ACR authentication:

```bash
# Verify identity configuration
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "identity.userAssignedIdentities"

# Verify registry configuration
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "properties.configuration.registries"
```

**Important**: Do NOT use `registry-password` secret. The managed identity handles authentication automatically.

## GitHub Actions Deployment (Future)

For CI/CD pipeline deployment:

```yaml
name: Deploy to Azure Container Apps

on:
  push:
    branches: [main]

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Login to Azure
        uses: azure/login@v1
        with:
          creds: ${{ secrets.AZURE_CREDENTIALS }}
      
      - name: Build and push image
        run: |
          az acr login --name protogatedevacr
          docker buildx build \
            --platform linux/amd64 \
            -f docker/Dockerfile.alpine \
            -t protogatedevacr.azurecr.io/protogate:${{ github.sha }} \
            --push \
            .
      
      - name: Deploy to Container App
        run: |
          az containerapp update \
            --name protogate-dev-app \
            --resource-group protogate-dev-rg \
            --image protogatedevacr.azurecr.io/protogate:${{ github.sha }}
```


## Success Criteria

Deployment is successful when:

✅ Revision status is "Healthy"  
✅ `curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health` returns HTTP 200  
✅ Health endpoint returns valid JSON with `"status": "healthy"`  
✅ Logs show "server ready" message  
✅ No error messages in container logs

