# Azure Deployment Guide - Protogate

**Status**: Ready for deployment  
**Estimated Time**: 30-45 minutes  
**Cost**: ~$12-16/month for light usage (1-5 tunnels)

---

## 🎯 Quick Start (TL;DR)

```bash
# 1. Login and prepare
az login
az account set --subscription "YOUR_SUBSCRIPTION_ID"

# 2. Create resource group
az group create --name protogate-rg --location eastus

# 3. Deploy infrastructure
cd deploy/azure
az deployment group create \
  --resource-group protogate-rg \
  --template-file main.bicep \
  --parameters parameters.dev.json \
  --parameters dnsDomainName="tunnel.yourdomain.com"

# 4. Build and push Docker image
docker build -f docker/Dockerfile.alpine -t protogate:latest .
# (See section below for Azure Container Registry push)

# 5. Update Container App with image
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-rg \
  --image your-acr.azurecr.io/protogate:latest
```

---

## 📋 Prerequisites

### Required Tools

```bash
# Azure CLI
brew install azure-cli  # macOS
# Or: https://aka.ms/install-azure-cli

# Docker
brew install docker  # macOS with Docker Desktop
# Or: https://docs.docker.com/get-docker/

# Bicep (included with Azure CLI 2.20+)
az bicep upgrade

# Verify
az --version
docker --version
az bicep version
```

### Azure Subscription Requirements

- **Subscription**: Active Azure subscription
- **Permissions**: Contributor or Owner role on subscription
- **Resource Providers**: Auto-registered during deployment
- **Quotas**: Ensure Container Apps quota available (default is sufficient)

---

## 🚀 Step-by-Step Deployment

### Step 1: Azure Login and Setup

```bash
# Login
az login

# List subscriptions
az account list --output table

# Set default subscription
az account set --subscription "YOUR_SUBSCRIPTION_ID"

# Verify
az account show --output table
```

### Step 2: Choose Your Domain Strategy

**Option A: Use Existing Domain** (Recommended for production)
```bash
# You own tunnel.example.com
DOMAIN="tunnel.example.com"
```

**Option B: Use Azure-Provided Domain** (For testing)
```bash
# Will use Container Apps auto-generated domain
# Format: protogate-dev-app.nicegrass-12345678.eastus.azurecontainerapps.io
DOMAIN="auto"  # We'll update this after deployment
```

### Step 3: Create Resource Group

```bash
# Choose location (eastus, westus2, westeurope, etc.)
LOCATION="eastus"
RESOURCE_GROUP="protogate-rg"

az group create \
  --name $RESOURCE_GROUP \
  --location $LOCATION \
  --tags Environment=production Project=Protogate
```

### Step 4: Update Deployment Parameters

Edit `deploy/azure/parameters.dev.json`:

```json
{
  "$schema": "https://schema.management.azure.com/schemas/2019-04-01/deploymentParameters.json#",
  "contentVersion": "1.0.0.0",
  "parameters": {
    "location": {
      "value": "eastus"  // Change to your region
    },
    "environment": {
      "value": "dev"  // or "staging", "prod"
    },
    "baseName": {
      "value": "protogate"
    },
    "dnsDomainName": {
      "value": "tunnel.yourdomain.com"  // YOUR DOMAIN HERE
    },
    "imageTag": {
      "value": "v1.0.0"  // Change to your version
    },
    "minReplicas": {
      "value": 1  // 0 for cost savings (cold start penalty)
    },
    "maxReplicas": {
      "value": 5  // Auto-scale up to 5 instances
    },
    "cpu": {
      "value": "0.5"  // 0.5 cores per instance
    },
    "memory": {
      "value": "1Gi"  // 1GB RAM per instance
    }
  }
}
```

### Step 5: Deploy Azure Infrastructure

```bash
cd deploy/azure

# Validate template
az deployment group validate \
  --resource-group $RESOURCE_GROUP \
  --template-file main.bicep \
  --parameters parameters.dev.json

# Deploy (takes 5-10 minutes)
az deployment group create \
  --resource-group $RESOURCE_GROUP \
  --template-file main.bicep \
  --parameters parameters.dev.json \
  --name "protogate-$(date +%Y%m%d-%H%M%S)" \
  --verbose

# Check deployment status
az deployment group list \
  --resource-group $RESOURCE_GROUP \
  --output table
```

**What gets created:**
- ✅ Log Analytics Workspace (`protogate-dev-logs`)
- ✅ Key Vault (`protogatedevkv`)
- ✅ DNS Zone (`tunnel.yourdomain.com`)
- ✅ Container App Environment
- ✅ Container App (`protogate-dev-app`)
- ✅ Managed Identity for Container App

### Step 6: Build and Push Docker Image

```bash
# Return to repo root
cd /Volumes/Projects/protogate

# Get ACR login server
ACR_NAME=$(az acr list --resource-group $RESOURCE_GROUP --query "[0].name" -o tsv)
ACR_LOGIN_SERVER=$(az acr show --name $ACR_NAME --query loginServer -o tsv)

echo "ACR: $ACR_LOGIN_SERVER"

# Login to ACR
az acr login --name $ACR_NAME

# Build Docker image (takes 10-15 minutes first time)
docker build \
  -f docker/Dockerfile.alpine \
  -t protogate:latest \
  -t protogate:v1.0.0 \
  -t $ACR_LOGIN_SERVER/protogate:latest \
  -t $ACR_LOGIN_SERVER/protogate:v1.0.0 \
  .

# Push to ACR
docker push $ACR_LOGIN_SERVER/protogate:latest
docker push $ACR_LOGIN_SERVER/protogate:v1.0.0
```

### Step 7: Update Container App with Image

```bash
# Get Container App name
APP_NAME=$(az containerapp list \
  --resource-group $RESOURCE_GROUP \
  --query "[0].name" -o tsv)

echo "Container App: $APP_NAME"

# Update with new image
az containerapp update \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --image $ACR_LOGIN_SERVER/protogate:v1.0.0 \
  --revision-suffix v1-0-0

# Check status
az containerapp show \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --query "properties.latestRevisionFqdn" -o tsv
```

### Step 8: Configure Tunnel Tokens in Key Vault

```bash
# Get Key Vault name
KV_NAME=$(az keyvault list \
  --resource-group $RESOURCE_GROUP \
  --query "[0].name" -o tsv)

echo "Key Vault: $KV_NAME"

# Create a test tunnel token
az keyvault secret set \
  --vault-name $KV_NAME \
  --name "tunnel-token-test-api" \
  --value "tnl_production_$(openssl rand -hex 32)"

# Verify
az keyvault secret list \
  --vault-name $KV_NAME \
  --output table
```

### Step 9: Configure DNS (If Using Custom Domain)

**Option A: Delegate DNS to Azure**

If you control the domain's nameservers:

```bash
# Get Azure DNS nameservers
az network dns zone show \
  --resource-group $RESOURCE_GROUP \
  --name tunnel.yourdomain.com \
  --query "nameServers" -o tsv

# Add these NS records at your domain registrar:
# ns1-01.azure-dns.com
# ns2-01.azure-dns.net
# ns3-01.azure-dns.org
# ns4-01.azure-dns.info
```

**Option B: CNAME to Container App**

If you can't delegate the full zone:

```bash
# Get Container App FQDN
FQDN=$(az containerapp show \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --query "properties.configuration.ingress.fqdn" -o tsv)

echo "FQDN: $FQDN"

# Add CNAME at your DNS provider:
# *.tunnel.yourdomain.com CNAME $FQDN
```

### Step 10: Configure TLS Certificate

```bash
# Get container app environment name
ENV_NAME=$(az containerapp env list \
  --resource-group $RESOURCE_GROUP \
  --query "[0].name" -o tsv)

# Add custom domain certificate
az containerapp hostname add \
  --hostname tunnel.yourdomain.com \
  --resource-group $RESOURCE_GROUP \
  --name $APP_NAME

# Bind TLS certificate (managed certificate)
az containerapp hostname bind \
  --hostname tunnel.yourdomain.com \
  --resource-group $RESOURCE_GROUP \
  --name $APP_NAME \
  --environment $ENV_NAME \
  --validation-method CNAME
```

---

## ✅ Verification

### 1. Check Container App Health

```bash
# Get logs
az containerapp logs show \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --follow

# Check health endpoint
FQDN=$(az containerapp show \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --query "properties.configuration.ingress.fqdn" -o tsv)

curl https://$FQDN/health
```

Expected output:
```json
{
  "status": "healthy",
  "version": "1.0.0",
  "uptime": 3600,
  "metrics": {
    "active_tunnels": 0,
    "active_http_connections": 0,
    "active_tcp_connections": 0
  }
}
```

### 2. Test Agent Connection

```bash
# On your local machine or server with tunnel-agent
./tunnel-agent/build/tunnel-agent \
  --server-url https://$FQDN:443 \
  --tunnel-id test-api \
  --token tnl_production_YOUR_TOKEN \
  --target-host localhost \
  --target-port 3000
```

### 3. Test HTTP Tunnel

```bash
# Start local service
python3 -m http.server 3000 &

# Make request through tunnel
curl https://test-api.tunnel.yourdomain.com/
```

---

## 📊 Monitoring and Operations

### View Logs

```bash
# Stream live logs
az containerapp logs show \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --follow

# Query Log Analytics
az monitor log-analytics query \
  --workspace $(az monitor log-analytics workspace show \
    --resource-group $RESOURCE_GROUP \
    --workspace-name protogate-dev-logs \
    --query customerId -o tsv) \
  --analytics-query "ContainerAppConsoleLogs_CL | where TimeGenerated > ago(1h) | take 100"
```

### View Metrics

```bash
# Container App metrics
az monitor metrics list \
  --resource $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --resource-type "Microsoft.App/containerApps" \
  --metric "Requests" \
  --start-time $(date -u -d '1 hour ago' '+%Y-%m-%dT%H:%M:%SZ') \
  --interval PT1M
```

### Scale Manually

```bash
# Scale to specific replica count
az containerapp update \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --min-replicas 2 \
  --max-replicas 10
```

---

## 🔧 Troubleshooting

### Container App Not Starting

```bash
# Check revision status
az containerapp revision list \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --output table

# Get detailed logs
az containerapp logs show \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --tail 100
```

### Image Pull Failures

```bash
# Verify ACR access
az acr check-health --name $ACR_NAME

# Check managed identity permissions
az role assignment list \
  --assignee $(az containerapp show \
    --name $APP_NAME \
    --resource-group $RESOURCE_GROUP \
    --query "identity.principalId" -o tsv) \
  --output table
```

### DNS Not Resolving

```bash
# Check DNS zone
az network dns zone show \
  --resource-group $RESOURCE_GROUP \
  --name tunnel.yourdomain.com

# Test DNS resolution
dig @8.8.8.8 tunnel.yourdomain.com NS
dig @8.8.8.8 api.tunnel.yourdomain.com A
```

---

## 💰 Cost Optimization

### Development Environment

```json
{
  "minReplicas": 0,  // Scale to zero when idle
  "maxReplicas": 2,
  "cpu": "0.25",     // Smallest CPU
  "memory": "0.5Gi"  // Smallest memory
}
```

**Estimated cost**: $3-6/month

### Production Environment

```json
{
  "minReplicas": 2,   // High availability
  "maxReplicas": 10,
  "cpu": "1.0",
  "memory": "2Gi"
}
```

**Estimated cost**: $50-150/month

---

## 🔄 Updates and CI/CD

### Manual Update

```bash
# Build new version
docker build -f docker/Dockerfile.alpine -t $ACR_LOGIN_SERVER/protogate:v1.1.0 .
docker push $ACR_LOGIN_SERVER/protogate:v1.1.0

# Update app
az containerapp update \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --image $ACR_LOGIN_SERVER/protogate:v1.1.0
```

### GitHub Actions (Automated)

The `.github/workflows/ci.yml` already includes build/test. To add deployment:

1. Add Azure credentials to GitHub Secrets:
   - `AZURE_CREDENTIALS` (Service Principal JSON)
   - `ACR_LOGIN_SERVER`
   - `ACR_USERNAME`
   - `ACR_PASSWORD`

2. Workflow automatically deploys on push to `main` branch

---

## 🧹 Cleanup

### Delete Everything

```bash
# Warning: This deletes ALL resources
az group delete \
  --name $RESOURCE_GROUP \
  --yes \
  --no-wait
```

### Keep Infrastructure, Remove App

```bash
az containerapp delete \
  --name $APP_NAME \
  --resource-group $RESOURCE_GROUP \
  --yes
```

---

## 📚 Next Steps

1. ✅ **Deploy to Azure** (you are here)
2. ⏭️ **Test with real domain** - Verify custom domain and TLS
3. ⏭️ **Load testing** - Run performance tests with `wrk` or `ab`
4. ⏭️ **Production hardening**:
   - Complete OpenTelemetry integration
   - Wire Management API to HTTP server
   - Add rate limiting
5. ⏭️ **Documentation** - Create user guides and API docs

---

## ❓ Getting Help

- **Azure Issues**: Check Container Apps logs and Application Insights
- **Build Issues**: Verify vcpkg dependencies and CMake configuration
- **DNS Issues**: Validate nameserver delegation or CNAME records
- **Certificate Issues**: Use Azure-managed certificates or Let's Encrypt

**Support Channels**:
- GitHub Issues: https://github.com/ormasoftchile/protogate/issues
- Azure Docs: https://aka.ms/containerapps
