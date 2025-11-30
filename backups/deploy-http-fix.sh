#!/bin/bash
set -e

COMMIT_SHA=$(git rev-parse --short HEAD)
IMAGE_TAG="v1.0.0-${COMMIT_SHA}"
ACR_NAME="protogatedevacr"
APP_NAME="protogate-dev-app"
RG="protogate-dev-rg"

echo "=== Deploying HTTP Fix to Azure Container Apps ==="
echo "Commit: ${COMMIT_SHA}"
echo "Image Tag: ${IMAGE_TAG}"
echo ""

echo "Step 1: Trigger GitHub Actions build..."
gh workflow run build-docker.yml --ref 001-tunnel-core-server
sleep 5
RUN_ID=$(gh run list --workflow=build-docker.yml --limit 1 --json databaseId --jq '.[0].databaseId')
echo "Build started: Run ID ${RUN_ID}"
echo "Waiting for build to complete..."
gh run watch ${RUN_ID}

echo ""
echo "Step 2: Tag image with unique identifier..."
az acr import \
  --name ${ACR_NAME} \
  --source ${ACR_NAME}.azurecr.io/protogate:v1.0.0 \
  --image protogate:${IMAGE_TAG} \
  --force

echo ""
echo "Step 3: Deploy to Container App..."
az containerapp update \
  --name ${APP_NAME} \
  --resource-group ${RG} \
  --image ${ACR_NAME}.azurecr.io/protogate:${IMAGE_TAG}

echo ""
echo "Step 4: Wait for health check..."
sleep 30

LATEST_REV=$(az containerapp show --name ${APP_NAME} --resource-group ${RG} --query "properties.latestRevisionName" -o tsv)
echo "Latest revision: ${LATEST_REV}"

HEALTH=$(az containerapp revision show --name ${APP_NAME} --resource-group ${RG} --revision ${LATEST_REV} --query "properties.healthState" -o tsv)
echo "Health state: ${HEALTH}"

if [ "${HEALTH}" = "Healthy" ]; then
  echo ""
  echo "✓ Deployment successful!"
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
  else
    echo ""
    echo "✗ Unexpected status code: ${HTTP_CODE}"
    exit 1
  fi
else
  echo ""
  echo "✗ Deployment failed - revision not healthy"
  exit 1
fi
