# Feature Specification: Azure Deployment with Test Environment

**Feature Branch**: `001-azure-deployment-test`  
**Created**: 2025-11-27  
**Status**: Active  
**Parent**: `001-003-integration-wiring` (Management API Wired)  
**Depends On**: Docker images, Azure Container Apps, Azure DNS

## Context

**Current State**:
- ✅ Protogate server fully functional (HTTP proxy + Management API)
- ✅ Tunnel agent functional (WebSocket + HTTP/TCP forwarding)
- ✅ Local testing validated (server + agent + test service)
- ✅ Dockerfiles exist: `docker/Dockerfile.alpine` (multi-stage, multi-arch)
- ❌ No Azure infrastructure deployed
- ❌ No test/staging environment
- ❌ No e2e test automation

**Current Azure Resources** (from conversation history):
- Azure Container Registry: `protogatedevacr.azurecr.io`
- Resource Group: `protogate-dev-rg`
- Container App: `protogate-dev-app` (exists but may be outdated)

**Goal**: Deploy complete protogate system to Azure with:
1. **Test environment** - Isolated Azure resources for validation
2. **Production-ready infrastructure** - Container Apps, DNS, Key Vault, monitoring
3. **E2E test automation** - Automated testing of full tunnel flow
4. **CI/CD foundation** - Scripts and workflows for future automation

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Deploy Test Environment (Priority: P0)

**Actor**: DevOps engineer  
**Context**: Need isolated Azure environment to validate protogate before production

**Scenario**: Engineer runs deployment script that provisions Azure Container Apps for server and agent, configures DNS, sets up Key Vault for TLS certificates, and validates connectivity. All resources use `-test` suffix and isolated from production.

**Why this priority**: Cannot validate deployment without test environment. Critical path for production readiness.

**Independent Test**:
```bash
# Deploy test environment
./scripts/deploy-test-env.sh

# Verify server health
curl https://protogate-test-app.salmonisland-4f8e9c0e.westus2.azurecontainerapps.io/health

# Expected: HTTP 200 with {"status":"healthy","version":"1.0.0"}
```

**Acceptance Scenarios**:
1. **Given** Azure subscription and ACR access, **When** running deploy-test-env.sh, **Then** creates resource group, container apps, DNS zone, Key Vault with all resources in test namespace
2. **Given** deployment complete, **When** checking server health endpoint, **Then** returns 200 OK with server metadata
3. **Given** test environment running, **When** checking Azure Portal, **Then** shows all resources with `-test` suffix and healthy status

---

### User Story 2 - Build and Push Multi-Arch Docker Images (Priority: P0)

**Actor**: DevOps engineer, CI/CD pipeline  
**Context**: Need production-ready Docker images for both AMD64 (Azure) and ARM64 (local development)

**Scenario**: Engineer runs build script that creates multi-arch Docker images using buildx, tags with semantic version and commit SHA, pushes to Azure Container Registry. Images are optimized (<100MB), include health checks, and run as non-root.

**Why this priority**: Without images, cannot deploy anything. Must support both architectures for dev/prod parity.

**Independent Test**:
```bash
# Build and push images
./scripts/build-and-push.sh v1.0.0

# Verify images in ACR
az acr repository show-tags \
  --name protogatedevacr \
  --repository protogate-server \
  --output table

# Expected: Tags for v1.0.0-amd64, v1.0.0-arm64, v1.0.0 (manifest list)
```

**Acceptance Scenarios**:
1. **Given** Docker buildx configured, **When** building images, **Then** creates AMD64 and ARM64 variants successfully
2. **Given** images built, **When** pushing to ACR, **Then** uploads all architectures with manifest list
3. **Given** images in ACR, **When** pulling on Linux AMD64, **Then** gets correct architecture variant automatically

---

### User Story 3 - E2E Test Tunnel Flow (Priority: P0)

**Actor**: Automated test, QA engineer  
**Context**: Validate complete tunnel functionality: Management API → Server → Agent → Local Service

**Scenario**: Test script creates tunnel via Management API, starts agent with tunnel token, deploys test HTTP service, makes request through tunnel, validates response matches local service. Tests all error conditions (invalid token, disconnected agent, service down).

**Why this priority**: Core product validation. Must verify end-to-end flow works in Azure before claiming deployment success.

**Independent Test**:
```bash
# Run e2e test suite
./scripts/e2e-test.sh --env test

# Test creates tunnel, starts agent, makes proxied request
# Expected output:
# ✓ Tunnel created via Management API
# ✓ Agent connected to server
# ✓ HTTP request proxied successfully
# ✓ Response matches local service
# ✓ All tests passed (4/4)
```

**Acceptance Scenarios**:
1. **Given** test environment deployed, **When** creating tunnel via POST /v1/tunnels, **Then** returns 201 with valid token
2. **Given** tunnel created, **When** agent connects with token, **Then** agent reports "connected" status in logs
3. **Given** agent connected, **When** making HTTP request with tunnel subdomain, **Then** proxies to local service and returns correct response
4. **Given** agent disconnected, **When** making HTTP request, **Then** returns 503 Service Unavailable with "Agent not connected" error

---

### User Story 4 - TLS Certificate Management (Priority: P1)

**Actor**: DevOps engineer, automated cert renewal  
**Context**: Manage TLS certificates for secure tunnel connections using Azure Key Vault

**Scenario**: Deployment script provisions Azure Key Vault, generates/imports TLS certificates for server domain, configures container app to access Key Vault via managed identity. Server loads certificates on startup and auto-reloads on Key Vault updates (if supported).

**Why this priority**: Required for production, but test environment can use self-signed certs initially.

**Independent Test**:
```bash
# Provision Key Vault with certs
./scripts/provision-keyvault.sh --env test

# Verify server loads cert
curl -v https://test.tunnel.example.com 2>&1 | grep "subject:"

# Expected: Valid certificate subject matching domain
```

**Acceptance Scenarios**:
1. **Given** Key Vault provisioned, **When** storing TLS certificate, **Then** certificate accessible via Key Vault API
2. **Given** container app with managed identity, **When** app requests certificate, **Then** successfully retrieves from Key Vault
3. **Given** certificate loaded, **When** client connects via HTTPS, **Then** TLS handshake succeeds with valid certificate

---

### User Story 5 - DNS Configuration (Priority: P1)

**Actor**: DevOps engineer  
**Context**: Configure Azure DNS for tunnel subdomains (e.g., `*.tunnel.example.com`)

**Scenario**: Script creates Azure DNS zone, adds wildcard A record pointing to container app ingress, validates DNS resolution. Tunnels accessible via `{tunnel_id}.tunnel.example.com`.

**Why this priority**: Required for production tunnel routing, but test environment can use direct IPs initially.

**Independent Test**:
```bash
# Configure DNS
./scripts/configure-dns.sh --env test --zone tunnel.example.com

# Verify DNS resolution
dig @8.8.8.8 test-api.tunnel.example.com

# Expected: A record pointing to container app IP
```

**Acceptance Scenarios**:
1. **Given** DNS zone created, **When** adding wildcard record, **Then** DNS propagates within 5 minutes
2. **Given** DNS configured, **When** querying `{tunnel_id}.tunnel.example.com`, **Then** resolves to container app ingress
3. **Given** DNS resolved, **When** making HTTPS request to tunnel subdomain, **Then** routes to correct tunnel

---

### User Story 6 - Monitoring and Logging (Priority: P2)

**Actor**: DevOps engineer, on-call engineer  
**Context**: Monitor application health, view logs, set up alerts for production issues

**Scenario**: Azure Application Insights configured for distributed tracing, Log Analytics workspace collects container logs, alerts configured for high error rates and service unavailability. Dashboard shows tunnel count, active connections, request latency.

**Why this priority**: Nice to have for test, critical for production. Can defer initially.

**Independent Test**:
```bash
# Configure monitoring
./scripts/setup-monitoring.sh --env test

# Query logs
az monitor app-insights query \
  --app protogate-test-insights \
  --analytics-query "traces | where message contains 'tunnel' | top 10 by timestamp"

# Expected: Recent log entries showing tunnel operations
```

**Acceptance Scenarios**:
1. **Given** Application Insights configured, **When** server logs message, **Then** appears in Log Analytics within 1 minute
2. **Given** monitoring enabled, **When** querying metrics, **Then** shows tunnel count, request rate, error rate
3. **Given** alert rules configured, **When** error rate exceeds threshold, **Then** sends notification to ops channel

---

## Technical Requirements *(mandatory)*

### Azure Infrastructure

**Resource Group**:
- **Test**: `protogate-test-rg` (West US 2)
- **Prod**: `protogate-prod-rg` (West US 2)

**Container Registry**:
- Use existing: `protogatedevacr.azurecr.io`
- Repositories: `protogate-server`, `protogate-agent`
- Tag format: `{version}-{arch}` (e.g., `v1.0.0-amd64`, `v1.0.0-arm64`)
- Manifest lists for multi-arch: `v1.0.0`, `latest`

**Container Apps**:
- **Server App**: `protogate-test-server` / `protogate-prod-server`
  - Image: `protogatedevacr.azurecr.io/protogate-server:latest`
  - CPU: 0.5 cores, Memory: 1Gi
  - Ingress: External, HTTPS, port 443
  - Additional ports: 8080 (health), 8443 (agent WebSocket)
  - Environment variables:
    - `KEY_VAULT_URI`: Azure Key Vault URL
    - `DNS_ZONE`: `tunnel.example.com`
    - `LOG_LEVEL`: `INFO`
  - Managed Identity: Enabled (for Key Vault access)
  - Scaling: Min 1, Max 5 replicas
  - Health probe: HTTP GET /health on port 8080

- **Agent App** (for e2e testing): `protogate-test-agent`
  - Image: `protogatedevacr.azurecr.io/protogate-agent:latest`
  - CPU: 0.25 cores, Memory: 512Mi
  - No ingress (outbound only)
  - Environment variables:
    - `SERVER_URL`: `wss://protogate-test-server.{app-env}.azurecontainerapps.io:8443`
    - `TUNNEL_TOKEN`: From test tunnel creation
    - `TARGET_URL`: `http://test-service:3000`
  - Scaling: Min 0, Max 1 (scale to zero when not testing)

**Key Vault**:
- **Test**: `protogate-test-kv-{suffix}` (unique suffix for global uniqueness)
- **Prod**: `protogate-prod-kv-{suffix}`
- Secrets:
  - `tls-cert`: TLS certificate (PEM format)
  - `tls-key`: Private key (PEM format)
- Access Policy: Container app managed identity with Get/List permissions

**DNS Zone** (optional for test, required for prod):
- Zone: `tunnel.example.com` (or subdomain like `test.tunnel.example.com`)
- Records:
  - `*.tunnel.example.com` → CNAME to container app ingress
  - `@` → A record to container app IP (for Management API)

**Application Insights** (P2):
- **Test**: `protogate-test-insights`
- Connection string in container app environment
- Instrumentation: Custom tracing in server code (future enhancement)

**Log Analytics Workspace** (P2):
- **Test**: `protogate-test-logs`
- Linked to Container Apps for log collection

### Docker Configuration

**Multi-Arch Build**:
```bash
# Create buildx builder
docker buildx create --name protogate-builder --use
docker buildx inspect --bootstrap

# Build and push multi-arch
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.alpine \
  --tag protogatedevacr.azurecr.io/protogate-server:v1.0.0 \
  --tag protogatedevacr.azurecr.io/protogate-server:latest \
  --push \
  .
```

**Image Optimization**:
- Alpine Linux base (~5MB)
- Multi-stage build (build artifacts not in final image)
- Static linking where possible
- Target size: <100MB per image
- Security: Run as non-root user, minimal packages

### Deployment Scripts

**Scripts to Create**:
1. `scripts/build-and-push.sh` - Build Docker images, push to ACR
2. `scripts/deploy-test-env.sh` - Provision test environment
3. `scripts/deploy-prod-env.sh` - Provision production environment
4. `scripts/provision-keyvault.sh` - Create Key Vault, upload certificates
5. `scripts/configure-dns.sh` - Set up Azure DNS zone and records
6. `scripts/e2e-test.sh` - Run end-to-end test suite
7. `scripts/cleanup-test-env.sh` - Delete test environment resources
8. `scripts/setup-monitoring.sh` - Configure Application Insights and alerts

**Script Requirements**:
- Idempotent (safe to run multiple times)
- Error handling and validation
- Progress indicators
- JSON output option for CI/CD
- Environment parameter: `--env test|prod`
- Dry-run mode: `--dry-run`

### E2E Test Requirements

**Test Flow**:
1. Create tunnel via Management API: `POST /v1/tunnels`
2. Deploy test HTTP service (simple echo server)
3. Start agent container with tunnel token
4. Wait for agent connection (poll agent logs or server API)
5. Make HTTP request via tunnel: `curl -H 'Host: {tunnel_id}' https://{server_url}/test`
6. Validate response matches test service output
7. Test error conditions:
   - Invalid token → Agent connection rejected
   - Agent disconnected → 503 Service Unavailable
   - Nonexistent tunnel → 404 Not Found
8. Cleanup: Delete tunnel, stop agent

**Test Service**:
- Simple HTTP server echoing request details
- Returns JSON: `{"echo": "Hello from test service", "headers": {...}, "timestamp": 123456}`
- Deploy as sidecar container or separate Container App

**Test Assertions**:
- Tunnel creation returns 201 with valid token
- Agent connects within 10 seconds
- Proxied request returns 200 with expected body
- Response headers include custom headers from test service
- Agent disconnection triggers 503 error
- Invalid token triggers 401/403 error

### Non-Functional Requirements

**Performance**:
- Container startup time: <30 seconds
- Agent connection time: <10 seconds
- Tunnel request latency: <100ms overhead (vs direct)

**Reliability**:
- Server uptime: 99.9% (3 nines)
- Auto-restart on crash (Container Apps built-in)
- Health check every 30 seconds, failure threshold: 3

**Security**:
- TLS 1.2+ for all external connections
- Managed identities for Azure resource access (no secrets in env vars)
- Network isolation: Private VNet for prod (test can use default)
- Secrets in Key Vault, not source control

**Cost Optimization**:
- Test environment: Scale to zero when idle
- Container Apps consumption plan (pay per request)
- Shared Key Vault and ACR across environments

---

## Implementation Plan *(mandatory)*

### Phase 1: Docker Images and ACR (2 hours)

**Files to Create**:
- `scripts/build-and-push.sh` - Multi-arch Docker build and push script

**Tasks**:
1. Configure Docker buildx for multi-arch builds
2. Update Dockerfile.alpine for optimal layering and caching
3. Build AMD64 and ARM64 images
4. Push to Azure Container Registry with version tags
5. Verify images in ACR

**Validation**:
```bash
./scripts/build-and-push.sh v1.0.0
az acr repository show-tags -n protogatedevacr --repository protogate-server
```

**Success Criteria**:
- Both architectures build successfully
- Images pushed to ACR with correct tags
- Image size <100MB per variant

---

### Phase 2: Azure Key Vault (1 hour)

**Files to Create**:
- `scripts/provision-keyvault.sh` - Key Vault provisioning and cert upload

**Tasks**:
1. Create Azure Key Vault with unique name
2. Generate self-signed TLS certificate for testing
3. Upload certificate and private key to Key Vault
4. Configure access policy for managed identity (placeholder for future container app)
5. Validate certificate retrieval

**Validation**:
```bash
./scripts/provision-keyvault.sh --env test
az keyvault secret show --vault-name protogate-test-kv-{suffix} --name tls-cert
```

**Success Criteria**:
- Key Vault created with unique name
- Certificate stored and retrievable
- Access policies configured

---

### Phase 3: Test Environment Deployment (3 hours)

**Files to Create**:
- `scripts/deploy-test-env.sh` - Complete test environment provisioning
- `azure/container-app-server.yaml` - Server container app spec (optional, can use CLI)

**Tasks**:
1. Create resource group: `protogate-test-rg`
2. Create Container Apps environment: `protogate-test-env`
3. Deploy server container app with:
   - Image from ACR
   - Managed identity
   - Environment variables (KEY_VAULT_URI, DNS_ZONE)
   - Ingress on ports 443, 8080, 8443
   - Health probe on /health
4. Assign Key Vault access to managed identity
5. Validate server health endpoint
6. Output server URL and status

**Validation**:
```bash
./scripts/deploy-test-env.sh
curl https://{server-url}/health
# Expected: {"status":"healthy","version":"1.0.0"}
```

**Success Criteria**:
- Resource group and Container Apps environment created
- Server container app deployed and healthy
- Health endpoint returns 200 OK
- Managed identity has Key Vault access

---

### Phase 4: E2E Test Automation (3 hours)

**Files to Create**:
- `scripts/e2e-test.sh` - End-to-end test orchestration
- `test/e2e/test-service.py` - Simple HTTP echo service for testing
- `test/e2e/validate.sh` - Test assertions and cleanup

**Tasks**:
1. Create test tunnel via Management API
2. Deploy test HTTP service (Python simple server or Container App)
3. Create agent config with tunnel token
4. Deploy agent container app (or run locally in Docker)
5. Wait for agent connection (poll server logs or /v1/tunnels/{id} endpoint)
6. Make HTTP request through tunnel
7. Validate response body and headers
8. Test error conditions (invalid token, disconnected agent)
9. Cleanup: Delete tunnel, stop agent, remove test service

**Validation**:
```bash
./scripts/e2e-test.sh --env test
# Expected output:
# ✓ Test environment ready
# ✓ Tunnel created: test-tunnel-{timestamp}
# ✓ Test service deployed
# ✓ Agent connected
# ✓ HTTP request proxied successfully
# ✓ Response validation passed
# ✓ Error condition tests passed
# ✓ Cleanup completed
# All tests passed (7/7)
```

**Success Criteria**:
- Tunnel creation succeeds
- Agent connects within 10 seconds
- Proxied request returns correct response
- Error conditions handled correctly
- Cleanup removes all test resources

---

### Phase 5: DNS Configuration (2 hours) *(Optional for test)*

**Files to Create**:
- `scripts/configure-dns.sh` - Azure DNS zone and record provisioning

**Tasks**:
1. Create Azure DNS zone (or use existing)
2. Add wildcard A/CNAME record: `*.tunnel.example.com`
3. Add root record for Management API
4. Validate DNS resolution
5. Update container app ingress with custom domain
6. Configure TLS certificate for custom domain

**Validation**:
```bash
./scripts/configure-dns.sh --env test --zone test.tunnel.example.com
dig @8.8.8.8 test-api.test.tunnel.example.com
curl https://test-api.test.tunnel.example.com/
```

**Success Criteria**:
- DNS zone created with correct records
- DNS resolves to container app
- Custom domain configured on ingress
- TLS certificate valid for domain

---

### Phase 6: Monitoring and Alerts (2 hours) *(Optional P2)*

**Files to Create**:
- `scripts/setup-monitoring.sh` - Application Insights and Log Analytics setup
- `azure/alert-rules.json` - Alert rule definitions

**Tasks**:
1. Create Application Insights workspace
2. Create Log Analytics workspace
3. Link Container Apps to Log Analytics
4. Configure Application Insights connection string
5. Create alert rules:
   - High error rate (>5% requests fail)
   - Service unavailable (health check fails)
   - High latency (p95 >1s)
6. Create dashboard with key metrics

**Validation**:
```bash
./scripts/setup-monitoring.sh --env test
az monitor app-insights query --app protogate-test-insights \
  --analytics-query "requests | summarize count() by bin(timestamp, 1m)"
```

**Success Criteria**:
- Application Insights receiving telemetry
- Logs queryable in Log Analytics
- Alert rules configured and active
- Dashboard displays key metrics

---

### Phase 7: Production Deployment (1 hour)

**Files to Create**:
- `scripts/deploy-prod-env.sh` - Production environment provisioning

**Tasks**:
1. Create production resource group: `protogate-prod-rg`
2. Provision Key Vault with production certificates
3. Deploy Container Apps environment with VNet integration (optional)
4. Deploy server container app with production config:
   - Min 2 replicas for HA
   - Increased resource limits (1 CPU, 2Gi memory)
   - Production DNS zone
5. Configure DNS with production domain
6. Enable monitoring and alerts
7. Run smoke tests
8. Document production URLs and credentials

**Validation**:
```bash
./scripts/deploy-prod-env.sh
./scripts/e2e-test.sh --env prod
```

**Success Criteria**:
- Production environment deployed
- All resources healthy
- E2E tests pass
- Monitoring active

---

## Testing Strategy *(mandatory)*

### Local Testing (Pre-Deployment)

**Docker Build Test**:
```bash
# Test multi-arch build locally
docker buildx build --platform linux/amd64,linux/arm64 \
  -f docker/Dockerfile.alpine \
  -t protogate-test:local \
  --load .

# Run locally
docker run -p 443:443 -p 8080:8080 -p 8443:8443 \
  -e KEY_VAULT_URI=https://mock.vault.local \
  -e DNS_ZONE=tunnel.local \
  protogate-test:local

# Test health
curl http://localhost:8080/health
```

**Script Validation**:
```bash
# Dry-run deployment scripts
./scripts/build-and-push.sh --dry-run v1.0.0-rc1
./scripts/deploy-test-env.sh --dry-run
./scripts/e2e-test.sh --dry-run
```

### Integration Testing (Test Environment)

**Deployment Tests**:
1. Fresh deployment (no existing resources)
2. Update deployment (existing resources)
3. Rollback deployment (previous version)
4. Resource cleanup

**E2E Test Scenarios**:
1. **Happy Path**: Tunnel creation → agent connection → proxied request → success
2. **Invalid Token**: Agent with wrong token → connection rejected
3. **Agent Disconnect**: Agent stops → request returns 503
4. **Tunnel Not Found**: Request to nonexistent tunnel → 404
5. **Management API**: CRUD operations on tunnels
6. **Concurrent Tunnels**: Multiple agents, multiple tunnels
7. **High Load**: 100 concurrent requests through tunnel

**Performance Tests**:
```bash
# Latency test
ab -n 1000 -c 10 https://{tunnel-url}/test

# Throughput test
wrk -t 4 -c 100 -d 30s https://{tunnel-url}/test
```

### Production Validation

**Smoke Tests**:
1. Health endpoint returns 200
2. Management API responsive
3. Can create/delete tunnel
4. Agent can connect
5. Simple proxied request works

**Monitoring Checks**:
1. All container replicas healthy
2. No error spikes in logs
3. Latency within acceptable range
4. Certificate expiry >30 days

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

**Deployment Success**:
- ✅ Docker images built and pushed to ACR (both architectures)
- ✅ Test environment deployed and healthy
- ✅ Key Vault provisioned with TLS certificates
- ✅ Server health endpoint returns 200 OK
- ✅ E2E tests pass (7/7 scenarios)

**Quality Metrics**:
- Container startup time: <30 seconds
- E2E test execution time: <2 minutes
- Zero manual steps in deployment (fully automated)
- All scripts idempotent and error-tolerant

**User Value**:
- DevOps can deploy test environment in <10 minutes
- Automated e2e tests validate deployment success
- Infrastructure as code (reproducible, version controlled)
- Foundation for CI/CD pipeline

---

## Definition of Done

### Must Have (P0)
- [ ] Docker images build successfully for AMD64 and ARM64
- [ ] Images pushed to Azure Container Registry
- [ ] Azure Key Vault provisioned with TLS certificates
- [ ] Test environment deployed (resource group, container apps)
- [ ] Server container app healthy and accessible
- [ ] Health endpoint returns 200 OK
- [ ] E2E test script created and passes all scenarios:
  - [ ] Tunnel creation via Management API
  - [ ] Agent connection with valid token
  - [ ] HTTP request proxied through tunnel
  - [ ] Response validation
  - [ ] Error condition tests (invalid token, disconnected agent)
- [ ] Deployment scripts created and tested:
  - [ ] `build-and-push.sh`
  - [ ] `deploy-test-env.sh`
  - [ ] `provision-keyvault.sh`
  - [ ] `e2e-test.sh`
- [ ] Documentation updated with deployment instructions

### Should Have (P1)
- [ ] DNS configuration script and validation
- [ ] Custom domain configured on container app
- [ ] TLS certificate for custom domain
- [ ] Production deployment script
- [ ] Cleanup script for test environment

### Nice to Have (P2)
- [ ] Application Insights configured
- [ ] Log Analytics workspace with queries
- [ ] Alert rules for error rate and availability
- [ ] Monitoring dashboard
- [ ] Performance test results documented

---

## Rollback Plan

**If Deployment Fails**:
1. Check container app logs: `az containerapp logs show`
2. Verify Key Vault access: `az keyvault show --name {vault-name}`
3. Validate image exists in ACR: `az acr repository show`
4. Rollback to previous image tag: `az containerapp update --image {previous-tag}`

**If E2E Tests Fail**:
1. Check server logs for errors
2. Verify agent connection in server logs
3. Test Management API directly (bypass tunnel)
4. Validate network connectivity between containers
5. Re-run with verbose logging: `./scripts/e2e-test.sh --verbose`

**Emergency Cleanup**:
```bash
# Delete test environment completely
az group delete --name protogate-test-rg --yes --no-wait

# Delete images from ACR (if bad build)
az acr repository delete \
  --name protogatedevacr \
  --repository protogate-server \
  --yes
```

---

## Dependencies

**External Services**:
- Azure subscription with sufficient quota (Container Apps, Key Vault)
- Azure Container Registry (existing: `protogatedevacr`)
- Docker with buildx support
- Azure CLI installed and authenticated
- Domain name for DNS configuration (optional for test)

**Code Dependencies**:
- Feature `001-003-integration-wiring` merged (Management API wired)
- Docker multi-stage build working
- Server binary builds on Linux AMD64
- Agent binary builds on Linux AMD64/ARM64

**Prerequisites**:
```bash
# Azure CLI
az --version  # >=2.50.0

# Docker buildx
docker buildx version

# Azure login
az login
az account set --subscription {subscription-id}

# ACR login
az acr login --name protogatedevacr
```

---

## Notes

### Cost Projection by Deployment Size

Based on Azure Container Apps pricing (Consumption plan - West US 2, as of Nov 2025):
- vCPU: $0.000024/vCPU-second (~$62.21/vCPU/month)
- Memory: $0.000003/GiB-second (~$7.78/GiB/month)
- HTTP requests: First 2M free, then $0.40 per million

| Deployment Size | Tunnels | Replicas | vCPU Total | Memory Total | Est. Requests/Month | Monthly Cost Breakdown | **Total/Month** |
|----------------|---------|----------|------------|--------------|---------------------|------------------------|-----------------|
| **Test/Dev** | 5-10 | 1 | 0.5 | 1Gi | 100K | Container Apps: $10<br>Key Vault: $1<br>Log Analytics: $5<br>ACR (shared): $0 | **$16** |
| **Startup** | 50 | 2 | 2.0 | 4Gi | 1M | Container Apps: $156<br>Key Vault: $1<br>Log Analytics: $10<br>DNS Zone: $1<br>App Insights: $5 | **$173** |
| **Small Business** | 100-200 | 4 | 4.0 | 8Gi | 5M | Container Apps: $311<br>Key Vault: $1<br>Log Analytics: $25<br>DNS Zone: $1<br>App Insights: $15<br>Requests: $1 | **$354** |
| **Medium Business** | 500 | 10 | 10.0 | 20Gi | 25M | Container Apps: $778<br>Key Vault: $2<br>Log Analytics: $75<br>DNS Zone: $1<br>App Insights: $50<br>Requests: $9 | **$915** |
| **Enterprise** | 1,000 | 20 | 20.0 | 40Gi | 100M | Container Apps: $1,556<br>Key Vault: $5<br>Log Analytics: $200<br>DNS Zone: $2<br>App Insights: $150<br>Requests: $39 | **$1,952** |
| **Large Enterprise** | 5,000 | 100 | 100.0 | 200Gi | 500M | Container Apps: $7,779<br>Key Vault: $20<br>Log Analytics: $800<br>DNS Zone (multi): $10<br>App Insights: $500<br>Requests: $199 | **$9,308** |

**Key Assumptions**:
- **Tunnels**: Active concurrent tunnel agents
- **Replicas**: Server instances (50 tunnels per replica max)
- **vCPU per replica**: 1.0 cores (baseline), 2.0 cores (high load)
- **Memory per replica**: 2Gi (baseline), 4Gi (high load)
- **Requests/tunnel/month**: ~10,000 for light use, ~20,000 for moderate, ~100,000 for heavy
- **Data transfer**: Included in Container Apps pricing (first 100GB free/month)
- **Egress**: $0.087/GB after first 100GB (minimal for tunnel use case)

**Cost Optimization Tips**:
1. **Scale to zero** when idle (test environments)
2. **Use consumption plan** vs dedicated (saves ~40%)
3. **Share Key Vault** across environments ($1 vs $3)
4. **Archive old logs** after 30 days (Log Analytics retention)
5. **Use reserved capacity** for predictable workloads (save 20-30%)

**Breaking Down Container Apps Cost**:
```
Formula: (vCPU-seconds × $0.000024) + (GiB-seconds × $0.000003)

Example: 2 replicas, 1 vCPU, 2Gi each, 730 hours/month
vCPU cost = 2 replicas × 1 vCPU × 730 hours × 3600 sec/hr × $0.000024 = $126.14
Memory cost = 2 replicas × 2 GiB × 730 hours × 3600 sec/hr × $0.000003 = $31.54
Total = $157.68/month
```

**Comparison with Alternatives**:

| Platform | Small (4 vCPU, 8Gi) | Medium (10 vCPU, 20Gi) | Large (20 vCPU, 40Gi) |
|----------|---------------------|------------------------|----------------------|
| **Azure Container Apps (Consumption)** | $354/month | $915/month | $1,952/month |
| Azure App Service (Premium v3) | $438/month | $876/month | $1,752/month |
| Azure Kubernetes Service | $200/month (nodes) + $73 (control plane) = $273/month | $500/month + $73 = $573/month | $1,000/month + $73 = $1,073/month |
| AWS Fargate (ECS) | $400/month | $1,025/month | $2,190/month |
| GCP Cloud Run | $380/month | $975/month | $2,080/month |

**Winner**: Azure Container Apps is competitive, especially for variable workloads. AKS is cheaper at scale but requires more ops overhead.

**Production Scale Considerations**:
- **Multi-region deployment** (future): 2x-3x cost (replicate across regions)
- **Auto-scaling**: Pay only for active replicas (1-10 based on load)
- **VNet integration**: $0.01/hour per instance = ~$7.30/replica/month
- **Azure Front Door**: $35/month + $0.01/GB = ~$50-100/month for global routing

**Cost per Customer Metrics**:
```
Startup (50 customers):     $173 / 50 = $3.46/customer/month
Small Business (200):       $354 / 200 = $1.77/customer/month
Medium Business (500):      $915 / 500 = $1.83/customer/month
Enterprise (1,000):         $1,952 / 1,000 = $1.95/customer/month
Large Enterprise (5,000):   $9,308 / 5,000 = $1.86/customer/month
```

**Pricing sweet spot**: ~$1.80-2.00 per customer/month at scale (500+ customers)

**Future Enhancements**:
- GitHub Actions CI/CD pipeline
- Terraform/Bicep IaC
- Blue-green deployments
- Canary releases
- Automated certificate renewal
- Distributed tracing with OpenTelemetry

