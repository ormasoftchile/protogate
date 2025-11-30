#!/bin/bash
# Provision Azure Key Vault and upload TLS certificates

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

LOCATION="westus2"
CERT_FILE="./azure/tls-cert.pem"
KEY_FILE="./azure/tls-key.pem"

usage() {
    cat << EOF
Usage: $0 --env <test|prod> [options]

Provision Azure Key Vault and upload TLS certificates

Required:
    --env <test|prod>   Environment (test or prod)

Options:
    --dry-run           Show what would be done without executing
    --json              Output results in JSON format
    --verbose, -v       Enable verbose output
    --cert <file>       Certificate file (default: $CERT_FILE)
    --key <file>        Private key file (default: $KEY_FILE)
    --location <loc>    Azure region (default: $LOCATION)

Examples:
    $0 --env test
    $0 --env test --cert ./my-cert.pem --key ./my-key.pem
    $0 --env prod --location eastus
EOF
    exit 1
}

provision_resource_group() {
    local rg=$(get_resource_group)
    
    log_info "Ensuring resource group exists: $rg"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create resource group: $rg"
        return 0
    fi
    
    if az group show --name "$rg" &> /dev/null; then
        log_info "Resource group already exists: $rg"
    else
        log_info "Creating resource group: $rg"
        az group create --name "$rg" --location "$LOCATION" --output none || \
            error_exit "Failed to create resource group"
        log_success "Resource group created: $rg"
    fi
}

generate_keyvault_name() {
    local kv_name=$(get_keyvault_name)
    echo "$kv_name"
}

provision_keyvault() {
    local kv_name=$1
    local rg=$(get_resource_group)
    
    log_info "Provisioning Key Vault: $kv_name"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create Key Vault: $kv_name"
        return 0
    fi
    
    # Check if Key Vault already exists
    if az keyvault show --name "$kv_name" --resource-group "$rg" &> /dev/null; then
        log_info "Key Vault already exists: $kv_name"
    else
        log_info "Creating Key Vault: $kv_name"
        az keyvault create \
            --name "$kv_name" \
            --resource-group "$rg" \
            --location "$LOCATION" \
            --enable-rbac-authorization false \
            --output none || error_exit "Failed to create Key Vault"
        
        log_success "Key Vault created: $kv_name"
    fi
    
    # Save Key Vault URI
    local kv_uri=$(az keyvault show --name "$kv_name" --query properties.vaultUri -o tsv)
    echo "$kv_uri" > "$SCRIPT_DIR/../azure/.keyvault-uri-${ENVIRONMENT}"
    log_info "Key Vault URI: $kv_uri"
    json_output "keyvault_uri" "$kv_uri"
}

generate_certificate_if_needed() {
    if [ ! -f "$CERT_FILE" ] || [ ! -f "$KEY_FILE" ]; then
        log_warn "Certificate files not found, generating self-signed certificate..."
        
        if [ "$DRY_RUN" = true ]; then
            log_info "[DRY RUN] Would generate certificate"
            return 0
        fi
        
        "$SCRIPT_DIR/generate-test-cert.sh" --domain "*.${ENVIRONMENT}.tunnel.example.com" || \
            error_exit "Failed to generate certificate"
    else
        log_info "Using existing certificate files"
    fi
}

upload_certificate() {
    local kv_name=$1
    
    log_info "Uploading certificate to Key Vault..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would upload certificate to: $kv_name"
        return 0
    fi
    
    # Upload certificate as secret (PEM format)
    log_info "Uploading certificate..."
    az keyvault secret set \
        --vault-name "$kv_name" \
        --name "tls-cert" \
        --file "$CERT_FILE" \
        --output none || error_exit "Failed to upload certificate"
    
    log_success "Certificate uploaded: tls-cert"
    
    # Upload private key as secret
    log_info "Uploading private key..."
    az keyvault secret set \
        --vault-name "$kv_name" \
        --name "tls-key" \
        --file "$KEY_FILE" \
        --output none || error_exit "Failed to upload private key"
    
    log_success "Private key uploaded: tls-key"
}

configure_access_policy() {
    local kv_name=$1
    
    log_info "Configuring Key Vault access policies..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would configure access policies"
        return 0
    fi
    
    # Get current user object ID
    local user_id=$(az ad signed-in-user show --query id -o tsv 2>/dev/null || echo "")
    
    if [ -n "$user_id" ]; then
        log_info "Granting current user access to secrets..."
        az keyvault set-policy \
            --name "$kv_name" \
            --object-id "$user_id" \
            --secret-permissions get list set delete \
            --output none || log_warn "Failed to set access policy for user"
    fi
    
    log_info "Note: Container app managed identity access will be configured during deployment"
}

verify_secrets() {
    local kv_name=$1
    
    log_info "Verifying secrets in Key Vault..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would verify secrets"
        return 0
    fi
    
    # List secrets
    local secrets=$(az keyvault secret list --vault-name "$kv_name" --query "[].name" -o tsv)
    
    if echo "$secrets" | grep -q "tls-cert" && echo "$secrets" | grep -q "tls-key"; then
        log_success "Secrets verified: tls-cert, tls-key"
    else
        error_exit "Secrets not found in Key Vault"
    fi
}

main() {
    parse_args "$@"
    
    if [ $# -eq 0 ]; then
        usage
    fi
    
    # Parse additional options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --env)
                shift 2
                ;;
            --cert)
                CERT_FILE="$2"
                shift 2
                ;;
            --key)
                KEY_FILE="$2"
                shift 2
                ;;
            --location)
                LOCATION="$2"
                shift 2
                ;;
            --dry-run|--json|--verbose|-v)
                shift
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                ;;
        esac
    done
    
    validate_environment
    
    log_info "Starting Key Vault provisioning..."
    log_info "Environment: $ENVIRONMENT"
    log_info "Location: $LOCATION"
    
    # Validate prerequisites
    check_azure_cli
    check_azure_login
    
    # Provision resources
    provision_resource_group
    
    local kv_name=$(generate_keyvault_name)
    provision_keyvault "$kv_name"
    
    # Generate certificate if needed
    generate_certificate_if_needed
    
    # Upload certificate
    upload_certificate "$kv_name"
    
    # Configure access
    configure_access_policy "$kv_name"
    
    # Verify
    verify_secrets "$kv_name"
    
    # Summary
    log_info ""
    log_success "========================================="
    log_success "Key Vault provisioning completed!"
    log_success "========================================="
    log_info "Key Vault: $kv_name"
    log_info "URI saved to: azure/.keyvault-uri-${ENVIRONMENT}"
    log_info ""
    log_info "Next steps:"
    log_info "  1. Deploy test environment: ./scripts/deploy-test-env.sh --env $ENVIRONMENT"
    log_info "  2. Container app will automatically access certificates via managed identity"
}

main "$@"
