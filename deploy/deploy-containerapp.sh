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
