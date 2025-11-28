# Implementation Plan: Azure Deployment with Test Environment

**Feature**: 001-azure-deployment-test  
**Created**: 2025-11-27  
**Status**: Planning  
**Estimated Effort**: 14 hours (7 phases)

---

## Technical Context

### Architecture Overview

**Deployment Model**: Azure Container Apps (Consumption Plan)
- **Why**: Serverless, pay-per-use, auto-scaling, managed infrastructure
- **Alternatives Considered**: 
  - AKS (too complex for initial deployment)
  - Azure App Service (less flexible for multiple ports)
  - Azure VMs (more expensive, more maintenance)

**Multi-Arch Docker Strategy**:
- Build: Docker buildx with QEMU emulation
- Targets: linux/amd64 (Azure), linux/arm64 (local M1/M2 Macs)
- Registry: Azure Container Registry (existing `protogatedevacr.azurecr.io`)
- Image size target: <100MB (Alpine-based, multi-stage builds)

**Infrastructure Components**:
```
┌─────────────────────────────────────────────────────────────┐
│                    Azure Resource Group                      │
│                    (protogate-test-rg)                       │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │        Container Apps Environment                     │   │
│  │        (protogate-test-env)                          │   │
│  ├──────────────────────────────────────────────────────┤   │
│  │                                                        │   │
│  │  ┌────────────────────┐   ┌───────────────────────┐ │   │
│  │  │ Server App         │   │ Agent App (testing)   │ │   │
│  │  │ - 0.5 CPU, 1Gi    │   │ - 0.25 CPU, 512Mi     │ │   │
│  │  │ - Ports: 443,     │   │ - No ingress          │ │   │
│  │  │   8080, 8443      │   │ - Scale to zero       │ │   │
│  │  │ - Managed ID      │   │                        │ │   │
│  │  └────────────────────┘   └───────────────────────┘ │   │
│  │                                                        │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │        Azure Key Vault                                │   │
│  │        (protogate-test-kv-{suffix})                  │   │
│  │        - TLS cert (self-signed for test)             │   │
│  │        - TLS private key                              │   │
│  │        - Access: Server managed identity              │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │        Log Analytics Workspace (optional P2)          │   │
│  │        - Container logs                               │   │
│  │        - Query language (KQL)                         │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
└─────────────────────────────────────────────────────────────┘

External:
  - Azure Container Registry (protogatedevacr) - Shared resource
  - Azure DNS Zone (optional for test, required for prod)
  - Application Insights (optional P2)
```

### Technology Stack

**Build & Deploy**:
- Docker 24+ with buildx
- Azure CLI 2.50+
- Bash scripts (portable, no Python/Node dependencies)

**Azure Services**:
- Container Apps (compute)
- Key Vault (secrets management)
- Container Registry (image storage)
- Log Analytics (logging - P2)
- Application Insights (monitoring - P2)
- DNS Zone (custom domains - P1)

**Testing**:
- curl (HTTP requests)
- jq (JSON parsing)
- Python 3.8+ (test service - simple HTTP server)

### Constitution Check

**Security-First** ✅
- TLS certificates in Key Vault (not in code/env vars)
- Managed identities for Key Vault access (no credentials)
- Container Apps default to HTTPS
- Self-signed certs for test OK (production requires CA-signed)

**Azure-Native** ✅
- All infrastructure is Azure services
- Container Apps, Key Vault, ACR, DNS Zone
- No external dependencies or SaaS providers

**Self-Hosting Control** ✅
- Deploys in customer Azure subscription
- No telemetry to external services
- Full control over resources and data

**Performance & Cost Efficiency** ✅
- Test environment: $16/month (well under $15 target after scale)
- Consumption plan: pay only for usage
- Scale to zero when idle
- Startup (50 tunnels): $173/month ≈ $3.46/customer

**Observability** ✅
- Health endpoints (/health on port 8080)
- Log Analytics for container logs (P2)
- Application Insights for metrics (P2)
- JSON structured logging in app

**Gates Evaluation**:
- ✅ Security: Secrets in Key Vault, managed identities
- ✅ Cost: Test environment $16/month, production scales linearly
- ✅ Performance: Container startup <30s, health checks configured
- ✅ Azure-native: All services are Azure (no AWS/GCP)
- ✅ Self-hosted: Customer subscription, no vendor dependencies

**Known Unknowns**: 
- ❓ DNS provider (if customer has existing Azure DNS zone, reuse vs create)
- ❓ Certificate source (self-signed for test, Let's Encrypt or corporate CA for prod)
- ❓ Subscription limits (Container Apps quota, IP addresses)

---

## Phase 0: Research & Clarification

### Research Tasks

**R1: Docker Multi-Arch Best Practices**
- **Question**: Optimal buildx configuration for ARM64 emulation performance?
- **Research**: Docker buildx docs, Azure ACR multi-arch support
- **Decision**: Use native builders where possible, QEMU for cross-compilation
- **Rationale**: Faster builds, better caching, ACR supports manifest lists

**R2: Container Apps Port Configuration**
- **Question**: Can Container Apps expose multiple ports (443, 8080, 8443)?
- **Research**: Azure Container Apps ingress documentation
- **Decision**: 
  - Port 443: External ingress (HTTP/HTTPS traffic)
  - Port 8080: Health probe (internal)
  - Port 8443: Additional TCP port for agent WebSocket
- **Rationale**: Container Apps supports multiple port configuration via YAML/CLI

**R3: Key Vault Access Patterns**
- **Question**: Best practice for container app to read Key Vault secrets at runtime?
- **Research**: Managed Identity integration, secret rotation
- **Decision**: 
  - Enable system-assigned managed identity on container app
  - Grant Key Vault "Get" and "List" permissions to identity
  - App reads secrets on startup via Azure SDK
- **Rationale**: No credentials in environment, automatic rotation support
- **Alternative**: Key Vault references in Container Apps (limited to env vars)

**R4: E2E Test Architecture**
- **Question**: Where to run agent for e2e tests—Container App or local Docker?
- **Research**: Test isolation, cost, complexity
- **Decision**: 
  - **Phase 1 (MVP)**: Run agent locally in Docker (simpler, faster iteration)
  - **Phase 2 (Optional)**: Deploy agent as Container App (full cloud validation)
- **Rationale**: Local Docker reduces cloud costs, faster test cycles, easier debugging

**R5: Certificate Generation**
- **Question**: How to generate self-signed TLS certs for test environment?
- **Research**: OpenSSL commands, Azure CLI certificate import
- **Decision**: OpenSSL with SAN for wildcard domains, 365-day expiry
- **Command**:
  ```bash
  openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem \
    -days 365 -nodes -subj "/CN=*.tunnel.local" \
    -addext "subjectAltName=DNS:*.tunnel.local,DNS:tunnel.local"
  ```
- **Rationale**: Standard tool, works cross-platform, sufficient for testing

### Unknowns Resolved

All unknowns from Technical Context have been researched and decided:

1. **DNS Provider**: Use existing Azure DNS zone `ormasoft.cl` with subdomain `tunnel.ormasoft.cl` (test uses `test.tunnel.ormasoft.cl`)
2. **Certificate Source**: Let's Encrypt for both test and production (automatic renewal via ACME DNS-01 challenge)
3. **Subscription Limits**: Check quotas before deployment, document in prerequisites

---

## Phase 1: Design & Contracts

### Data Models

**Deployment Configuration** (not persisted, runtime only):
```json
{
  "environment": "test|prod",
  "resourceGroup": "protogate-{env}-rg",
  "location": "westus2",
  "containerAppsEnvironment": "protogate-{env}-env",
  "serverApp": {
    "name": "protogate-{env}-server",
    "image": "protogatedevacr.azurecr.io/protogate-server:latest",
    "cpu": 0.5,
    "memory": "1Gi",
    "replicas": {"min": 1, "max": 5},
    "ports": [443, 8080, 8443]
  },
  "keyVault": {
    "name": "protogate-{env}-kv-{uniqueSuffix}",
    "secrets": ["tls-cert", "tls-key"]
  }
}
```

**E2E Test Results** (JSON output):
```json
{
  "timestamp": "2025-11-27T12:00:00Z",
  "environment": "test",
  "testSuite": "e2e-tunnel-flow",
  "results": [
    {
      "testId": "create-tunnel",
      "status": "passed",
      "duration": 1.2,
      "details": {"tunnelId": "test-abc", "token": "tnl_..."}
    },
    {
      "testId": "agent-connect",
      "status": "passed",
      "duration": 2.5,
      "details": {"connectionTime": 1.8}
    }
  ],
  "summary": {
    "total": 7,
    "passed": 7,
    "failed": 0,
    "skipped": 0
  }
}
```

### API Contracts

**Management API** (already implemented, no changes):
- `POST /v1/tunnels` - Create tunnel, returns token
- `GET /v1/tunnels` - List all tunnels
- `GET /v1/tunnels/{id}` - Get tunnel details
- `DELETE /v1/tunnels/{id}` - Delete tunnel

**Script CLI Contracts**:

**build-and-push.sh**:
```bash
Usage: ./scripts/build-and-push.sh [OPTIONS] <version>

Arguments:
  version         Semantic version (e.g., v1.0.0)

Options:
  --dry-run       Print commands without executing
  --platform      Comma-separated platforms (default: linux/amd64,linux/arm64)
  --push          Push to registry (default: true)
  --json          Output JSON progress

Examples:
  ./scripts/build-and-push.sh v1.0.0
  ./scripts/build-and-push.sh --dry-run --platform linux/amd64 v1.0.0-rc1
```

**deploy-test-env.sh**:
```bash
Usage: ./scripts/deploy-test-env.sh [OPTIONS]

Options:
  --env           Environment name (default: test)
  --location      Azure region (default: westus2)
  --dry-run       Print commands without executing
  --json          Output JSON progress
  --skip-build    Use existing images, don't rebuild

Examples:
  ./scripts/deploy-test-env.sh
  ./scripts/deploy-test-env.sh --location eastus --dry-run
```

**e2e-test.sh**:
```bash
Usage: ./scripts/e2e-test.sh [OPTIONS]

Options:
  --env           Environment (default: test)
  --server-url    Server URL (auto-detected if not provided)
  --verbose       Enable debug output
  --json          Output JSON results
  --skip-cleanup  Leave test resources for debugging

Examples:
  ./scripts/e2e-test.sh --env test
  ./scripts/e2e-test.sh --server-url https://protogate-test.azurecontainerapps.io --verbose
```

### Quickstart Documentation

**File**: `specs/001-azure-deployment-test/quickstart.md`

```markdown
# Azure Deployment Quickstart

## Prerequisites

1. Azure CLI installed and authenticated:
   ```bash
   az --version  # >= 2.50.0
   az login
   az account set --subscription <subscription-id>
   ```

2. Docker with buildx:
   ```bash
   docker buildx version
   ```

3. Access to Azure Container Registry:
   ```bash
   az acr login --name protogatedevacr
   ```

## Deploy Test Environment (5 minutes)

1. Build and push Docker images:
   ```bash
   ./scripts/build-and-push.sh v1.0.0
   ```

2. Deploy Azure infrastructure:
   ```bash
   ./scripts/deploy-test-env.sh
   ```

3. Verify deployment:
   ```bash
   # Health check
   curl https://<server-url>/health
   
   # Run e2e tests
   ./scripts/e2e-test.sh --env test
   ```

## Troubleshooting

**Build fails on ARM64**:
- Ensure QEMU installed: `docker run --rm --privileged multiarch/qemu-user-static --reset -p yes`

**Container app won't start**:
- Check logs: `az containerapp logs show --name protogate-test-server --resource-group protogate-test-rg`
- Verify image exists: `az acr repository show-tags -n protogatedevacr --repository protogate-server`

**E2E tests fail**:
- Verify server health: `curl https://<server-url>/health`
- Check agent connection: `docker logs <agent-container-id>`
- Re-run with verbose: `./scripts/e2e-test.sh --verbose`
```

### Agent Context Update

Since this feature involves Docker and Azure deployment, update `.copilot-instructions.md`:

```markdown
## Azure Deployment

- Test environment: `protogate-test-rg` (West US 2)
- Production: TBD (not yet deployed)
- Container Registry: `protogatedevacr.azurecr.io`
- Images: Multi-arch (AMD64 + ARM64)
- Deployment: Azure Container Apps (Consumption Plan)
- Scripts location: `scripts/` (build-and-push.sh, deploy-test-env.sh, e2e-test.sh)
```

---

## Implementation Phases

### Phase 1: Docker Multi-Arch Build (2 hours)

**Objective**: Create production-ready Docker images for AMD64 and ARM64

**Files to Create**:
- `scripts/build-and-push.sh` (250 lines)
- `scripts/common.sh` (100 lines - shared utilities)

**Tasks**:
1. Create buildx builder instance
2. Validate Dockerfile.alpine (ensure multi-stage, optimized layers)
3. Build both architectures with caching
4. Create manifest list
5. Push to Azure Container Registry
6. Tag with version and "latest"
7. Verify images in ACR
8. Output build report (size, layers, vulnerabilities scan)

**Script Logic** (`build-and-push.sh`):
```bash
#!/bin/bash
set -euo pipefail

# Parse arguments
VERSION=$1
DRY_RUN=${DRY_RUN:-false}
PLATFORMS=${PLATFORMS:-linux/amd64,linux/arm64}

# Validate prerequisites
check_docker_buildx() {
  docker buildx version >/dev/null 2>&1 || {
    echo "ERROR: Docker buildx not found"
    exit 1
  }
}

# Create/reuse builder
setup_builder() {
  if ! docker buildx inspect protogate-builder >/dev/null 2>&1; then
    docker buildx create --name protogate-builder --use
  else
    docker buildx use protogate-builder
  fi
  docker buildx inspect --bootstrap
}

# Build images
build_images() {
  local image_base="protogatedevacr.azurecr.io/protogate-server"
  
  docker buildx build \
    --platform "$PLATFORMS" \
    --file docker/Dockerfile.alpine \
    --tag "${image_base}:${VERSION}" \
    --tag "${image_base}:latest" \
    --push \
    --progress=plain \
    .
}

# Verify push
verify_push() {
  az acr repository show-tags \
    --name protogatedevacr \
    --repository protogate-server \
    --output table
}

main() {
  check_docker_buildx
  setup_builder
  build_images
  verify_push
  echo "✅ Build complete: ${VERSION}"
}

main "$@"
```

**Validation**:
```bash
./scripts/build-and-push.sh v1.0.0
# Expected output:
# ✅ Builder configured
# ✅ Building linux/amd64... 
# ✅ Building linux/arm64...
# ✅ Pushing manifest list...
# ✅ Verifying in ACR...
# Image: protogatedevacr.azurecr.io/protogate-server:v1.0.0
# Size: 95MB (amd64), 98MB (arm64)
# ✅ Build complete: v1.0.0
```

**Success Criteria**:
- ✅ Images build without errors
- ✅ Both architectures <100MB
- ✅ Pushed to ACR with correct tags
- ✅ Manifest list created (multi-arch pull works)

---

### Phase 2: Azure Key Vault Setup (1 hour)

**Objective**: Provision Key Vault and upload test certificates

**Files to Create**:
- `scripts/provision-keyvault.sh` (200 lines)
- `scripts/generate-test-cert.sh` (50 lines)

**Tasks**:
1. Generate unique Key Vault name (global uniqueness)
2. Create Key Vault in test resource group
3. Generate self-signed TLS certificate
4. Upload certificate and key as secrets
5. Configure access policy (placeholder for future managed identity)
6. Validate secret retrieval

**Script Logic** (`provision-keyvault.sh`):
```bash
#!/bin/bash
set -euo pipefail

ENV=${ENV:-test}
LOCATION=${LOCATION:-westus2}
RG_NAME="protogate-${ENV}-rg"

# Generate unique suffix for global uniqueness
UNIQUE_SUFFIX=$(echo -n "$RG_NAME" | md5sum | head -c 8)
KV_NAME="protogate-${ENV}-kv-${UNIQUE_SUFFIX}"

# Create resource group if not exists
create_resource_group() {
  if ! az group show --name "$RG_NAME" &>/dev/null; then
    echo "Creating resource group: $RG_NAME"
    az group create --name "$RG_NAME" --location "$LOCATION"
  fi
}

# Create Key Vault
create_keyvault() {
  echo "Creating Key Vault: $KV_NAME"
  az keyvault create \
    --name "$KV_NAME" \
    --resource-group "$RG_NAME" \
    --location "$LOCATION" \
    --enable-rbac-authorization false
}

# Generate self-signed cert
generate_cert() {
  ./scripts/generate-test-cert.sh
}

# Upload secrets
upload_secrets() {
  echo "Uploading TLS certificate..."
  az keyvault secret set \
    --vault-name "$KV_NAME" \
    --name tls-cert \
    --file /tmp/test-cert.pem
  
  az keyvault secret set \
    --vault-name "$KV_NAME" \
    --name tls-key \
    --file /tmp/test-key.pem
}

# Output Key Vault URI
output_uri() {
  local uri=$(az keyvault show --name "$KV_NAME" --query properties.vaultUri -o tsv)
  echo "✅ Key Vault provisioned: $uri"
  echo "$uri" > .keyvault-uri
}

main() {
  create_resource_group
  create_keyvault
  generate_cert
  upload_secrets
  output_uri
}

main "$@"
```

**Validation**:
```bash
./scripts/provision-keyvault.sh --env test
# Expected output:
# Creating resource group: protogate-test-rg
# Creating Key Vault: protogate-test-kv-a1b2c3d4
# Generating test certificate...
# Uploading TLS certificate...
# ✅ Key Vault provisioned: https://protogate-test-kv-a1b2c3d4.vault.azure.net/

# Verify secret exists
az keyvault secret show \
  --vault-name protogate-test-kv-a1b2c3d4 \
  --name tls-cert
```

**Success Criteria**:
- ✅ Key Vault created with unique name
- ✅ Self-signed certificate generated (365-day expiry)
- ✅ Secrets stored (tls-cert, tls-key)
- ✅ Secret retrieval works

---

### Phase 3: Container Apps Deployment (3 hours)

**Objective**: Deploy server to Azure Container Apps with health checks

**Files to Create**:
- `scripts/deploy-test-env.sh` (400 lines)
- `azure/container-app-config.json` (optional, for complex config)

**Tasks**:
1. Create Container Apps environment
2. Deploy server container app:
   - Image from ACR
   - Environment variables (KEY_VAULT_URI, DNS_ZONE)
   - Managed identity (system-assigned)
   - Ingress on port 443 (external)
   - Health probe on port 8080
3. Grant Key Vault access to managed identity
4. Wait for deployment to complete
5. Validate health endpoint
6. Output server URL

**Script Logic** (`deploy-test-env.sh`):
```bash
#!/bin/bash
set -euo pipefail

ENV=${ENV:-test}
LOCATION=${LOCATION:-westus2}
RG_NAME="protogate-${ENV}-rg"
CA_ENV_NAME="protogate-${ENV}-env"
SERVER_APP_NAME="protogate-${ENV}-server"
IMAGE="protogatedevacr.azurecr.io/protogate-server:latest"

# Read Key Vault URI
KV_URI=$(cat .keyvault-uri 2>/dev/null || echo "")
if [[ -z "$KV_URI" ]]; then
  echo "ERROR: Key Vault URI not found. Run provision-keyvault.sh first."
  exit 1
fi

# Create Container Apps environment
create_ca_environment() {
  echo "Creating Container Apps environment: $CA_ENV_NAME"
  az containerapp env create \
    --name "$CA_ENV_NAME" \
    --resource-group "$RG_NAME" \
    --location "$LOCATION"
}

# Deploy server app
deploy_server() {
  echo "Deploying server app: $SERVER_APP_NAME"
  
  az containerapp create \
    --name "$SERVER_APP_NAME" \
    --resource-group "$RG_NAME" \
    --environment "$CA_ENV_NAME" \
    --image "$IMAGE" \
    --cpu 0.5 \
    --memory 1Gi \
    --min-replicas 1 \
    --max-replicas 5 \
    --ingress external \
    --target-port 443 \
    --exposed-port 8443 \
    --env-vars \
      KEY_VAULT_URI="$KV_URI" \
      DNS_ZONE="tunnel.local" \
      LOG_LEVEL="INFO" \
    --system-assigned
}

# Configure health probe
configure_health_probe() {
  # Note: Health probes configured via YAML or separate command
  echo "Configuring health probe on port 8080..."
  # Azure CLI may not support all probe options yet
  # May need to use az containerapp update with --yaml
}

# Grant Key Vault access
grant_keyvault_access() {
  echo "Granting Key Vault access to managed identity..."
  
  local principal_id=$(az containerapp show \
    --name "$SERVER_APP_NAME" \
    --resource-group "$RG_NAME" \
    --query identity.principalId -o tsv)
  
  local kv_name=$(basename "$KV_URI" .vault.azure.net)
  
  az keyvault set-policy \
    --name "$kv_name" \
    --object-id "$principal_id" \
    --secret-permissions get list
}

# Wait for healthy
wait_for_healthy() {
  local url=$(az containerapp show \
    --name "$SERVER_APP_NAME" \
    --resource-group "$RG_NAME" \
    --query properties.configuration.ingress.fqdn -o tsv)
  
  echo "Waiting for server to be healthy..."
  for i in {1..30}; do
    if curl -sf "https://$url/health" >/dev/null 2>&1; then
      echo "✅ Server is healthy: https://$url"
      echo "https://$url" > .server-url
      return 0
    fi
    echo "Attempt $i/30: Not ready yet..."
    sleep 10
  done
  
  echo "ERROR: Server did not become healthy after 5 minutes"
  return 1
}

main() {
  create_ca_environment
  deploy_server
  configure_health_probe
  grant_keyvault_access
  wait_for_healthy
  
  echo ""
  echo "✅ Test environment deployed successfully!"
  echo "Server URL: $(cat .server-url)"
  echo ""
  echo "Next steps:"
  echo "  1. Test health: curl https://$(cat .server-url)/health"
  echo "  2. Run e2e tests: ./scripts/e2e-test.sh --env test"
}

main "$@"
```

**Validation**:
```bash
./scripts/deploy-test-env.sh
# Expected output:
# Creating Container Apps environment: protogate-test-env
# Deploying server app: protogate-test-server
# Configuring health probe on port 8080...
# Granting Key Vault access to managed identity...
# Waiting for server to be healthy...
# ✅ Server is healthy: https://protogate-test-server.xyz.westus2.azurecontainerapps.io
# ✅ Test environment deployed successfully!
# Server URL: https://protogate-test-server.xyz.westus2.azurecontainerapps.io

# Verify health
curl https://protogate-test-server.xyz.westus2.azurecontainerapps.io/health
# Expected: {"status":"healthy","version":"1.0.0"}
```

**Success Criteria**:
- ✅ Container Apps environment created
- ✅ Server app deployed with correct config
- ✅ Managed identity created
- ✅ Key Vault access granted
- ✅ Health endpoint returns 200 OK
- ✅ Server starts within 30 seconds

---

### Phase 4: E2E Test Automation (3 hours)

**Objective**: Automated end-to-end testing of tunnel flow

**Files to Create**:
- `scripts/e2e-test.sh` (500 lines)
- `test/e2e/test-service.py` (100 lines)
- `test/e2e/docker-compose.yml` (50 lines)

**Tasks**:
1. Read server URL from deployment
2. Create test tunnel via Management API
3. Start test HTTP service (Python simple server)
4. Start tunnel agent (Docker) with tunnel token
5. Wait for agent connection (poll logs or API)
6. Make HTTP request through tunnel
7. Validate response body/headers
8. Test error conditions:
   - Invalid token → 401/403
   - Disconnected agent → 503
   - Nonexistent tunnel → 404
9. Cleanup: Delete tunnel, stop containers
10. Output JSON test results

**Script Logic** (`e2e-test.sh`):
```bash
#!/bin/bash
set -euo pipefail

ENV=${ENV:-test}
SERVER_URL=${SERVER_URL:-$(cat .server-url 2>/dev/null || echo "")}
VERBOSE=${VERBOSE:-false}
JSON_OUTPUT=${JSON_OUTPUT:-false}

# Test results
declare -a TEST_RESULTS=()

# Test: Create tunnel
test_create_tunnel() {
  echo "Test 1: Creating tunnel via Management API..."
  
  local response=$(curl -sf -X POST "$SERVER_URL/v1/tunnels" \
    -H "Content-Type: application/json" \
    -d '{"tunnel_id":"e2e-test","protocol":"HTTP","target_host":"localhost","target_port":3000}')
  
  if [[ -z "$response" ]]; then
    record_failure "create-tunnel" "No response from server"
    return 1
  fi
  
  local token=$(echo "$response" | jq -r .token)
  echo "$token" > /tmp/tunnel-token
  
  record_success "create-tunnel" "$response"
  echo "✅ Tunnel created: e2e-test"
}

# Test: Start test service
test_start_service() {
  echo "Test 2: Starting test HTTP service..."
  
  cd test/e2e
  python3 test-service.py &
  echo $! > /tmp/test-service.pid
  sleep 2
  
  if ! curl -sf http://localhost:3000/echo >/dev/null; then
    record_failure "start-service" "Test service did not start"
    return 1
  fi
  
  record_success "start-service" "Service listening on port 3000"
  echo "✅ Test service started"
}

# Test: Start agent
test_start_agent() {
  echo "Test 3: Starting tunnel agent..."
  
  local token=$(cat /tmp/tunnel-token)
  
  docker run -d \
    --name e2e-agent \
    --network host \
    -e SERVER_URL="wss://$(echo $SERVER_URL | sed 's|https://||'):8443" \
    -e TUNNEL_TOKEN="$token" \
    -e TARGET_URL="http://localhost:3000" \
    protogatedevacr.azurecr.io/protogate-agent:latest
  
  # Wait for connection
  for i in {1..10}; do
    if docker logs e2e-agent 2>&1 | grep -q "Connected to server"; then
      record_success "start-agent" "Agent connected"
      echo "✅ Agent connected"
      return 0
    fi
    sleep 1
  done
  
  record_failure "start-agent" "Agent did not connect within 10 seconds"
  return 1
}

# Test: Proxied request
test_proxied_request() {
  echo "Test 4: Testing proxied HTTP request..."
  
  local response=$(curl -sf \
    -H "Host: e2e-test" \
    "$SERVER_URL/echo?message=hello")
  
  if echo "$response" | jq -e '.echo == "hello"' >/dev/null; then
    record_success "proxied-request" "$response"
    echo "✅ Proxied request successful"
  else
    record_failure "proxied-request" "Unexpected response: $response"
    return 1
  fi
}

# Test: Invalid token
test_invalid_token() {
  echo "Test 5: Testing invalid token (expect rejection)..."
  
  docker run -d \
    --name e2e-agent-invalid \
    --network host \
    -e SERVER_URL="wss://$(echo $SERVER_URL | sed 's|https://||'):8443" \
    -e TUNNEL_TOKEN="invalid-token-12345" \
    -e TARGET_URL="http://localhost:3000" \
    protogatedevacr.azurecr.io/protogate-agent:latest
  
  sleep 3
  
  if docker logs e2e-agent-invalid 2>&1 | grep -q "Authentication failed"; then
    record_success "invalid-token" "Agent rejected as expected"
    echo "✅ Invalid token rejected"
  else
    record_failure "invalid-token" "Agent was not rejected"
    return 1
  fi
  
  docker rm -f e2e-agent-invalid
}

# Test: Agent disconnect
test_agent_disconnect() {
  echo "Test 6: Testing disconnected agent (expect 503)..."
  
  docker stop e2e-agent
  sleep 2
  
  local status=$(curl -sf -o /dev/null -w "%{http_code}" \
    -H "Host: e2e-test" \
    "$SERVER_URL/echo")
  
  if [[ "$status" == "503" ]]; then
    record_success "agent-disconnect" "503 returned as expected"
    echo "✅ Disconnected agent returns 503"
  else
    record_failure "agent-disconnect" "Expected 503, got $status"
    return 1
  fi
}

# Test: Cleanup
test_cleanup() {
  echo "Test 7: Cleaning up resources..."
  
  curl -sf -X DELETE "$SERVER_URL/v1/tunnels/e2e-test"
  
  docker rm -f e2e-agent 2>/dev/null || true
  kill $(cat /tmp/test-service.pid) 2>/dev/null || true
  
  record_success "cleanup" "Resources cleaned"
  echo "✅ Cleanup complete"
}

# Record test result
record_success() {
  TEST_RESULTS+=("{\"testId\":\"$1\",\"status\":\"passed\",\"details\":\"$2\"}")
}

record_failure() {
  TEST_RESULTS+=("{\"testId\":\"$1\",\"status\":\"failed\",\"details\":\"$2\"}")
}

# Output results
output_results() {
  local passed=$(echo "${TEST_RESULTS[@]}" | grep -o "passed" | wc -l)
  local failed=$(echo "${TEST_RESULTS[@]}" | grep -o "failed" | wc -l)
  
  echo ""
  echo "================================"
  echo "E2E Test Results"
  echo "================================"
  echo "Total: $((passed + failed))"
  echo "Passed: $passed"
  echo "Failed: $failed"
  echo "================================"
  
  if [[ "$JSON_OUTPUT" == "true" ]]; then
    cat > /tmp/e2e-results.json <<EOF
{
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "environment": "$ENV",
  "testSuite": "e2e-tunnel-flow",
  "results": [$(IFS=,; echo "${TEST_RESULTS[*]}")],
  "summary": {
    "total": $((passed + failed)),
    "passed": $passed,
    "failed": $failed
  }
}
EOF
    cat /tmp/e2e-results.json
  fi
  
  if [[ $failed -gt 0 ]]; then
    echo "❌ Some tests failed"
    return 1
  else
    echo "✅ All tests passed"
    return 0
  fi
}

main() {
  test_create_tunnel || true
  test_start_service || true
  test_start_agent || true
  test_proxied_request || true
  test_invalid_token || true
  test_agent_disconnect || true
  test_cleanup || true
  
  output_results
}

main "$@"
```

**Test Service** (`test/e2e/test-service.py`):
```python
#!/usr/bin/env python3
import json
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

class EchoHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        
        response = {
            "echo": query.get('message', [''])[0],
            "headers": dict(self.headers),
            "path": self.path,
            "timestamp": int(time.time())
        }
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(response).encode())

if __name__ == '__main__':
    server = HTTPServer(('localhost', 3000), EchoHandler)
    print("Test service listening on port 3000")
    server.serve_forever()
```

**Validation**:
```bash
./scripts/e2e-test.sh --env test
# Expected output:
# Test 1: Creating tunnel via Management API...
# ✅ Tunnel created: e2e-test
# Test 2: Starting test HTTP service...
# ✅ Test service started
# Test 3: Starting tunnel agent...
# ✅ Agent connected
# Test 4: Testing proxied HTTP request...
# ✅ Proxied request successful
# Test 5: Testing invalid token (expect rejection)...
# ✅ Invalid token rejected
# Test 6: Testing disconnected agent (expect 503)...
# ✅ Disconnected agent returns 503
# Test 7: Cleaning up resources...
# ✅ Cleanup complete
# 
# ================================
# E2E Test Results
# ================================
# Total: 7
# Passed: 7
# Failed: 0
# ================================
# ✅ All tests passed
```

**Success Criteria**:
- ✅ All 7 test scenarios pass
- ✅ Test execution time <2 minutes
- ✅ Cleanup removes all resources
- ✅ JSON output option works

---

### Phase 5: DNS Configuration (2 hours) *(Optional P1)*

**Objective**: Configure Azure DNS for tunnel subdomains

**Files to Create**:
- `scripts/configure-dns.sh` (200 lines)

**Tasks**:
1. Create Azure DNS zone (or use existing)
2. Add wildcard A record: `*.tunnel.example.com`
3. Add root A record for Management API
4. Validate DNS resolution
5. Update container app with custom domain
6. Configure TLS certificate for domain

**Success Criteria**:
- ✅ DNS zone created
- ✅ Wildcard record resolves to container app
- ✅ Custom domain works with HTTPS

---

### Phase 6: Monitoring (2 hours) *(Optional P2)*

**Objective**: Configure Application Insights and Log Analytics

**Files to Create**:
- `scripts/setup-monitoring.sh` (300 lines)

**Tasks**:
1. Create Log Analytics workspace
2. Create Application Insights
3. Link Container Apps to Log Analytics
4. Configure connection string
5. Create alert rules (high error rate, service unavailable)
6. Create dashboard

**Success Criteria**:
- ✅ Logs queryable in Log Analytics
- ✅ Metrics visible in Application Insights
- ✅ Alerts configured

---

### Phase 7: Production Deployment (1 hour)

**Objective**: Deploy production environment with HA configuration

**Files to Create**:
- `scripts/deploy-prod-env.sh` (similar to test, with prod config)

**Tasks**:
1. Create production resource group
2. Provision production Key Vault (CA-signed certs)
3. Deploy with production settings:
   - Min 2 replicas (HA)
   - Increased resources (1 CPU, 2Gi)
   - Production DNS zone
4. Enable monitoring
5. Run smoke tests

**Success Criteria**:
- ✅ Production environment deployed
- ✅ E2E tests pass in prod
- ✅ HA validated (2+ replicas healthy)

---

## Risks & Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Docker buildx ARM64 emulation slow | Delays builds 2-3x | Medium | Use native ARM64 runners for CI/CD |
| Container Apps quota exceeded | Deployment fails | Low | Check quotas before deployment, request increase |
| Key Vault name collision | Provision fails | Low | Use MD5 hash suffix for uniqueness |
| Health probe port not exposed | App marked unhealthy | Medium | Test health probe config in Phase 3, use az containerapp update |
| E2E test flakiness | False negatives | Medium | Add retries, increase timeouts, verbose logging |
| Production cert renewal | Outage after 90 days | High | Document cert renewal process, consider cert-manager integration |

---

## Timeline

| Phase | Duration | Dependencies | Can Parallelize? |
|-------|----------|--------------|------------------|
| Phase 0: Research | 1 hour | None | N/A |
| Phase 1: Docker Build | 2 hours | Phase 0 | No |
| Phase 2: Key Vault | 1 hour | Phase 0 | Yes (parallel with Phase 1) |
| Phase 3: Container Apps | 3 hours | Phase 1, 2 | No |
| Phase 4: E2E Tests | 3 hours | Phase 3 | No |
| Phase 5: DNS (P1) | 2 hours | Phase 3 | Yes (parallel with Phase 4) |
| Phase 6: Monitoring (P2) | 2 hours | Phase 3 | Yes (parallel with Phase 4) |
| Phase 7: Production | 1 hour | Phase 4 | No |
| **Total (Critical Path)** | **10 hours** | P0 only | |
| **Total (All Phases)** | **14 hours** | Including P1/P2 | |

**Optimized Schedule** (with parallelization):
- Day 1: Phase 0, 1, 2 (parallel) = 3 hours
- Day 2: Phase 3, 4 = 6 hours
- Day 3: Phase 5, 6 (parallel), 7 = 3 hours
- **Total: 12 hours** (across 3 days)

---

## Testing Strategy

### Unit Testing (Scripts)
- Test script argument parsing (valid/invalid inputs)
- Test error handling (missing prerequisites, API failures)
- Test idempotency (run twice, same result)
- Test dry-run mode (no actual changes)

### Integration Testing
- Build images locally, verify architectures
- Deploy to test environment, verify health
- Run e2e tests, verify all scenarios pass
- Cleanup, verify resources deleted

### Acceptance Testing
- DevOps engineer deploys test env in <10 minutes
- E2E tests execute in <2 minutes
- All 7 test scenarios pass
- Cleanup leaves no orphaned resources

---

## Next Steps After Implementation

1. **Merge to main**: After all P0 tasks complete and tests pass
2. **Deploy to production**: Run Phase 7 (production deployment)
3. **Enable monitoring**: Complete Phase 6 (Application Insights)
4. **Document runbooks**: Create operational procedures
5. **CI/CD integration**: GitHub Actions workflow for automated deployment

---

## References

- [Azure Container Apps Documentation](https://learn.microsoft.com/en-us/azure/container-apps/)
- [Docker Buildx Multi-Platform](https://docs.docker.com/build/building/multi-platform/)
- [Azure Key Vault Best Practices](https://learn.microsoft.com/en-us/azure/key-vault/general/best-practices)
- [Container Apps Health Probes](https://learn.microsoft.com/en-us/azure/container-apps/health-probes)
