#!/bin/bash

#
# Teardown Test Environment
#
# This script removes all Azure resources created for the test environment:
# - Container Apps (server)
# - Container Apps environment
# - Key Vault
# - DNS records
# - Resource group (optional)
#
# Usage:
#   ./scripts/teardown-test-env.sh [--keep-rg] [--keep-dns] [--dry-run]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Source common utilities
# shellcheck source=scripts/common.sh
source "$SCRIPT_DIR/common.sh"

# Default values
ENVIRONMENT=""
KEEP_RESOURCE_GROUP=false
KEEP_DNS=false
DRY_RUN=false
VERBOSE=false

# Azure resource names (will be set based on environment)
RESOURCE_GROUP=""
CONTAINER_APP_NAME=""
CONTAINER_ENV_NAME=""
KEYVAULT_NAME=""
DNS_ZONE=""
DNS_PREFIX=""

usage() {
    cat << EOF
Usage: $0 --env ENV [OPTIONS]

Teardown Azure environment resources.

Required Arguments:
    --env ENV           Environment name (dev/test/prod)

Options:
    --keep-rg           Keep the resource group (only delete individual resources)
    --keep-dns          Keep DNS records (don't delete A/TXT records)
    --dry-run           Show what would be deleted without making changes
    --verbose, -v       Enable verbose logging
    --help, -h          Show this help message

Examples:
    # Delete dev environment
    $0 --env dev

    # Delete test environment (dry run)
    $0 --env test --dry-run

    # Delete resources but keep resource group
    $0 --env test --keep-rg

    # Keep DNS records (useful if shared with other services)
    $0 --env test --keep-dns

Warning:
    This is a destructive operation. All data in the environment will be lost.
    - Container Apps and logs will be deleted
    - Key Vault and all secrets/certificates will be deleted (soft-delete may retain for 90 days)
    - DNS records will be removed (if not --keep-dns)

EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --env)
            ENVIRONMENT="$2"
            shift 2
            ;;
        --keep-rg)
            KEEP_RESOURCE_GROUP=true
            shift
            ;;
        --keep-dns)
            KEEP_DNS=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            usage
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            ;;
    esac
done

# Validate environment
if [ -z "$ENVIRONMENT" ]; then
    log_error "Environment is required. Use --env dev|test|prod"
    usage
fi

# Set resource names based on environment
case "$ENVIRONMENT" in
    dev)
        RESOURCE_GROUP="protogate-dev-rg"
        CONTAINER_APP_NAME="protogate-dev-app"
        CONTAINER_ENV_NAME="protogate-dev-env"
        KEYVAULT_NAME="protogatedevkv"
        DNS_ZONE="tunnel-dev.example.com"
        DNS_PREFIX=""  # Root zone
        ;;
    test)
        RESOURCE_GROUP="protogate-test-rg"
        CONTAINER_APP_NAME="protogate-test-server"
        CONTAINER_ENV_NAME="protogate-test-env"
        KEYVAULT_NAME="pg-test-kv-098f6b"
        DNS_ZONE="ormasoft.cl"
        DNS_PREFIX="test.tunnel"
        ;;
    prod)
        RESOURCE_GROUP="protogate-prod-rg"
        CONTAINER_APP_NAME="protogate-prod-server"
        CONTAINER_ENV_NAME="protogate-prod-env"
        KEYVAULT_NAME="pg-prod-kv-098f6b"
        DNS_ZONE="ormasoft.cl"
        DNS_PREFIX="tunnel"
        ;;
    *)
        log_error "Invalid environment: $ENVIRONMENT (must be dev, test, or prod)"
        exit 1
        ;;
esac

if [ "$VERBOSE" = true ]; then
    set -x
fi

# Confirmation prompt
confirm_teardown() {
    log_warn "================================================"
    log_warn "WARNING: DESTRUCTIVE OPERATION"
    log_warn "================================================"
    log_warn "This will DELETE the following resources:"
    log_warn "  - Resource Group: $RESOURCE_GROUP"
    log_warn "  - Container App: $CONTAINER_APP_NAME"
    log_warn "  - Container Environment: $CONTAINER_ENV_NAME"
    log_warn "  - Key Vault: $KEYVAULT_NAME"
    
    if [ "$KEEP_DNS" = false ]; then
        log_warn "  - DNS Records: ${DNS_PREFIX}.${DNS_ZONE}, *.${DNS_PREFIX}.${DNS_ZONE}"
    fi
    
    if [ "$KEEP_RESOURCE_GROUP" = true ]; then
        log_info "Resource group will be KEPT (--keep-rg)"
    else
        log_warn "  - Resource Group will be DELETED (all resources inside)"
    fi
    
    log_warn ""
    log_warn "This action CANNOT be undone!"
    log_warn ""
    
    if [ "$DRY_RUN" = true ]; then
        log_info "DRY RUN MODE - No changes will be made"
        return 0
    fi
    
    read -p "Type 'DELETE' to confirm teardown: " confirmation
    
    if [ "$confirmation" != "DELETE" ]; then
        log_info "Teardown cancelled"
        exit 0
    fi
}

# Function: Delete DNS records
delete_dns_records() {
    if [ "$KEEP_DNS" = true ]; then
        log_info "Skipping DNS deletion (--keep-dns)"
        return 0
    fi
    
    log_info "Step 1: Deleting DNS records"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would delete DNS records"
        return 0
    fi
    
    # For dev environment, delete the entire DNS zone
    if [ "$ENVIRONMENT" = "dev" ]; then
        if az network dns zone show \
            --name "$DNS_ZONE" \
            --resource-group "$RESOURCE_GROUP" \
            --output none 2>/dev/null; then
            
            log_info "Deleting DNS zone: $DNS_ZONE"
            if az network dns zone delete \
                --name "$DNS_ZONE" \
                --resource-group "$RESOURCE_GROUP" \
                --yes \
                --output none 2>/dev/null; then
                log_success "Deleted DNS zone: $DNS_ZONE"
            else
                log_warn "Failed to delete DNS zone: $DNS_ZONE"
            fi
        else
            log_info "DNS zone not found (already deleted)"
        fi
        return 0
    fi
    
    # For test/prod: delete specific records from shared zone
    local dns_rg=$(az network dns zone list --query "[?name=='$DNS_ZONE'].resourceGroup" -o tsv | head -n 1)
    
    if [ -z "$dns_rg" ]; then
        log_warn "DNS zone not found: $DNS_ZONE"
        return 0
    fi
    
    # Delete A records
    for record in "$DNS_PREFIX" "*.${DNS_PREFIX}"; do
        if az network dns record-set a delete \
            --resource-group "$dns_rg" \
            --zone-name "$DNS_ZONE" \
            --name "$record" \
            --yes \
            --output none 2>/dev/null; then
            log_success "Deleted A record: $record"
        else
            log_warn "Failed to delete A record: $record (may not exist)"
        fi
    done
    
    # Delete TXT validation record
    local txt_record="asuid.${DNS_PREFIX}"
    if az network dns record-set txt delete \
        --resource-group "$dns_rg" \
        --zone-name "$DNS_ZONE" \
        --name "$txt_record" \
        --yes \
        --output none 2>/dev/null; then
        log_success "Deleted TXT record: $txt_record"
    else
        log_warn "Failed to delete TXT record: $txt_record (may not exist)"
    fi
    
    # Delete ACME challenge records (cleanup)
    local acme_record="_acme-challenge.${DNS_PREFIX}"
    az network dns record-set txt delete \
        --resource-group "$dns_rg" \
        --zone-name "$DNS_ZONE" \
        --name "$acme_record" \
        --yes \
        --output none 2>/dev/null || true
}

# Function: Delete Container App
delete_container_app() {
    log_info "Step 2: Deleting Container App"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would delete Container App: $CONTAINER_APP_NAME"
        return 0
    fi
    
    if az containerapp show \
        --name "$CONTAINER_APP_NAME" \
        --resource-group "$RESOURCE_GROUP" \
        --output none 2>/dev/null; then
        
        log_info "Deleting Container App: $CONTAINER_APP_NAME"
        
        if az containerapp delete \
            --name "$CONTAINER_APP_NAME" \
            --resource-group "$RESOURCE_GROUP" \
            --yes \
            --output none 2>&1; then
            log_success "Container App deleted"
        else
            log_error "Failed to delete Container App"
            return 1
        fi
    else
        log_info "Container App not found (already deleted)"
    fi
}

# Function: Delete Container Apps Environment
delete_container_env() {
    log_info "Step 3: Deleting Container Apps Environment"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would delete Container Environment: $CONTAINER_ENV_NAME"
        return 0
    fi
    
    if az containerapp env show \
        --name "$CONTAINER_ENV_NAME" \
        --resource-group "$RESOURCE_GROUP" \
        --output none 2>/dev/null; then
        
        log_info "Deleting Container Environment: $CONTAINER_ENV_NAME"
        
        if az containerapp env delete \
            --name "$CONTAINER_ENV_NAME" \
            --resource-group "$RESOURCE_GROUP" \
            --yes \
            --output none 2>&1; then
            log_success "Container Environment deleted"
        else
            log_error "Failed to delete Container Environment"
            return 1
        fi
    else
        log_info "Container Environment not found (already deleted)"
    fi
}

# Function: Delete Key Vault
delete_keyvault() {
    log_info "Step 4: Deleting Key Vault"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would delete Key Vault: $KEYVAULT_NAME"
        return 0
    fi
    
    if az keyvault show \
        --name "$KEYVAULT_NAME" \
        --output none 2>/dev/null; then
        
        log_info "Deleting Key Vault: $KEYVAULT_NAME"
        
        if az keyvault delete \
            --name "$KEYVAULT_NAME" \
            --output none 2>&1; then
            log_success "Key Vault deleted (soft-delete enabled, recoverable for 90 days)"
            log_info "To permanently delete: az keyvault purge --name $KEYVAULT_NAME"
        else
            log_error "Failed to delete Key Vault"
            return 1
        fi
    else
        log_info "Key Vault not found (already deleted)"
    fi
}

# Function: Delete Resource Group
delete_resource_group() {
    if [ "$KEEP_RESOURCE_GROUP" = true ]; then
        log_info "Skipping Resource Group deletion (--keep-rg)"
        return 0
    fi
    
    log_info "Step 5: Deleting Resource Group"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would delete Resource Group: $RESOURCE_GROUP"
        return 0
    fi
    
    if az group show \
        --name "$RESOURCE_GROUP" \
        --output none 2>/dev/null; then
        
        log_info "Deleting Resource Group: $RESOURCE_GROUP"
        log_warn "This will delete ALL resources in the group..."
        
        if az group delete \
            --name "$RESOURCE_GROUP" \
            --yes \
            --no-wait \
            --output none 2>&1; then
            log_success "Resource Group deletion started (running in background)"
            log_info "Check status: az group show --name $RESOURCE_GROUP"
        else
            log_error "Failed to delete Resource Group"
            return 1
        fi
    else
        log_info "Resource Group not found (already deleted)"
    fi
}

# Function: Clean up local files
cleanup_local_files() {
    log_info "Step 6: Cleaning up local files"
    
    local files=(
        "$PROJECT_ROOT/azure/.keyvault-uri-${ENVIRONMENT}"
        "$PROJECT_ROOT/azure/.managed-identity-${ENVIRONMENT}"
        "$PROJECT_ROOT/azure/.container-app-url-${ENVIRONMENT}"
    )
    
    # Only clean up Let's Encrypt files for test/prod (not dev)
    if [ "$ENVIRONMENT" != "dev" ]; then
        files+=("$PROJECT_ROOT/azure/letsencrypt")
    fi
    
    for file in "${files[@]}"; do
        if [ -e "$file" ]; then
            if [ "$DRY_RUN" = true ]; then
                log_warn "[DRY RUN] Would delete: $file"
            else
                rm -rf "$file"
                log_info "Deleted: $file"
            fi
        fi
    done
    
    log_success "Local files cleaned up"
}

# Main execution
main() {
    log_info "================================================"
    log_info "Protogate Environment Teardown"
    log_info "================================================"
    log_info "Environment: $ENVIRONMENT"
    log_info "Resource Group: $RESOURCE_GROUP"
    log_info ""
    
    # Confirmation
    confirm_teardown
    
    log_info ""
    log_info "Starting teardown..."
    log_info ""
    
    # Execute teardown steps
    delete_dns_records || log_warn "DNS deletion had errors, continuing..."
    delete_container_app || log_warn "Container App deletion had errors, continuing..."
    delete_container_env || log_warn "Container Environment deletion had errors, continuing..."
    delete_keyvault || log_warn "Key Vault deletion had errors, continuing..."
    delete_resource_group || log_warn "Resource Group deletion had errors, continuing..."
    cleanup_local_files
    
    # Summary
    log_info ""
    log_info "================================================"
    log_success "Teardown Complete!"
    log_info "================================================"
    
    if [ "$KEEP_RESOURCE_GROUP" = false ]; then
        log_info "Resource Group deletion is running in background"
        log_info "Check status: az group show --name $RESOURCE_GROUP"
    fi
    
    log_info ""
    log_info "Deleted resources:"
    log_info "  ✓ Container App: $CONTAINER_APP_NAME"
    log_info "  ✓ Container Environment: $CONTAINER_ENV_NAME"
    log_info "  ✓ Key Vault: $KEYVAULT_NAME (soft-deleted)"
    
    if [ "$KEEP_DNS" = false ]; then
        if [ "$ENVIRONMENT" = "dev" ]; then
            log_info "  ✓ DNS Zone: $DNS_ZONE"
        else
            log_info "  ✓ DNS Records: ${DNS_PREFIX}.${DNS_ZONE}"
        fi
    fi
    
    if [ "$KEEP_RESOURCE_GROUP" = false ]; then
        log_info "  ✓ Resource Group: $RESOURCE_GROUP (deleting...)"
    fi
    
    log_info ""
    log_info "Notes:"
    log_info "  - Key Vault is soft-deleted (recoverable for 90 days)"
    log_info "  - To permanently delete: az keyvault purge --name $KEYVAULT_NAME"
    log_info "  - To recreate test environment: ./scripts/deploy-test-env.sh"
    log_info ""
}

# Run main function
main
