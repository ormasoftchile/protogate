# Protogate - Quick Start Guide

Complete guide to deploy and use Protogate tunneling service on Azure.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Deploy Test Environment](#deploy-test-environment)
3. [Verify Deployment](#verify-deployment)
4. [Create Your First Tunnel](#create-your-first-tunnel)
5. [Run End-to-End Tests](#run-end-to-end-tests)
6. [Teardown](#teardown)
7. [Production Deployment](#production-deployment)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Required Tools

Install these tools before proceeding:

```bash
# Azure CLI (>= 2.50.0)
brew install azure-cli

# Docker with buildx (for multi-arch builds)
# Download from: https://www.docker.com/products/docker-desktop

# Python 3 (for test service)
brew install python3

# Verify installations
az --version          # Should be >= 2.50.0
docker --version      # Should be >= 24.0
python3 --version     # Should be >= 3.8
```

### Azure Access

```bash
# Login to Azure
az login

# Set your subscription (if you have multiple)
az account list --output table
az account set --subscription "YOUR_SUBSCRIPTION_NAME"

# Verify access
az account show
```

---

## Deploy Test Environment

### Step 1: Deploy Complete Environment

This single command deploys everything: Resource Group, Container Registry, Key Vault, Log Analytics, Container Apps Environment, and Server.

```bash
./scripts/deploy-test-env.sh --env test
```

**What it does:**
- Creates `protogate-test-rg` resource group
- Creates `protogate-test-logs` Log Analytics workspace
- Creates `protogate-test-kv` Key Vault (auto-generated)
- Creates `protogatetest acr` Container Registry
- Builds and pushes Docker image (multi-arch: AMD64 + ARM64)
- Creates `protogate-test-env` Container Apps environment
- Deploys `protogate-test-server` with managed identity
- Grants Key Vault access to server
- Configures health probes
- Saves configuration to `azure/.server-url-test`

**Expected output:**
```
[INFO] Using resource group: protogate-test-rg
[SUCCESS] Resource group created: protogate-test-rg
[SUCCESS] Log Analytics workspace created: protogate-test-logs
[SUCCESS] Key Vault created: protogate-test-kv
[SUCCESS] Container Registry created: protogatetest acr
[SUCCESS] Image built and pushed: protogatetest acr.azurecr.io/protogate-server:latest
[SUCCESS] Container Apps environment created: protogate-test-env
[SUCCESS] Container app created: protogate-test-server
[SUCCESS] Managed identity enabled
[SUCCESS] Key Vault access granted
[SUCCESS] Health probe configured
[SUCCESS] Deployment completed!

Server URL: https://protogate-test-server.whitesea-e4a76aae.westus2.azurecontainerapps.io
```

**Time:** ~10-15 minutes (first deployment)

---

## Verify Deployment

### Check Server Health

```bash
# Load server URL from saved config
SERVER_URL=$(cat azure/.server-url-test)

# Health check
curl $SERVER_URL/health

# Expected output:
# {"status":"healthy","version":"1.0.0"}
```

### View Deployment Details

```bash
# Container app details
az containerapp show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --query "{Name:name, Status:properties.runningStatus, URL:properties.configuration.ingress.fqdn}" \
  --output table

# Key Vault details
az keyvault show \
  --name protogate-test-kv \
  --resource-group protogate-test-rg \
  --query "{Name:name, Location:location, URI:properties.vaultUri}" \
  --output table

# View container logs
az containerapp logs show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --tail 50
```

---

## Create Your First Tunnel

### Via Management API

```bash
# Create tunnel
curl -X POST $SERVER_URL/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "my-api",
    "protocol": "HTTP",
    "target_host": "localhost",
    "target_port": 3000
  }'

# Expected output:
# {
#   "tunnel_id": "my-api",
#   "protocol": "HTTP",
#   "target_host": "localhost",
#   "target_port": 3000,
#   "status": "ACTIVE",
#   "token": "eyJhbGc..."  # Save this token!
# }
```

### List All Tunnels

```bash
curl $SERVER_URL/v1/tunnels

# Expected output:
# [
#   {
#     "tunnel_id": "my-api",
#     "protocol": "HTTP",
#     "status": "ACTIVE",
#     "created_at": "2025-11-28T10:00:00Z"
#   }
# ]
```

### Get Tunnel Details

```bash
curl $SERVER_URL/v1/tunnels/my-api

# Expected output:
# {
#   "tunnel_id": "my-api",
#   "protocol": "HTTP",
#   "target_host": "localhost",
#   "target_port": 3000,
#   "status": "ACTIVE",
#   "agents_connected": 0
# }
```

### Delete Tunnel

```bash
curl -X DELETE $SERVER_URL/v1/tunnels/my-api

# Expected: HTTP 204 No Content
```

---

## Run End-to-End Tests

Automated tests verify complete tunnel flow: server → agent → local service.

```bash
./scripts/e2e-test.sh --env test
```

**What it tests:**
1. Server health endpoint (`/health`)
2. Local test HTTP service (starts on port 3000)
3. Tunnel creation via Management API
4. Full HTTP proxying flow (if agent implemented)

**Expected output:**
```
[INFO] Starting E2E tests...
[INFO] Environment: test
=========================================
Running test suite...
=========================================

[INFO] Test 1: Checking server health...
[SUCCESS] ✓ Server is healthy
  Response: {"status":"healthy","version":"1.0.0"}

[INFO] Starting test HTTP service on port 3000...
[SUCCESS] Test service started (PID: 12345)

[INFO] Test 2: Testing local service directly...
[SUCCESS] ✓ Local test service responding

[INFO] Test 3: Creating tunnel via Management API...
[SUCCESS] ✓ Tunnel created: e2e-test
  Token: eyJhbGc...

[INFO] Cleaning up test resources...
[INFO] Deleting test tunnel: e2e-test
[SUCCESS] Test service stopped
[SUCCESS] Cleanup completed
=========================================
[SUCCESS] All tests passed! (3/3)
```

---

## Teardown

### Delete Test Environment

**Complete teardown** (removes everything including Key Vault):

```bash
./scripts/teardown-test-env.sh --env test
```

**What it deletes:**
1. Container App (`protogate-test-server`)
2. Container Apps Environment (`protogate-test-env`)
3. Key Vault (`protogate-test-kv`) - **soft-deleted, then purged**
4. Resource Group (`protogate-test-rg`) - includes everything else
5. Local cached files (`azure/.keyvault-uri-test`, etc.)

**Safety features:**
- Requires typing `DELETE` to confirm
- Shows what will be deleted before proceeding
- Use `--dry-run` to preview without deleting

**Expected output:**
```
[INFO] Step 1: Validating environment
[INFO] Step 2: Deleting Container App: protogate-test-server
[SUCCESS] Container App deleted
[INFO] Step 3: Deleting Container Apps environment
[SUCCESS] Container Apps environment deleted
[INFO] Step 4: Deleting Key Vault: protogate-test-kv
[SUCCESS] Key Vault deleted (soft-deleted, recoverable for 90 days)
[INFO] Step 5: Deleting resource group: protogate-test-rg
[SUCCESS] Resource group deletion initiated
[INFO] Step 6: Purging soft-deleted Key Vault
[SUCCESS] Key Vault purged: protogate-test-kv
[INFO] Step 7: Cleaning up local files
[SUCCESS] Cleanup completed
```

### Preview Without Deleting

```bash
# Dry-run mode (safe, no changes)
./scripts/teardown-test-env.sh --env test --dry-run
```

### Partial Teardown Options

```bash
# Keep resource group, delete individual resources
./scripts/teardown-test-env.sh --env test --keep-rg

# Keep DNS records (if configured)
./scripts/teardown-test-env.sh --env test --keep-dns
```

---

## Production Deployment

### Before Production

1. Review test environment configuration
2. Plan domain names (e.g., `tunnel.yourcompany.com`)
3. Decide on dedicated vs consumption Container Apps plan
4. Plan monitoring and alerts

### Deploy Production

```bash
# Deploy production environment
./scripts/deploy-test-env.sh --env prod

# Expected resources created:
# - Resource Group: protogate-prod-rg
# - Key Vault: protogate-prod-kv
# - Container Registry: protogateprodacr
# - Container App: protogate-prod-server
```

### Configure DNS (Optional - Phase 7)

```bash
# Configure custom domain with Let's Encrypt
./scripts/configure-dns.sh \
  --env prod \
  --zone yourcompany.com \
  --subdomain tunnel \
  --letsencrypt \
  --email admin@yourcompany.com

# This creates:
# - tunnel.yourcompany.com → Management API
# - *.tunnel.yourcompany.com → Individual tunnels
# - Let's Encrypt wildcard certificate
```

---

## Troubleshooting

### Deployment Failed

**Problem:** `ResourceGroupBeingDeleted` error

```bash
# Wait for deletion to complete
az group show --name protogate-test-rg --query "properties.provisioningState"

# Should return error "could not be found" when ready
```

**Problem:** `Key Vault already exists in deleted state`

```bash
# Purge soft-deleted Key Vault
az keyvault purge --name protogate-test-kv

# Wait 30 seconds, then retry deployment
./scripts/deploy-test-env.sh --env test
```

**Problem:** `Failed to login to ACR`

```bash
# Re-login to Azure
az login

# Verify subscription
az account show

# Try manual ACR login
az acr login --name protogatedevacr
```

### E2E Tests Failed

**Problem:** `Server URL file not found`

```bash
# Deploy environment first
./scripts/deploy-test-env.sh --env test

# Verify file exists
cat azure/.server-url-test
```

**Problem:** `Health check failed`

```bash
# Check container logs
az containerapp logs show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --tail 100

# Verify container is running
az containerapp show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --query "properties.runningStatus"
```

**Problem:** `Tunnel creation returns 409 Conflict`

```bash
# Delete existing tunnel first
curl -X DELETE $SERVER_URL/v1/tunnels/e2e-test

# Or run cleanup manually
./scripts/e2e-test.sh --env test --skip-cleanup
```

### View Logs

```bash
# Stream container logs
az containerapp logs show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --follow

# View recent errors
az containerapp logs show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --tail 100 | grep ERROR
```

### Check Resource Status

```bash
# List all resources in resource group
az resource list \
  --resource-group protogate-test-rg \
  --output table

# Check container app health
az containerapp show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --query "{Status:properties.runningStatus, Health:properties.health, Replicas:properties.template.scale}" \
  --output table
```

---

## Advanced Usage

### Custom Image Tag

```bash
# Deploy with specific image version
./scripts/deploy-test-env.sh --env test --image-tag v1.2.3
```

### Custom Location

```bash
# Deploy to different Azure region
./scripts/deploy-test-env.sh --env test --location eastus
```

### Specify Key Vault Name

```bash
# Use custom Key Vault name (default: protogate-{env}-kv)
./scripts/deploy-test-env.sh --env test --keyvault-name my-custom-kv
```

### Verbose Output

```bash
# Enable detailed logging
./scripts/deploy-test-env.sh --env test --verbose
```

---

## Next Steps

1. ✅ **Complete deployment** - Test environment working
2. ⏭️ **Configure DNS** - Custom domains (optional)
3. ⏭️ **Setup monitoring** - Application Insights and alerts
4. ⏭️ **Production deployment** - Deploy to prod environment
5. ⏭️ **Agent integration** - Deploy tunnel agents
6. ⏭️ **Documentation** - Update external docs with URLs

---

## Quick Reference

### Essential Commands

```bash
# Deploy test environment
./scripts/deploy-test-env.sh --env test

# Verify deployment
curl $(cat azure/.server-url-test)/health

# Run e2e tests
./scripts/e2e-test.sh --env test

# Teardown everything
./scripts/teardown-test-env.sh --env test
```

### Important Files

- `azure/.server-url-test` - Test server URL
- `azure/.keyvault-uri-test` - Key Vault URI
- `azure/.managed-identity-test` - Managed identity ID
- `scripts/deploy-test-env.sh` - Deployment script
- `scripts/teardown-test-env.sh` - Teardown script
- `scripts/e2e-test.sh` - End-to-end tests

### Azure Resources (Test Environment)

- Resource Group: `protogate-test-rg`
- Container App: `protogate-test-server`
- Container Environment: `protogate-test-env`
- Key Vault: `protogate-test-kv`
- Container Registry: `protogatetest acr`
- Log Analytics: `protogate-test-logs`

---

## Support

For issues or questions:

1. Check [Troubleshooting](#troubleshooting) section
2. View container logs: `az containerapp logs show ...`
3. Review deployment script: `./scripts/deploy-test-env.sh --dry-run`
4. Check Azure Portal for resource status

**Documentation:**
- Deployment scripts: `specs/001-azure-deployment-test/`
- Management API: `specs/002-management-api-http-proxy/`
- Feature status: `FEATURE_COMPLETE.md`

