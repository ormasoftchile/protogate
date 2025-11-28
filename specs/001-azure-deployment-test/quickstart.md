# Azure Deployment Test - Quickstart Guide

This guide walks through deploying protogate to Azure Container Apps with a complete test environment.

## Prerequisites

### Required Tools

- **Azure CLI** (>= 2.50.0)
  ```bash
  az --version
  # Install: https://aka.ms/azure-cli
  ```

- **Docker with buildx** (for multi-arch builds)
  ```bash
  docker buildx version
  # Update Docker Desktop to latest version
  ```

- **Python 3** (for test service)
  ```bash
  python3 --version
  ```

### Azure Setup

1. **Login to Azure**
   ```bash
   az login
   az account set --subscription <your-subscription-id>
   ```

2. **Login to Azure Container Registry**
   ```bash
   az acr login --name protogatedevacr
   ```

3. **Verify Access**
   ```bash
   az account show
   # Should show your subscription details
   ```

## Quick Start (3 Steps)

### Step 1: Build and Push Docker Images

Build multi-architecture Docker images and push to Azure Container Registry:

```bash
./scripts/build-and-push.sh v1.0.0 --latest
```

This will:
- Create a Docker buildx builder for multi-arch support
- Build images for linux/amd64 and linux/arm64
- Push images to protogatedevacr.azurecr.io
- Verify images and report sizes

**Expected output:**
```
[SUCCESS] Build and push completed successfully!
Images available at:
  - protogatedevacr.azurecr.io/protogate-server:v1.0.0
  - protogatedevacr.azurecr.io/protogate-server:latest
```

### Step 2: Provision Key Vault with TLS Certificates

Create Azure Key Vault and upload self-signed certificates for testing:

```bash
./scripts/provision-keyvault.sh --env test
```

This will:
- Create resource group: `protogate-test-rg`
- Generate self-signed TLS certificate (365 days)
- Create Key Vault with unique name
- Upload certificate and private key as secrets
- Configure access policies

**Expected output:**
```
[SUCCESS] Key Vault provisioning completed!
Key Vault: pg-test-kv-abc123
URI saved to: azure/.keyvault-uri-test
```

### Step 3: Deploy Test Environment

Deploy Azure Container Apps environment with protogate server:

```bash
./scripts/deploy-test-env.sh --env test
```

This will:
- Create Container Apps environment
- Deploy protogate server with latest image
- Enable managed identity
- Grant Key Vault access
- Configure health probes
- Verify server health

**Expected output:**
```
[SUCCESS] Deployment completed!
Server URL: https://protogate-test-server.salmonisland-abc123.westus2.azurecontainerapps.io
[SUCCESS] Server is healthy!
```

## Verify Deployment

### Check Server Health

```bash
# Load server URL
SERVER_URL=$(cat azure/.server-url-test)

# Health check
curl $SERVER_URL/health
# Expected: {"status":"healthy","version":"1.0.0"}
```

### Run E2E Tests

```bash
./scripts/e2e-test.sh --env test
```

This will:
- Start local test HTTP service
- Verify server health
- Test tunnel creation (if Management API is ready)
- Validate end-to-end flow

**Expected output:**
```
[SUCCESS] All tests passed! (3/3)
```

## Script Reference

### Build and Push (`scripts/build-and-push.sh`)

Build and push multi-arch Docker images.

```bash
# Basic usage
./scripts/build-and-push.sh v1.0.0

# With latest tag
./scripts/build-and-push.sh v1.0.0 --latest

# Dry run (show what would happen)
./scripts/build-and-push.sh v1.0.0 --dry-run

# Verbose output
./scripts/build-and-push.sh v1.0.0 --verbose
```

### Provision Key Vault (`scripts/provision-keyvault.sh`)

Create Key Vault and upload certificates.

```bash
# Test environment
./scripts/provision-keyvault.sh --env test

# Production environment
./scripts/provision-keyvault.sh --env prod

# With custom certificate
./scripts/provision-keyvault.sh --env test \
  --cert ./my-cert.pem \
  --key ./my-key.pem

# Different region
./scripts/provision-keyvault.sh --env test --location eastus
```

### Deploy Environment (`scripts/deploy-test-env.sh`)

Deploy Container Apps environment.

```bash
# Test environment with latest image
./scripts/deploy-test-env.sh --env test

# Specific image tag
./scripts/deploy-test-env.sh --env test --image-tag v1.0.0

# Production environment
./scripts/deploy-test-env.sh --env prod --image-tag v1.0.0
```

### E2E Tests (`scripts/e2e-test.sh`)

Run end-to-end tests.

```bash
# Basic test
./scripts/e2e-test.sh --env test

# Verbose output
./scripts/e2e-test.sh --env test --verbose

# Keep test resources (no cleanup)
./scripts/e2e-test.sh --env test --skip-cleanup
```

## Troubleshooting

### Build Issues

**Problem:** `docker buildx not available`
```bash
# Solution: Update Docker Desktop or install buildx
docker buildx install
```

**Problem:** `Failed to login to ACR`
```bash
# Solution: Check Azure login and subscription
az login
az account show
az acr login --name protogatedevacr
```

### Deployment Issues

**Problem:** `Resource group not found`
```bash
# Solution: Run provision-keyvault.sh first
./scripts/provision-keyvault.sh --env test
```

**Problem:** `Health check timed out`
```bash
# Solution: Check container logs
az containerapp logs show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --tail 100
```

**Problem:** `Image pull failed`
```bash
# Solution: Verify image exists in ACR
az acr repository show-tags \
  --name protogatedevacr \
  --repository protogate-server
```

### View Resources

```bash
# List resource groups
az group list --query "[?starts_with(name, 'protogate')]" -o table

# List container apps
az containerapp list \
  --resource-group protogate-test-rg \
  -o table

# Get container app details
az containerapp show \
  --name protogate-test-server \
  --resource-group protogate-test-rg

# View logs
az containerapp logs show \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --follow
```

## Cleanup

### Delete Test Environment

```bash
# Delete entire resource group (WARNING: destructive)
az group delete --name protogate-test-rg --yes --no-wait

# Delete specific container app only
az containerapp delete \
  --name protogate-test-server \
  --resource-group protogate-test-rg \
  --yes
```

### Delete Images from ACR

```bash
# Delete specific version
az acr repository delete \
  --name protogatedevacr \
  --image protogate-server:v1.0.0 \
  --yes

# List all tags
az acr repository show-tags \
  --name protogatedevacr \
  --repository protogate-server \
  --output table
```

## Next Steps

1. **Setup DNS** - Configure custom domain for tunnels
   ```bash
   ./scripts/configure-dns.sh --env test --zone tunnel.ormasoft.cl --letsencrypt
   ```

2. **Enable Monitoring** - Add Application Insights and alerts
   ```bash
   ./scripts/setup-monitoring.sh --env test
   ```

3. **Production Deployment** - Deploy to production environment
   ```bash
   ./scripts/build-and-push.sh v1.0.0
   ./scripts/provision-keyvault.sh --env prod
   ./scripts/deploy-test-env.sh --env prod --image-tag v1.0.0
   ```

## Cost Estimates

See [spec.md](./spec.md) for detailed cost projections.

**Test Environment:**
- Container Apps: ~$10/month
- Key Vault: ~$1/month
- **Total: ~$16/month** (scales to zero when idle)

## Additional Resources

- [Azure Container Apps Documentation](https://learn.microsoft.com/azure/container-apps/)
- [Azure Key Vault Documentation](https://learn.microsoft.com/azure/key-vault/)
- [Docker Buildx Documentation](https://docs.docker.com/buildx/)
- [Spec: 001-azure-deployment-test](./spec.md)
- [Tasks: Implementation checklist](./tasks.md)
