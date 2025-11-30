#!/bin/bash
# quick-deploy.sh - Fast deployment for Protogate
# Preconfigured with your Azure subscription

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Your configuration
SUBSCRIPTION_ID="fba68909-a61e-43fd-8bff-4dcdaa99c2d6"
ENVIRONMENT="dev"
RESOURCE_GROUP="protogate-dev-rg"
LOCATION="eastus"
DOMAIN="tunnel-dev.example.com"  # Change this to your domain or leave for auto-generated

echo -e "${GREEN}🚀 Protogate Quick Deploy${NC}"
echo "=========================="
echo ""

# Set subscription
echo "Setting Azure subscription..."
az account set --subscription "$SUBSCRIPTION_ID"
echo "✓ Using subscription: $(az account show --query name -o tsv)"
echo ""

# Deploy
echo -e "${GREEN}Deploying to Azure...${NC}"
./deploy-to-azure.sh "$ENVIRONMENT" "$DOMAIN"

echo ""
echo -e "${GREEN}✅ Quick deploy complete!${NC}"
echo ""
echo "Next: Follow the printed instructions to build and push your Docker image."
