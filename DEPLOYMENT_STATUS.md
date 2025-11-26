# Protogate Azure Deployment Status

**Date**: November 25, 2025  
**Subscription**: fba68909-a61e-43fd-8bff-4dcdaa99c2d6 (Suscripción clubster)  
**Status**: Infrastructure Deployed ✅ | Image Building 🔄 | ACR Registration Pending ⏳

---

## ✅ Successfully Deployed Resources

| Resource | Name | Status | URL/Details |
|----------|------|--------|-------------|
| Resource Group | `protogate-dev-rg` | ✅ Ready | Location: eastus |
| Log Analytics | `protogate-dev-logs` | ✅ Ready | Workspace for structured logging |
| Key Vault | `protogatedevkv` | ✅ Ready | Secrets and certificates storage |
| DNS Zone | `tunnel-dev.example.com` | ✅ Ready | Custom domain DNS |
| DNS Zone | `protogate-dev.azurecontainerapps.io` | ✅ Ready | Auto-generated domain |
| Container Environment | `protogate-dev-env` | ✅ Ready | Managed environment for apps |
| Managed Identity | `protogate-dev-app-identity` | ✅ Ready | For ACR and Key Vault access |
| **Container App** | `protogate-dev-app` | ✅ Ready | **Currently running placeholder image** |

---

## 🌐 Access URLs

### Container App
```
https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io
```

### Health Endpoint (after real image deployed)
```
https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health
```

### Management API (after real image deployed)
```
https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/api/v1
```

---

## 🔄 In Progress

### 1. Docker Image Build
- **Status**: Building locally 🔄
- **Log**: `/tmp/docker-build.log`
- **Image Tags**: `protogate:v1.0.0`, `protogate:latest`
- **Check Progress**: `tail -f /tmp/docker-build.log`
- **ETA**: 10-15 minutes (first build)

### 2. ACR Provider Registration
- **Status**: Registering ⏳
- **Namespace**: `Microsoft.ContainerRegistry`
- **Check Status**: `az provider show --namespace Microsoft.ContainerRegistry --query "registrationState" -o tsv`
- **ETA**: 2-5 minutes

---

## 📋 Next Steps (Once ACR Registration Completes)

### Step 1: Create Azure Container Registry
```bash
az acr create \
  --resource-group protogate-dev-rg \
  --name protogatedevacr \
  --sku Basic \
  --admin-enabled false \
  --location eastus
```

### Step 2: Login to ACR
```bash
az acr login --name protogatedevacr
```

### Step 3: Tag and Push Docker Image
```bash
# Tag local image for ACR
docker tag protogate:v1.0.0 protogatedevacr.azurecr.io/protogate:v1.0.0
docker tag protogate:latest protogatedevacr.azurecr.io/protogate:latest

# Push to ACR
docker push protogatedevacr.azurecr.io/protogate:v1.0.0
docker push protogatedevacr.azurecr.io/protogate:latest
```

### Step 4: Grant Managed Identity Access to ACR
```bash
# Get managed identity principal ID
IDENTITY_ID=$(az identity show \
  --name protogate-dev-app-identity \
  --resource-group protogate-dev-rg \
  --query principalId -o tsv)

# Grant AcrPull role
az role assignment create \
  --assignee $IDENTITY_ID \
  --role AcrPull \
  --scope $(az acr show --name protogatedevacr --query id -o tsv)
```

### Step 5: Update Container App with Real Image
```bash
az containerapp update \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --image protogatedevacr.azurecr.io/protogate:v1.0.0 \
  --revision-suffix v1-0-0
```

### Step 6: Create Test Tunnel Token
```bash
# Generate secure token
TOKEN="tnl_production_$(openssl rand -hex 32)"

# Store in Key Vault
az keyvault secret set \
  --vault-name protogatedevkv \
  --name "tunnel-token-test-api" \
  --value "$TOKEN"

# Save token for agent configuration
echo "Token for test-api tunnel: $TOKEN"
```

### Step 7: Test Health Endpoint
```bash
curl https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health
```

Expected response:
```json
{
  "status": "healthy",
  "version": "1.0.0",
  "uptime": 60,
  "metrics": {
    "active_tunnels": 0,
    "active_http_connections": 0,
    "active_tcp_connections": 0
  }
}
```

---

## 🧪 Testing After Deployment

### Test Agent Connection (Local)
```bash
# Run tunnel-agent locally pointing to Azure
./tunnel-agent/build/tunnel-agent \
  --server-url https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io \
  --tunnel-id test-api \
  --token tnl_production_YOUR_TOKEN \
  --target-host localhost \
  --target-port 3000
```

### Test HTTP Tunnel
```bash
# Start local service
python3 -m http.server 3000 &

# Make request through tunnel (from anywhere)
curl https://test-api.tunnel-dev.example.com/
```

### Test TCP Tunnel
```bash
# Assuming TCP port 9100 is configured
echo "GET / HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n" | nc protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io 9100
```

---

## 📊 Monitoring

### View Logs
```bash
# Stream live logs
az containerapp logs show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --follow

# Query last 100 log entries
az monitor log-analytics query \
  --workspace $(az monitor log-analytics workspace show \
    --resource-group protogate-dev-rg \
    --workspace-name protogate-dev-logs \
    --query customerId -o tsv) \
  --analytics-query "ContainerAppConsoleLogs_CL | take 100"
```

### View Metrics
```bash
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "properties.template.scale"
```

---

## 💰 Cost Estimate

### Current Configuration
- **CPU**: 0.5 cores
- **Memory**: 1Gi
- **Min Replicas**: 0 (scales to zero)
- **Max Replicas**: 3

### Estimated Monthly Cost
- **Idle**: $0 (scaled to zero)
- **Light usage** (1-5 tunnels, <1000 requests/day): $3-6/month
- **Medium usage** (10-20 tunnels, <10k requests/day): $12-24/month

---

## 🔧 Troubleshooting

### Check ACR Registration Status
```bash
watch -n 5 "az provider show --namespace Microsoft.ContainerRegistry --query registrationState -o tsv"
```

### Check Docker Build Progress
```bash
tail -f /tmp/docker-build.log
```

### Check Container App Status
```bash
az containerapp show \
  --name protogate-dev-app \
  --resource-group protogate-dev-rg \
  --query "properties.{status:provisioningState,health:runningStatus,replicas:template.scale}" -o json
```

### Common Issues

#### 1. Container App Won't Start
- Check logs: `az containerapp logs show --name protogate-dev-app --resource-group protogate-dev-rg --tail 100`
- Verify image exists: `az acr repository show --name protogatedevacr --repository protogate`
- Check managed identity has AcrPull role

#### 2. Agent Can't Connect
- Verify tunnel token in Key Vault
- Check Container App ingress configuration
- Verify agent using correct FQDN
- Check firewall/network rules

#### 3. DNS Not Resolving
- Verify DNS zone nameservers: `az network dns zone show --resource-group protogate-dev-rg --name tunnel-dev.example.com --query nameServers`
- Check domain registrar has correct NS records
- DNS propagation can take up to 48 hours

---

## 🎉 What's Been Accomplished

1. ✅ Full Azure infrastructure deployed via Bicep
2. ✅ Container Apps environment configured
3. ✅ Managed identity created for secure access
4. ✅ Key Vault ready for secrets
5. ✅ DNS zones configured
6. ✅ Log Analytics workspace for observability
7. 🔄 Docker image building (in progress)
8. ⏳ ACR registration pending (will complete soon)

**Next**: Complete steps 1-7 above to deploy your Protogate image and start tunneling! 🚀
