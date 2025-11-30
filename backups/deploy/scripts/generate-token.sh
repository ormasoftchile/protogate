#!/usr/bin/env bash

# generate-token.sh
# Generates a cryptographically secure tunnel token and stores it in Azure Key Vault
# Usage: ./generate-token.sh <key-vault-name> [tunnel-id]

set -euo pipefail

# ========================================
# Configuration
# ========================================

KEY_VAULT_NAME="${1:-}"
TUNNEL_ID="${2:-default}"
TOKEN_LENGTH=32

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

if [[ -z "$KEY_VAULT_NAME" ]]; then
    error "Usage: $0 <key-vault-name> [tunnel-id]"
    error "Example: $0 protogate-prod-kv my-tunnel"
    exit 1
fi

# Check Azure CLI
if ! command -v az &> /dev/null; then
    error "Azure CLI not found. Please install: https://docs.microsoft.com/en-us/cli/azure/install-azure-cli"
    exit 1
fi

# Check OpenSSL
if ! command -v openssl &> /dev/null; then
    error "OpenSSL not found. Please install OpenSSL."
    exit 1
fi

# Check Azure login
if ! az account show &> /dev/null; then
    error "Not logged in to Azure. Run 'az login' first."
    exit 1
fi

# Check Key Vault exists
if ! az keyvault show --name "$KEY_VAULT_NAME" &> /dev/null; then
    error "Key Vault not found: $KEY_VAULT_NAME"
    exit 1
fi

# ========================================
# Generate Token
# ========================================

info "Generating cryptographically secure token..."

# Generate random bytes and encode as base64
TOKEN=$(openssl rand -base64 "$TOKEN_LENGTH" | tr -d '\n')

success "Token generated (${#TOKEN} characters)"

# ========================================
# Store in Key Vault
# ========================================

SECRET_NAME="tunnel-token-${TUNNEL_ID}"

info "Storing token in Key Vault: $KEY_VAULT_NAME..."
info "Secret name: $SECRET_NAME"

az keyvault secret set \
    --vault-name "$KEY_VAULT_NAME" \
    --name "$SECRET_NAME" \
    --value "$TOKEN" \
    --output none

success "Token stored in Key Vault"

# ========================================
# Output Results
# ========================================

echo ""
echo "=========================================="
echo "Token Generated"
echo "=========================================="
echo "Tunnel ID:       $TUNNEL_ID"
echo "Secret Name:     $SECRET_NAME"
echo "Key Vault:       $KEY_VAULT_NAME"
echo ""
echo "Token (save securely):"
echo "$TOKEN"
echo ""
echo "=========================================="
echo ""

warn "⚠️  IMPORTANT: Save this token securely!"
warn "   This is the only time it will be displayed in plain text."
warn "   You'll need this token to connect tunnel agents."
echo ""

info "To retrieve the token later (requires Key Vault access):"
echo "  az keyvault secret show --vault-name $KEY_VAULT_NAME --name $SECRET_NAME --query value -o tsv"
echo ""

info "To use with curl:"
echo "  curl -H \"Authorization: Bearer $TOKEN\" https://your-tunnel-domain.com/api/v1/tunnels"
echo ""

success "Token generation complete! 🔑"
