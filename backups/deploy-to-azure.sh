#!/bin/bash
# deploy-to-azure.sh - Quick deployment script for Protogate
# Usage: ./deploy-to-azure.sh [dev|staging|prod] [domain]

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
ENVIRONMENT=${1:-dev}
DOMAIN=${2}
RESOURCE_GROUP="protogate-${ENVIRONMENT}-rg"
LOCATION="eastus"
DEPLOYMENT_NAME="protogate-$(date +%Y%m%d-%H%M%S)"

echo -e "${GREEN}🚀 Protogate Azure Deployment${NC}"
echo "=================================="
echo "Environment: $ENVIRONMENT"
echo "Resource Group: $RESOURCE_GROUP"
echo "Location: $LOCATION"
echo ""

# Check if logged in to Azure
if ! az account show &>/dev/null; then
    echo -e "${RED}❌ Not logged in to Azure${NC}"
    echo "Run: az login"
    exit 1
fi

# Check domain
if [ -z "$DOMAIN" ]; then
    echo -e "${YELLOW}⚠️  No domain specified. Using auto-generated Container App domain.${NC}"
    DOMAIN="auto-generated.azurecontainerapps.io"
else
    echo "Domain: $DOMAIN"
fi

# Confirm
echo ""
read -p "Continue with deployment? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Deployment cancelled"
    exit 0
fi

# Step 1: Create resource group
echo ""
echo -e "${GREEN}📦 Step 1/5: Creating resource group...${NC}"
if az group show --name "$RESOURCE_GROUP" &>/dev/null; then
    echo "Resource group already exists"
else
    az group create \
        --name "$RESOURCE_GROUP" \
        --location "$LOCATION" \
        --tags Environment=$ENVIRONMENT Project=Protogate
    echo "✓ Resource group created"
fi

# Step 2: Validate Bicep template
echo ""
echo -e "${GREEN}🔍 Step 2/5: Validating Bicep template...${NC}"
cd deploy/azure
az deployment group validate \
    --resource-group "$RESOURCE_GROUP" \
    --template-file main.bicep \
    --parameters "parameters.${ENVIRONMENT}.json" \
    --parameters dnsDomainName="$DOMAIN"
echo "✓ Template validated"

# Step 3: Deploy infrastructure
echo ""
echo -e "${GREEN}☁️  Step 3/5: Deploying Azure infrastructure...${NC}"
echo "This will take 5-10 minutes..."
az deployment group create \
    --resource-group "$RESOURCE_GROUP" \
    --template-file main.bicep \
    --parameters "parameters.${ENVIRONMENT}.json" \
    --parameters dnsDomainName="$DOMAIN" \
    --name "$DEPLOYMENT_NAME" \
    --verbose

echo "✓ Infrastructure deployed"

# Step 4: Get deployment outputs
echo ""
echo -e "${GREEN}📋 Step 4/5: Retrieving deployment information...${NC}"
cd ../..

APP_NAME=$(az deployment group show \
    --resource-group "$RESOURCE_GROUP" \
    --name "$DEPLOYMENT_NAME" \
    --query "properties.outputs.containerAppName.value" -o tsv)

APP_FQDN=$(az deployment group show \
    --resource-group "$RESOURCE_GROUP" \
    --name "$DEPLOYMENT_NAME" \
    --query "properties.outputs.containerAppFqdn.value" -o tsv)

KV_NAME=$(az deployment group show \
    --resource-group "$RESOURCE_GROUP" \
    --name "$DEPLOYMENT_NAME" \
    --query "properties.outputs.keyVaultName.value" -o tsv)

DNS_ZONE=$(az deployment group show \
    --resource-group "$RESOURCE_GROUP" \
    --name "$DEPLOYMENT_NAME" \
    --query "properties.outputs.dnsZoneName.value" -o tsv)

echo "✓ Deployment outputs retrieved"
echo ""
echo "Container App: $APP_NAME"
echo "FQDN: $APP_FQDN"
echo "Key Vault: $KV_NAME"
echo "DNS Zone: $DNS_ZONE"

# Step 5: Check if ACR exists
echo ""
echo -e "${GREEN}🐳 Step 5/5: Checking Azure Container Registry...${NC}"
ACR_NAME=$(az acr list --resource-group "$RESOURCE_GROUP" --query "[0].name" -o tsv || echo "")

if [ -z "$ACR_NAME" ]; then
    echo -e "${YELLOW}⚠️  No ACR found. Creating...${NC}"
    ACR_NAME="protogate${ENVIRONMENT}acr"
    az acr create \
        --resource-group "$RESOURCE_GROUP" \
        --name "$ACR_NAME" \
        --sku Basic \
        --admin-enabled true
    echo "✓ ACR created: $ACR_NAME"
else
    echo "✓ ACR exists: $ACR_NAME"
fi

ACR_LOGIN_SERVER=$(az acr show --name "$ACR_NAME" --query loginServer -o tsv)

# Summary
echo ""
echo -e "${GREEN}✅ Deployment Complete!${NC}"
echo "=================================="
echo ""
echo "📝 Next Steps:"
echo ""
echo "1. Build and push Docker image:"
echo "   az acr login --name $ACR_NAME"
echo "   docker build -f docker/Dockerfile.alpine -t $ACR_LOGIN_SERVER/protogate:latest ."
echo "   docker push $ACR_LOGIN_SERVER/protogate:latest"
echo ""
echo "2. Update Container App:"
echo "   az containerapp update \\"
echo "     --name $APP_NAME \\"
echo "     --resource-group $RESOURCE_GROUP \\"
echo "     --image $ACR_LOGIN_SERVER/protogate:latest"
echo ""
echo "3. Create tunnel token:"
echo "   az keyvault secret set \\"
echo "     --vault-name $KV_NAME \\"
echo "     --name tunnel-token-test-api \\"
echo "     --value \"tnl_production_\$(openssl rand -hex 32)\""
echo ""
echo "4. Test health endpoint:"
echo "   curl https://$APP_FQDN/health"
echo ""
echo "5. Configure DNS (if using custom domain):"
echo "   - Delegate DNS to Azure or add CNAME record"
echo "   - See DEPLOY_AZURE.md for details"
echo ""
echo -e "${GREEN}🎉 Happy tunneling!${NC}"
