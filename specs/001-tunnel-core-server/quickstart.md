# Protogate Quickstart: Azure Deployment Guide

**Version**: 1.0.0  
**Created**: 2025-11-22  
**Estimated Time**: 20-30 minutes  
**Prerequisites**: Azure subscription, Azure CLI installed, Bicep CLI installed

---

## Overview

This guide walks through deploying the Protogate Core Server to Azure Container Apps with full infrastructure setup: managed identity, Key Vault for secrets, DNS Zone for wildcard routing, and Log Analytics for observability.

**Target Cost**: $6-12/month for light usage (1-5 tunnels, <10k requests/day)

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        Azure Subscription                         │
│                                                                   │
│  ┌──────────────────┐    ┌──────────────────┐                   │
│  │  DNS Zone        │    │  Key Vault       │                   │
│  │  tunnel.mycorp   │    │  - Certificates  │                   │
│  │  *.tunnel.mycorp │───▶│  - Token secrets │                   │
│  └──────────────────┘    └──────────────────┘                   │
│           │                      ▲                                │
│           │                      │                                │
│           ▼                      │                                │
│  ┌────────────────────────────┴────────────────────────┐        │
│  │  Container App (protogate-server)                  │        │
│  │  - Managed Identity enabled                         │        │
│  │  - Ingress: HTTPS (port 443)                        │        │
│  │  - Agent port: 8443 (internal)                      │        │
│  │  - CPU: 0.5, Memory: 1Gi                            │        │
│  │  - Replicas: 1-10 (auto-scale on CPU 70%)          │        │
│  └─────────────────────────────────────────────────────┘        │
│           │                                                       │
│           ▼                                                       │
│  ┌──────────────────┐                                            │
│  │  Log Analytics   │                                            │
│  │  - Structured    │                                            │
│  │    logs          │                                            │
│  │  - Metrics       │                                            │
│  └──────────────────┘                                            │
└───────────────────────────────────────────────────────────────────┘
```

---

## Prerequisites

### 1. Install Tools

```bash
# Azure CLI
brew install azure-cli  # macOS
# Or: https://learn.microsoft.com/en-us/cli/azure/install-azure-cli

# Bicep CLI (included with Azure CLI 2.20+)
az bicep install
az bicep upgrade

# Verify installation
az --version
az bicep version
```

### 2. Login to Azure

```bash
az login

# Set default subscription (if you have multiple)
az account set --subscription "YOUR_SUBSCRIPTION_ID"

# Verify
az account show
```

### 3. Register Resource Providers

```bash
az provider register --namespace Microsoft.App
az provider register --namespace Microsoft.OperationalInsights
az provider register --namespace Microsoft.KeyVault
az provider register --namespace Microsoft.Network

# Wait for registration (takes 1-2 minutes)
az provider show --namespace Microsoft.App --query "registrationState"
```

---

## Step 1: Clone and Prepare

```bash
# Clone repository (replace with your repo URL)
git clone https://github.com/mycorp/protogate.git
cd protogate

# Navigate to deployment directory
cd deploy/azure
```

---

## Step 2: Configure Parameters

Create `parameters.json` with your configuration:

```json
{
  "$schema": "https://schema.management.azure.com/schemas/2019-04-01/deploymentParameters.json#",
  "contentVersion": "1.0.0.0",
  "parameters": {
    "projectName": {
      "value": "dnstunnel"
    },
    "location": {
      "value": "eastus"
    },
    "environment": {
      "value": "prod"
    },
    "dnsZoneName": {
      "value": "tunnel.mycorp.com"
    },
    "containerImageTag": {
      "value": "1.0.0"
    },
    "minReplicas": {
      "value": 1
    },
    "maxReplicas": {
      "value": 10
    }
  }
}
```

**Key Parameters**:
- `projectName`: Short name for resource naming (alphanumeric, max 12 chars)
- `location`: Azure region (e.g., eastus, westus2, westeurope)
- `dnsZoneName`: Your custom domain (must own and control DNS)
- `containerImageTag`: Docker image version to deploy

---

## Step 3: Deploy Infrastructure

Run the deployment (takes ~5-10 minutes):

```bash
# Create resource group
az group create \
  --name rg-dnstunnel-prod \
  --location eastus

# Deploy Bicep template
az deployment group create \
  --resource-group rg-dnstunnel-prod \
  --template-file main.bicep \
  --parameters parameters.json
```

**What gets created**:
1. Resource Group: `rg-dnstunnel-prod`
2. Log Analytics Workspace: `log-dnstunnel-prod`
3. Container Apps Environment: `cae-dnstunnel-prod`
4. Key Vault: `kv-dnstunnel-prod-{unique}`
5. DNS Zone: `tunnel.mycorp.com`
6. Container App: `ca-dnstunnel-server-prod`
7. Managed Identity: `id-dnstunnel-prod`

---

## Step 4: Configure DNS

After deployment completes, get the Container App FQDN:

```bash
# Get the app's FQDN
az containerapp show \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --query properties.configuration.ingress.fqdn \
  --output tsv
```

Output example: `ca-dnstunnel-server-prod.kindbeach-12345678.eastus.azurecontainerapps.io`

### Configure Wildcard DNS

In your DNS provider (e.g., Azure DNS Zone, Cloudflare, GoDaddy), create:

**CNAME Record**:
- **Name**: `*.tunnel` (or just `*` if zone is `tunnel.mycorp.com`)
- **Type**: CNAME
- **Value**: `ca-dnstunnel-server-prod.kindbeach-12345678.eastus.azurecontainerapps.io`
- **TTL**: 300 (5 minutes)

**A Record (alternative if CNAME not supported)**:
- Get Container App IP: `nslookup ca-dnstunnel-server-prod.kindbeach-12345678.eastus.azurecontainerapps.io`
- Create A record for `*.tunnel` pointing to that IP

### Verify DNS Propagation

```bash
# Wait 5-10 minutes for DNS propagation, then test
nslookup api.tunnel.mycorp.com

# Should resolve to Container App FQDN or IP
```

---

## Step 5: Upload TLS Certificate

Generate or obtain a wildcard TLS certificate for `*.tunnel.mycorp.com`.

### Option A: Self-Signed (Testing Only)

```bash
# Generate self-signed certificate
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout tunnel.key \
  -out tunnel.crt \
  -subj "/CN=*.tunnel.mycorp.com" \
  -addext "subjectAltName=DNS:*.tunnel.mycorp.com"

# Combine into PFX
openssl pkcs12 -export -out tunnel.pfx \
  -inkey tunnel.key -in tunnel.crt \
  -password pass:YourStrongPassword
```

### Option B: Let's Encrypt (Production)

Use `certbot` with DNS challenge:

```bash
# Install certbot
brew install certbot  # macOS

# Generate certificate (manual DNS challenge)
sudo certbot certonly --manual --preferred-challenges dns \
  -d "*.tunnel.mycorp.com"

# Follow prompts to create TXT record in DNS
# Certificate saved to: /etc/letsencrypt/live/tunnel.mycorp.com/

# Convert to PFX
sudo openssl pkcs12 -export \
  -out tunnel.pfx \
  -inkey /etc/letsencrypt/live/tunnel.mycorp.com/privkey.pem \
  -in /etc/letsencrypt/live/tunnel.mycorp.com/fullchain.pem \
  -password pass:YourStrongPassword
```

### Upload to Key Vault

```bash
# Upload certificate
az keyvault certificate import \
  --vault-name kv-dnstunnel-prod-abc123 \
  --name wildcard-tunnel-cert \
  --file tunnel.pfx \
  --password YourStrongPassword

# Verify
az keyvault certificate show \
  --vault-name kv-dnstunnel-prod-abc123 \
  --name wildcard-tunnel-cert
```

---

## Step 6: Build and Push Docker Image

### Build Docker Image

```bash
# From repository root
docker build -t dnstunnel-server:1.0.0 -f docker/Dockerfile .

# Test locally
docker run -p 8080:8080 \
  -e PORT=8080 \
  -e LOG_LEVEL=info \
  dnstunnel-server:1.0.0
```

### Push to Azure Container Registry

```bash
# Create ACR (if not exists)
az acr create \
  --resource-group rg-dnstunnel-prod \
  --name acrDnsTunnel \
  --sku Basic

# Login to ACR
az acr login --name acrDnsTunnel

# Tag image
docker tag dnstunnel-server:1.0.0 \
  acrdnstunnel.azurecr.io/dnstunnel-server:1.0.0

# Push image
docker push acrdnstunnel.azurecr.io/dnstunnel-server:1.0.0
```

### Update Container App

```bash
# Update Container App with new image
az containerapp update \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --image acrdnstunnel.azurecr.io/dnstunnel-server:1.0.0
```

---

## Step 7: Verify Deployment

### Health Check

```bash
# Test health endpoint
curl https://api.tunnel.mycorp.com/health

# Expected response:
# {"status":"healthy","timestamp":"2025-11-22T12:00:00Z"}
```

### View Logs

```bash
# Stream live logs
az containerapp logs show \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --follow

# Query logs with KQL
az monitor log-analytics query \
  --workspace $(az containerapp env show \
    --name cae-dnstunnel-prod \
    --resource-group rg-dnstunnel-prod \
    --query properties.appLogsConfiguration.logAnalyticsConfiguration.customerId \
    --output tsv) \
  --analytics-query "ContainerAppConsoleLogs_CL | where ContainerAppName_s == 'ca-dnstunnel-server-prod' | order by TimeGenerated desc | limit 100"
```

### Test Tunnel Creation

```bash
# Get Entra ID token (if management API enabled)
TOKEN=$(az account get-access-token --resource api://protogate --query accessToken -o tsv)

# Create test tunnel
curl -X POST https://api.tunnel.mycorp.com/api/v1/tunnels \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "test",
    "protocol": "HTTPS",
    "target_url": "http://localhost:5000"
  }'

# Expected response:
# {
#   "tunnel": { ... },
#   "token": "tnl_a1b2c3d4e5f6..."
# }
```

---

## Cost Estimation

### Monthly Cost Breakdown (Light Usage)

| Service                     | Configuration                | Cost (USD) |
|-----------------------------|------------------------------|------------|
| **Container Apps**          | 0.5 vCPU, 1Gi RAM, 1 replica | $5-8       |
| **Log Analytics**           | 1GB ingestion/month          | $1-2       |
| **Key Vault**               | Standard tier, 1000 ops/mo   | $0.50      |
| **DNS Zone**                | 1 zone, 1M queries/mo        | $0.50      |
| **Container Registry**      | Basic tier                   | $5         |
| **Data Transfer**           | 1GB outbound                 | $0.10      |
| **Total**                   |                              | **$12-16** |

### Cost Optimization Tips

1. **Use Consumption Plan**: Container Apps scales to zero when idle (not supported in MVP, but future)
2. **Reduce Log Retention**: Set to 30 days instead of default 90 days
3. **Share Resources**: Use same Log Analytics workspace for multiple apps
4. **Monitor Usage**: Set budget alerts at $20/month

---

## Scaling Configuration

### Auto-Scaling Rules

Configured in Bicep template:

```yaml
scale:
  minReplicas: 1
  maxReplicas: 10
  rules:
    - name: cpu-scaling
      custom:
        type: cpu
        metadata:
          type: Utilization
          value: "70"  # Scale up at 70% CPU
```

### Manual Scaling

```bash
# Scale to specific replica count
az containerapp update \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --min-replicas 2 \
  --max-replicas 20
```

### Performance Testing

```bash
# Install wrk
brew install wrk

# Benchmark tunnel endpoint
wrk -t4 -c100 -d30s https://api.tunnel.mycorp.com/health

# Expected throughput: 10k-50k req/s (single replica)
```

---

## Security Hardening

### 1. Enable Managed Identity

Already configured in Bicep, verify:

```bash
az containerapp identity show \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod
```

### 2. Restrict Key Vault Access

```bash
# Grant Container App access to Key Vault
PRINCIPAL_ID=$(az containerapp identity show \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --query principalId -o tsv)

az keyvault set-policy \
  --name kv-dnstunnel-prod-abc123 \
  --object-id $PRINCIPAL_ID \
  --secret-permissions get list \
  --certificate-permissions get
```

### 3. Enable Firewall Rules

```bash
# Restrict Container App ingress to specific IPs (optional)
az containerapp ingress update \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --allow-ip-range 203.0.113.0/24,198.51.100.0/24
```

### 4. Enable Entra ID Authentication

See `docs/entra-integration.md` for step-by-step guide.

---

## Monitoring and Alerts

### Create Alert Rules

```bash
# Alert on high error rate
az monitor metrics alert create \
  --name high-error-rate \
  --resource-group rg-dnstunnel-prod \
  --scopes $(az containerapp show --name ca-dnstunnel-server-prod --resource-group rg-dnstunnel-prod --query id -o tsv) \
  --condition "avg Percentage CPU > 80" \
  --window-size 5m \
  --evaluation-frequency 1m \
  --action email --email admin@mycorp.com
```

### Dashboard Setup

1. Navigate to Azure Portal > Dashboards
2. Create new dashboard: "Protogate Monitoring"
3. Add tiles:
   - Container App CPU/Memory
   - Log Analytics query for request latency
   - Log Analytics query for error rate
   - Cost Management widget

---

## Troubleshooting

### Container Not Starting

```bash
# Check logs for errors
az containerapp logs show \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod \
  --tail 100

# Common issues:
# - Missing Key Vault permissions
# - Invalid environment variables
# - Docker image pull failure
```

### DNS Not Resolving

```bash
# Check DNS propagation
dig api.tunnel.mycorp.com

# Verify CNAME record
dig CNAME api.tunnel.mycorp.com

# Test with specific DNS server
dig @8.8.8.8 api.tunnel.mycorp.com
```

### High Latency

```bash
# Check replica count
az containerapp replica list \
  --name ca-dnstunnel-server-prod \
  --resource-group rg-dnstunnel-prod

# Check CPU/memory usage
az monitor metrics list \
  --resource $(az containerapp show --name ca-dnstunnel-server-prod --resource-group rg-dnstunnel-prod --query id -o tsv) \
  --metric "Percentage CPU" "MemoryWorkingSetBytes"
```

### TLS Certificate Errors

```bash
# Verify certificate in Key Vault
az keyvault certificate show \
  --vault-name kv-dnstunnel-prod-abc123 \
  --name wildcard-tunnel-cert

# Check expiration date
openssl s_client -connect api.tunnel.mycorp.com:443 -showcerts
```

---

## Clean Up Resources

To delete all resources:

```bash
# Delete resource group (WARNING: irreversible)
az group delete \
  --name rg-dnstunnel-prod \
  --yes --no-wait
```

---

## Next Steps

1. **Deploy Tunnel Agent**: Follow `docs/agent-deployment.md`
2. **Configure Printer4All**: See `docs/printer4all-integration.md`
3. **Set Up CI/CD**: Use GitHub Actions (`.github/workflows/deploy.yml`)
4. **Enable Monitoring**: Configure Application Insights (optional)
5. **Production Hardening**: Review security checklist in constitution

---

## Support

- **Documentation**: https://github.com/mycorp/protogate/tree/main/docs
- **Issues**: https://github.com/mycorp/protogate/issues
- **Slack**: #protogate channel

---

**Deployment Complete! 🎉**

Your Protogate server is now running on Azure Container Apps and ready to accept tunnel connections.
