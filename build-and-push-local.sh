#!/bin/bash
set -e

echo "=== Local Build and Deploy to Azure ==="
echo ""

# Configuration
ACR_NAME="protogatedevacr"
APP_NAME="protogate-dev-app"
RG="protogate-dev-rg"
COMMIT_SHA=$(git rev-parse --short HEAD)
TIMESTAMP=$(date +%s)
IMAGE_TAG="v1.0.0-${COMMIT_SHA}-${TIMESTAMP}"

echo "Commit: ${COMMIT_SHA}"
echo "Image Tag: ${IMAGE_TAG}"
echo ""

# Step 1: Login to ACR
echo "Step 1: Logging into ACR..."
az acr login --name ${ACR_NAME}

# Step 2: Build image locally for AMD64
echo ""
echo "Step 2: Building Docker image for linux/amd64..."
docker buildx build \
  --platform linux/amd64 \
  --build-arg CACHEBUST=${TIMESTAMP} \
  --no-cache \
  -f docker/Dockerfile.alpine \
  -t ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG} \
  --push \
  .

echo "Build and push complete!"

# Step 3: Deploy to Container App
echo ""
echo "Step 3: Deploying to Container App..."
az containerapp update \
  --name ${APP_NAME} \
  --resource-group ${RG} \
  --image ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG}

# Step 4: Wait and check health
echo ""
echo "Step 4: Waiting for deployment..."
sleep 30

LATEST_REV=$(az containerapp show --name ${APP_NAME} --resource-group ${RG} --query "properties.latestRevisionName" -o tsv)
echo "Latest revision: ${LATEST_REV}"

HEALTH=$(az containerapp revision show --name ${APP_NAME} --resource-group ${RG} --revision ${LATEST_REV} --query "properties.healthState" -o tsv)
echo "Health state: ${HEALTH}"

if [ "${HEALTH}" = "Healthy" ]; then
  echo ""
  echo "✓ Deployment successful!"
  echo ""
  echo "Checking logs for new code..."
  sleep 5
  az containerapp logs show --name ${APP_NAME} --resource-group ${RG} --tail 50 --follow=false | grep -E "HTTPServer initialized|tls:" | head -3 || echo "No HTTPServer init logs yet"
  
  echo ""
  echo "Testing endpoint..."
  RESPONSE=$(curl -s -w "\nHTTP_CODE:%{http_code}" https://protogate-dev-app.victorioussmoke-2a30f7aa.eastus.azurecontainerapps.io/health)
  HTTP_CODE=$(echo "${RESPONSE}" | grep HTTP_CODE | cut -d: -f2)
  BODY=$(echo "${RESPONSE}" | grep -v HTTP_CODE)
  
  echo "Response: ${BODY}"
  echo "HTTP Status: ${HTTP_CODE}"
  
  if [ "${HTTP_CODE}" = "200" ]; then
    echo ""
    echo "✓✓✓ SUCCESS! Port 443 HTTP is working!"
    echo ""
    echo "Deployment complete with image: ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG}"
  else
    echo ""
    echo "Note: HTTP Status ${HTTP_CODE} - checking logs..."
    az containerapp logs show --name ${APP_NAME} --resource-group ${RG} --tail 30 --follow=false | grep -v "check request\|check response" | tail -15
  fi
else
  echo ""
  echo "✗ Deployment failed - revision not healthy"
  exit 1
fi
