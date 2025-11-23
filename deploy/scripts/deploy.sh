#!/usr/bin/env bash

# deploy.sh
# Automated deployment script for Protogate Core Server to Azure Container Apps
# Usage: ./deploy.sh [dev|staging|prod] [resource-group-name]

set -euo pipefail

# ========================================
# Configuration
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AZURE_DIR="$(dirname "$SCRIPT_DIR")/azure"
ENVIRONMENT="${1:-dev}"
RESOURCE_GROUP="${2:-protogate-${ENVIRONMENT}-rg}"
LOCATION="${3:-eastus}"

# Bicep files
MAIN_BICEP="$AZURE_DIR/main.bicep"
PARAMS_FILE="$AZURE_DIR/parameters.${ENVIRONMENT}.json"

# ========================================
# Color output
# ========================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ========================================
# Validation
# ========================================

info "Starting deployment validation..."

# Check environment parameter
if [[ ! "$ENVIRONMENT" =~ ^(dev|staging|prod)$ ]]; then
    error "Invalid environment: $ENVIRONMENT. Must be one of: dev, staging, prod"
    exit 1
fi

# Check Azure CLI
if ! command -v az &> /dev/null; then
    error "Azure CLI not found. Please install: https://docs.microsoft.com/en-us/cli/azure/install-azure-cli"
    exit 1
fi

# Check Bicep template
if [[ ! -f "$MAIN_BICEP" ]]; then
    error "Main Bicep template not found: $MAIN_BICEP"
    exit 1
fi

# Check parameters file
if [[ ! -f "$PARAMS_FILE" ]]; then
    error "Parameters file not found: $PARAMS_FILE"
    exit 1
fi

success "Validation complete"

# ========================================
# Azure Login
# ========================================

info "Checking Azure login status..."

if ! az account show &> /dev/null; then
    warn "Not logged in to Azure. Starting login flow..."
    az login
else
    ACCOUNT_NAME=$(az account show --query "name" -o tsv)
    SUBSCRIPTION_ID=$(az account show --query "id" -o tsv)
    info "Logged in as: $ACCOUNT_NAME"
    info "Subscription: $SUBSCRIPTION_ID"
fi

# ========================================
# Resource Group
# ========================================

info "Checking resource group: $RESOURCE_GROUP..."

if az group show --name "$RESOURCE_GROUP" &> /dev/null; then
    info "Resource group already exists"
else
    info "Creating resource group: $RESOURCE_GROUP in $LOCATION..."
    az group create \
        --name "$RESOURCE_GROUP" \
        --location "$LOCATION" \
        --tags "Environment=$ENVIRONMENT" "Project=Protogate" "ManagedBy=deploy.sh"
    success "Resource group created"
fi

# ========================================
# Bicep Validation
# ========================================

info "Validating Bicep template..."

az deployment group validate \
    --resource-group "$RESOURCE_GROUP" \
    --template-file "$MAIN_BICEP" \
    --parameters "@$PARAMS_FILE" \
    --output none

success "Bicep template is valid"

# ========================================
# What-If Analysis
# ========================================

info "Running what-if analysis..."

az deployment group what-if \
    --resource-group "$RESOURCE_GROUP" \
    --template-file "$MAIN_BICEP" \
    --parameters "@$PARAMS_FILE" \
    --result-format ResourceIdOnly

# ========================================
# Deployment Confirmation
# ========================================

echo ""
warn "About to deploy to:"
warn "  Environment: $ENVIRONMENT"
warn "  Resource Group: $RESOURCE_GROUP"
warn "  Location: $LOCATION"
echo ""

read -p "Continue with deployment? (yes/no): " -r REPLY
echo ""

if [[ ! "$REPLY" =~ ^[Yy][Ee][Ss]$ ]]; then
    warn "Deployment cancelled by user"
    exit 0
fi

# ========================================
# Deployment
# ========================================

info "Starting deployment..."

DEPLOYMENT_NAME="protogate-${ENVIRONMENT}-$(date +%Y%m%d-%H%M%S)"

az deployment group create \
    --resource-group "$RESOURCE_GROUP" \
    --template-file "$MAIN_BICEP" \
    --parameters "@$PARAMS_FILE" \
    --name "$DEPLOYMENT_NAME" \
    --output json > "/tmp/${DEPLOYMENT_NAME}.json"

success "Deployment complete!"

# ========================================
# Output Results
# ========================================

info "Deployment outputs:"

CONTAINER_APP_URL=$(jq -r '.properties.outputs.containerAppUrl.value' "/tmp/${DEPLOYMENT_NAME}.json")
HEALTH_CHECK_URL=$(jq -r '.properties.outputs.healthCheckUrl.value' "/tmp/${DEPLOYMENT_NAME}.json")
KEY_VAULT_NAME=$(jq -r '.properties.outputs.keyVaultName.value' "/tmp/${DEPLOYMENT_NAME}.json")
DNS_NAME_SERVERS=$(jq -r '.properties.outputs.dnsNameServers.value | join(", ")' "/tmp/${DEPLOYMENT_NAME}.json")

echo ""
echo "=========================================="
echo "Deployment Summary"
echo "=========================================="
echo "Environment:        $ENVIRONMENT"
echo "Resource Group:     $RESOURCE_GROUP"
echo "Deployment Name:    $DEPLOYMENT_NAME"
echo ""
echo "Container App URL:  $CONTAINER_APP_URL"
echo "Health Check URL:   $HEALTH_CHECK_URL"
echo "Key Vault:          $KEY_VAULT_NAME"
echo ""
echo "DNS Name Servers:"
echo "$DNS_NAME_SERVERS" | tr ',' '\n' | sed 's/^/  - /'
echo ""
echo "=========================================="
echo ""

info "Next steps:"
echo "  1. Update your domain's NS records to point to the Azure DNS name servers above"
echo "  2. Generate and store a tunnel token:"
echo "     ./deploy/scripts/generate-token.sh $KEY_VAULT_NAME"
echo "  3. Test the health endpoint:"
echo "     curl $HEALTH_CHECK_URL"
echo ""

success "Deployment complete! 🚀"
