# Protogate Testing Guide

## Current Deployment Status

**Server URL**: `https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io`  
**Status**: ✅ Deployed with health probes configured  
**Image**: `protogatedevacr.azurecr.io/protogate:v1.0.0` (AMD64, Ubuntu-based)

---

## Quick Health Check

```bash
# Test health endpoint
curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health

# Expected response (when healthy):
# {"status":"healthy","timestamp":"2025-11-27T00:00:00Z","version":"1.0.0","uptime_seconds":123}
```

---

## Container App Configuration

### Environment Variables Required

```bash
KEY_VAULT_URI=https://protogatedevkv.vault.azure.net/
DNS_ZONE=protogate.dev
LOG_ANALYTICS_WORKSPACE_ID=821b7bc8-cc36-48c1-a499-20c132f45e16
AZURE_CLIENT_ID=76d3ff9f-012a-488d-823b-cea778eaf4a3
```

### Health Probes Configured

**Liveness Probe**:
- Path: `/health`
- Port: 443
- Initial Delay: 10s
- Period: 30s
- Timeout: 5s
- Failure Threshold: 3

**Readiness Probe**:
- Path: `/health`
- Port: 443
- Initial Delay: 5s
- Period: 10s
- Timeout: 3s
- Failure Threshold: 3

---

## Testing with Tunnel Agent

### 1. Create a Tunnel Token

```bash
# Generate a test token in Key Vault
az keyvault secret set \
  --vault-name protogatedevkv \
  --name "tunnel-token-test01" \
  --value "$(openssl rand -base64 32)"

# Get the token
TOKEN=$(az keyvault secret show \
  --vault-name protogatedevkv \
  --name "tunnel-token-test01" \
  --query value -o tsv)

echo "Token: $TOKEN"
```

### 2. Build Tunnel Agent

```bash
cd /Volumes/Projects/protogate/tunnel-agent

# Build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Or use Docker
docker build -t tunnel-agent:latest -f ../docker/Dockerfile.agent .
```

### 3. Configure Agent

Create `config.json`:

```json
{
  "server": "protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io",
  "port": 8443,
  "token": "YOUR_TOKEN_HERE",
  "tunnel_id": "test01",
  "local_url": "http://localhost:3000",
  "tls": {
    "verify": true
  },
  "logging": {
    "level": "debug",
    "format": "json"
  }
}
```

### 4. Run Agent

```bash
# Native
./build/tunnel-agent --config config.json

# Docker
docker run --rm -v $(pwd)/config.json:/app/config.json \
  tunnel-agent:latest --config /app/config.json
```

### 5. Test HTTP Tunnel

```bash
# In another terminal, start a local web server
python3 -m http.server 3000

# Make request through tunnel (from anywhere)
curl https://test01.protogate.dev/

# Should proxy to your local server
```

---

## Debugging Container App

### Check Logs

```bash
# Real-time logs
az containerapp logs show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --follow

# Recent logs with errors
az containerapp logs show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --format text \
  --tail 100 | grep -E "(ERROR|WARNING|failed)"
```

### Check Revision Health

```bash
az containerapp revision list \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "[].{name:name,health:properties.healthState,replicas:properties.replicas,traffic:properties.trafficWeight}" \
  --output table
```

### Restart Container

```bash
# Restart current revision
az containerapp revision restart \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --revision $(az containerapp show \
    --name protogate-dev-app \
    --resource-group protogate-dev-rg \
    --query properties.latestRevisionName -o tsv)
```

### Update Configuration

```bash
# Update environment variables
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --set-env-vars \
    KEY_VAULT_URI=https://protogatedevkv.vault.azure.net/ \
    DNS_ZONE=protogate.dev \
    LOG_ANALYTICS_WORKSPACE_ID=821b7bc8-cc36-48c1-a499-20c132f45e16 \
    AZURE_CLIENT_ID=76d3ff9f-012a-488d-823b-cea778eaf4a3
```

---

## Rebuild and Deploy

### Trigger GitHub Actions Build

```bash
# Trigger workflow
gh workflow run build-docker.yml

# Check status
gh run list --workflow=build-docker.yml --limit 1

# View logs
gh run view --log
```

### Manual Docker Build

```bash
cd /Volumes/Projects/protogate

# Build AMD64 image
docker build --platform linux/amd64 \
  -f docker/Dockerfile.alpine \
  -t protogatedevacr.azurecr.io/protogate:v1.0.0 .

# Login to ACR
az acr login --name protogatedevacr

# Push
docker push protogatedevacr.azurecr.io/protogate:v1.0.0

# Update Container App
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --image protogatedevacr.azurecr.io/protogate:v1.0.0
```

---

## Common Issues

### 503 Service Unavailable

**Cause**: Container not passing health checks or no healthy replicas  
**Fix**: Check logs, verify env vars, ensure certificate in Key Vault

```bash
# Check if certificate exists
az keyvault certificate show \
  --vault-name protogatedevkv \
  --name wildcard-protogate-dev

# Verify managed identity permissions
az role assignment list \
  --assignee 76d3ff9f-012a-488d-823b-cea778eaf4a3 \
  --scope /subscriptions/fba68909-a61e-43fd-8bff-4dcdaa99c2d6/resourceGroups/protogate-dev-rg/providers/Microsoft.KeyVault/vaults/protogatedevkv
```

### Container Fails to Start

**Cause**: Missing environment variables or Key Vault access  
**Fix**: Verify all required env vars are set and identity has permissions

```bash
# Check env vars
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "properties.template.containers[0].env"
```

### Health Probe Failures

**Cause**: Server not responding on port 443 or /health endpoint broken  
**Fix**: Check container logs for startup errors

```bash
# Check if server started
az containerapp logs show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --format text | grep "Protogate server ready"
```

---

## Load Testing

```bash
# Install hey (HTTP load generator)
brew install hey  # macOS
# or download from https://github.com/rakyll/hey

# Simple load test
hey -n 1000 -c 10 https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health

# Monitor during load
watch -n 2 'az containerapp revision list --name protogate-dev-app --resource-group protogate-dev-rg --query "[0].properties.replicas" -o tsv'
```

---

## CI/CD Pipeline

GitHub Actions workflow: `.github/workflows/build-docker.yml`

**Triggers**:
- Manual: `workflow_dispatch`
- Auto: Push to `main` or `001-tunnel-core-server` branch

**Steps**:
1. Checkout code
2. Set up Docker Buildx
3. Login to ACR
4. Build AMD64 image
5. Push to `protogatedevacr.azurecr.io/protogate:v1.0.0`

**No automatic deployment** - Update Container App manually after build succeeds.

---

## Next Steps

1. ✅ Server deployed with health probes
2. ⏳ Create tunnel token in Key Vault
3. ⏳ Test with tunnel-agent
4. ⏳ Set up DNS records for `*.protogate.dev`
5. ⏳ Replace self-signed cert with Let's Encrypt
6. ⏳ Configure auto-scaling rules
7. ⏳ Set up monitoring and alerts
