# Protogate Deployment Guide

## Current Deployment Status

**Working Configuration**:
- **Container App**: protogate-dev-app
- **Image**: protogatedevacr.azurecr.io/protogate:v1.0.0 (AMD64, Ubuntu 22.04)
- **Status**: Server starts successfully and logs show "server ready"
- **Issue**: Health probes failing due to HTTP/HTTPS scheme mismatch

## Problem Explanation

### Why Some Revisions Work and Others Don't

**Root Cause**: Health probe configuration mismatch

1. **Server Configuration**:
   - Port **443**: HTTPS (TLS enabled) for production traffic
   - Port **8080**: HTTP (no TLS) for health checks
   - Port **8443**: HTTPS for agent connections

2. **Previous Health Probe Configuration** (INCORRECT):
   ```yaml
   httpGet:
     path: /health
     port: 443
     scheme: HTTP  # ❌ WRONG - Server port 443 expects HTTPS
   ```

3. **What Happened**:
   - Health probe sent plain HTTP request to port 443
   - Server expected TLS handshake, got plain HTTP
   - Connection failed, probe never passed
   - Revision stayed in "Activating" state forever

**Revision History**:
- **Revision 3**: Created without health probes, became healthy, worked fine
- **Revisions 4-6**: Created with incorrect HTTP probes on HTTPS port, never passed health checks

### Why This Was Complex

Azure Container Apps has several moving parts:
1. **Environment Variables**: Lost when using YAML updates (Azure CLI bug)
2. **Health Probes**: Must match server protocol exactly
3. **Revision Management**: New revision created on every update
4. **Traffic Routing**: Only healthy revisions receive traffic
5. **Scale to Zero**: Unhealthy revisions don't count toward min replicas

## Solution: Dedicated HTTP Health Port (IMPLEMENTED)

**The proper solution** is to add a dedicated HTTP endpoint on port 8080 for health checks.

### What Changed

1. **New HealthServer** (`src/server/health_server.cpp`):
   - Simple HTTP server (no TLS) listening on port 8080
   - Responds to `/health` requests
   - Uses existing `HealthHandler` for status checks
   
2. **Dockerfile Updated**:
   - Exposes port 8080
   - Health check uses `http://localhost:8080/health`
   
3. **Bicep Template Updated**:
   - Probes configured for HTTP on port 8080
   
4. **Deployment Script Updated**:
   - Uses HTTP probes on port 8080

### Why This Is Better Than TCP Probes

**HTTP Probes** (✅ IMPLEMENTED):
- ✅ Verifies application logic actually works
- ✅ Returns detailed health status (JSON)
- ✅ Standard practice for production monitoring
- ✅ Better debugging information
- ✅ No TLS overhead for health checks
- ✅ Fast probe responses

**TCP Probes** (❌ NOT RECOMMENDED):
- ❌ Only checks if port is open
- ❌ Can't detect application-level failures (deadlock, OOM, etc.)
- ❌ Less useful for debugging
- ❌ Doesn't verify the app is actually healthy

**HTTPS Probes on Port 443** (❌ PROBLEMATIC):
- ❌ Requires certificate validation
- ❌ TLS overhead on every probe
- ❌ Self-signed cert issues
- ❌ More complex configuration

## Recommended Deployment Script

Save this as `deploy/deploy-containerapp.sh`:

```bash
#!/bin/bash
set -e

# Configuration
RESOURCE_GROUP="protogate-dev-rg"
APP_NAME="protogate-dev-app"
ACR_NAME="protogatedevacr"
IMAGE_TAG="v1.0.0"
KEY_VAULT_NAME="protogatedevkv"

echo "=== Protogate Container App Deployment ==="
echo ""

# Step 1: Build and push image (if needed)
echo "Step 1: Checking if image rebuild is needed..."
read -p "Rebuild Docker image? (y/N): " REBUILD
if [[ "$REBUILD" =~ ^[Yy]$ ]]; then
    echo "Building AMD64 image..."
    docker buildx build \
        --platform linux/amd64 \
        -f docker/Dockerfile.alpine \
        -t ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG} \
        --push \
        .
    echo "✓ Image built and pushed"
else
    echo "✓ Using existing image"
fi
echo ""

# Step 2: Get Key Vault details
echo "Step 2: Retrieving Key Vault details..."
KV_URI=$(az keyvault show --name ${KEY_VAULT_NAME} --query "properties.vaultUri" -o tsv)
WORKSPACE_ID=$(az containerapp env show \
    --name protogate-dev-env \
    --resource-group ${RESOURCE_GROUP} \
    --query "properties.appLogsConfiguration.logAnalyticsConfiguration.customerId" -o tsv)
MANAGED_IDENTITY_CLIENT_ID=$(az containerapp show \
    --name ${APP_NAME} \
    --resource-group ${RESOURCE_GROUP} \
    --query "identity.userAssignedIdentities.*.clientId" -o tsv | head -1)

echo "✓ Key Vault URI: ${KV_URI}"
echo "✓ Workspace ID: ${WORKSPACE_ID}"
echo "✓ Managed Identity Client ID: ${MANAGED_IDENTITY_CLIENT_ID}"
echo ""

# Step 3: Create deployment YAML with TCP probes and env vars
echo "Step 3: Creating deployment configuration..."
cat > /tmp/containerapp-deploy.yaml <<EOF
properties:
  template:
    containers:
    - name: protogate-server
      image: ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG}
      env:
      - name: KEY_VAULT_URI
        value: ${KV_URI}
      - name: DNS_ZONE
        value: protogate.dev
      - name: LOG_ANALYTICS_WORKSPACE_ID
        value: ${WORKSPACE_ID}
      - name: AZURE_CLIENT_ID
        value: ${MANAGED_IDENTITY_CLIENT_ID}
      resources:
        cpu: 0.5
        memory: 1Gi
      probes:
      - type: Liveness
        tcpSocket:
          port: 443
        initialDelaySeconds: 10
        periodSeconds: 30
        timeoutSeconds: 5
        failureThreshold: 3
      - type: Readiness
        tcpSocket:
          port: 443
        initialDelaySeconds: 5
        periodSeconds: 10
        timeoutSeconds: 3
        failureThreshold: 3
    scale:
      minReplicas: 1
      maxReplicas: 3
EOF
echo "✓ Deployment configuration created"
echo ""

# Step 4: Apply configuration
echo "Step 4: Deploying to Azure Container Apps..."
az containerapp update \
    --name ${APP_NAME} \
    --resource-group ${RESOURCE_GROUP} \
    --yaml /tmp/containerapp-deploy.yaml

echo "✓ Deployment complete"
echo ""

# Step 5: Wait for health check
echo "Step 5: Waiting for container to become healthy..."
sleep 30

REVISION=$(az containerapp revision list \
    --name ${APP_NAME} \
    --resource-group ${RESOURCE_GROUP} \
    --query "[?properties.trafficWeight > \`0\`].name" -o tsv)

echo "Active revision: ${REVISION}"

for i in {1..12}; do
    HEALTH=$(az containerapp revision show \
        --name ${APP_NAME} \
        --resource-group ${RESOURCE_GROUP} \
        --revision ${REVISION} \
        --query "properties.healthState" -o tsv)
    
    echo "Health check attempt $i/12: ${HEALTH}"
    
    if [ "$HEALTH" == "Healthy" ]; then
        echo "✓ Container is healthy!"
        break
    fi
    
    if [ $i -eq 12 ]; then
        echo "❌ Container failed to become healthy"
        echo "Checking logs..."
        az containerapp logs show \
            --name ${APP_NAME} \
            --resource-group ${RESOURCE_GROUP} \
            --revision ${REVISION} \
            --tail 20
        exit 1
    fi
    
    sleep 10
done
echo ""

# Step 6: Test endpoint
echo "Step 6: Testing health endpoint..."
APP_URL=$(az containerapp show \
    --name ${APP_NAME} \
    --resource-group ${RESOURCE_GROUP} \
    --query "properties.configuration.ingress.fqdn" -o tsv)

echo "Application URL: https://${APP_URL}"
echo ""
echo "Testing health endpoint..."
curl -s "https://${APP_URL}/health" | jq . || echo "Health endpoint not responding via HTTPS"
echo ""

echo "=== Deployment Complete ==="
echo ""
echo "Next steps:"
echo "1. Create tunnel token: az keyvault secret set --vault-name ${KEY_VAULT_NAME} --name tunnel-token-test01 --value \$(openssl rand -base64 32)"
echo "2. Build tunnel-agent: cd tunnel-agent && cmake -B build && cmake --build build"
echo "3. Test with agent: ./build/tunnel-agent --config config.json"
echo ""
echo "For detailed testing instructions, see TESTING.md"
```

Make it executable:
```bash
chmod +x deploy/deploy-containerapp.sh
```

## Deployment Steps

### 1. Build and Push New Image

The code now includes a dedicated HTTP health endpoint on port 8080. Build and push:

```bash
# Trigger GitHub Actions build
git add -A
git commit -m "Add HTTP health endpoint on port 8080"
git push origin 001-tunnel-core-server

# Or build manually:
docker buildx build \
  --platform linux/amd64 \
  -f docker/Dockerfile.alpine \
  -t protogatedevacr.azurecr.io/protogate:v1.0.0 \
  --push \
  .
```

### 2. Deploy with Updated Probes

Use the deployment script:

```bash
cd /Volumes/Projects/protogate
./deploy/deploy-containerapp.sh
```

Or deploy manually:

```bash
cat > /tmp/containerapp-deploy.yaml <<'EOF'
properties:
  template:
    containers:
    - name: protogate-server
      image: protogatedevacr.azurecr.io/protogate:v1.0.0
      env:
      - name: KEY_VAULT_URI
        value: https://protogatedevkv.vault.azure.net/
      - name: DNS_ZONE
        value: protogate.dev
      - name: LOG_ANALYTICS_WORKSPACE_ID
        value: 821b7bc8-cc36-48c1-a499-20c132f45e16
      - name: AZURE_CLIENT_ID
        value: 76d3ff9f-012a-488d-823b-cea778eaf4a3
      resources:
        cpu: 0.5
        memory: 1Gi
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
    scale:
      minReplicas: 1
      maxReplicas: 3
EOF

az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --yaml /tmp/containerapp-deploy.yaml
```

### 3. Wait for Health Check

```bash
# Wait 30 seconds for deployment
sleep 30

# Check revision health
az containerapp revision list \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "[].{Name:name, Health:properties.healthState, Running:properties.runningState, Traffic:properties.trafficWeight}" -o table
```

### 4. Test Endpoints

```bash
# Test public HTTPS endpoint (production traffic)
curl -s https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health | jq .

# Note: Port 8080 is only accessible internally for health probes
# It's not exposed via ingress for security reasons
```

## Summary

**Why complexity exists**:
1. Azure Container Apps creates immutable revisions on every update
2. Health probes must match server protocol exactly (HTTP vs HTTPS)
3. YAML updates can lose environment variable values (Azure CLI issue)
4. Multiple configuration dimensions: image, env vars, probes, scale settings

**Best practice going forward**:
1. Use TCP probes for HTTPS services (or add dedicated HTTP health port)
2. Always include env vars in YAML updates
3. Test health endpoint independently before deploying
4. Use deployment script to ensure consistent configuration
5. Clean up old revisions regularly

**Current action needed**:
Run the quick fix above to apply TCP probes and get a healthy revision.
