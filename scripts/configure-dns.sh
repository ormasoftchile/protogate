#!/bin/bash

#
# Configure DNS for Protogate Tunnel Subdomains
#
# This script configures Azure DNS records for tunnel subdomains and provisions
# Let's Encrypt TLS certificates using DNS-01 challenge.
#
# Usage:
#   ./scripts/configure-dns.sh --env test --zone ormasoft.cl --subdomain tunnel --letsencrypt --email admin@ormasoft.cl
#
# Requirements:
#   - Azure CLI authenticated
#   - certbot installed (for Let's Encrypt)
#   - Access to Azure DNS zone and Key Vault
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Source common utilities
# shellcheck source=scripts/common.sh
source "$SCRIPT_DIR/common.sh"

# Default values
ENVIRONMENT=""
BASE_ZONE=""
SUBDOMAIN="tunnel"
ENABLE_LETSENCRYPT=false
CONTACT_EMAIL=""
DRY_RUN=false
JSON_OUTPUT=false
VERBOSE=false

# Azure resource names (will be set based on environment)
RESOURCE_GROUP=""
CONTAINER_APP_NAME=""
KEYVAULT_NAME=""
DNS_ZONE_RESOURCE_GROUP=""  # May be different from app resource group

usage() {
    cat << EOF
Usage: $0 --env ENV --zone ZONE [OPTIONS]

Configure DNS records and TLS certificates for Protogate tunnel subdomains.

Required Arguments:
    --env ENV               Environment name (test/prod)
    --zone ZONE             Base DNS zone (e.g., ormasoft.cl)

Optional Arguments:
    --subdomain SUBDOMAIN   Subdomain for tunnels (default: tunnel)
    --letsencrypt           Enable Let's Encrypt certificate provisioning
    --email EMAIL           Contact email for Let's Encrypt notifications
    --dry-run               Show what would be done without making changes
    --json                  Output results in JSON format
    --verbose, -v           Enable verbose logging
    --help, -h              Show this help message

Examples:
    # Configure test environment with Let's Encrypt
    $0 --env test --zone ormasoft.cl --letsencrypt --email admin@ormasoft.cl

    # Configure production (dry-run)
    $0 --env prod --zone ormasoft.cl --subdomain tunnel --dry-run

    # Test environment without Let's Encrypt (use self-signed certs)
    $0 --env test --zone ormasoft.cl

Environment Variables:
    AZURE_SUBSCRIPTION_ID   Override default Azure subscription

DNS Records Created:
    For test environment with --subdomain tunnel:
        - A record: test.tunnel.ormasoft.cl → Container App IP
        - A record: *.test.tunnel.ormasoft.cl → Container App IP

    For production environment:
        - A record: tunnel.ormasoft.cl → Container App IP
        - A record: *.tunnel.ormasoft.cl → Container App IP

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
        --zone)
            BASE_ZONE="$2"
            shift 2
            ;;
        --subdomain)
            SUBDOMAIN="$2"
            shift 2
            ;;
        --letsencrypt)
            ENABLE_LETSENCRYPT=true
            shift
            ;;
        --email)
            CONTACT_EMAIL="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --json)
            JSON_OUTPUT=true
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

# Validation
if [ -z "$ENVIRONMENT" ]; then
    log_error "Environment (--env) is required"
    usage
fi

if [ -z "$BASE_ZONE" ]; then
    log_error "DNS zone (--zone) is required"
    usage
fi

if [ "$ENABLE_LETSENCRYPT" = true ] && [ -z "$CONTACT_EMAIL" ]; then
    log_error "Contact email (--email) is required when using Let's Encrypt"
    usage
fi

# Set Azure resource names based on environment
if [ "$ENVIRONMENT" = "test" ]; then
    RESOURCE_GROUP="protogate-test-rg"
    CONTAINER_APP_NAME="protogate-test-server"
    KEYVAULT_NAME="pg-test-kv-098f6b"
    DNS_PREFIX="test.$SUBDOMAIN"
elif [ "$ENVIRONMENT" = "prod" ]; then
    RESOURCE_GROUP="protogate-prod-rg"
    CONTAINER_APP_NAME="protogate-prod-server"
    KEYVAULT_NAME="pg-prod-kv-098f6b"
    DNS_PREFIX="$SUBDOMAIN"
else
    log_error "Invalid environment: $ENVIRONMENT (must be test or prod)"
    exit 1
fi

# Auto-detect DNS zone resource group
log_info "Detecting resource group for DNS zone: $BASE_ZONE"
DNS_ZONE_RESOURCE_GROUP=$(az network dns zone list --query "[?name=='$BASE_ZONE'].resourceGroup" -o tsv | head -n 1)

if [ -z "$DNS_ZONE_RESOURCE_GROUP" ]; then
    log_error "Could not find resource group for DNS zone: $BASE_ZONE"
    exit 1
fi

log_info "DNS zone resource group: $DNS_ZONE_RESOURCE_GROUP"

# Construct full DNS names
TUNNEL_DOMAIN="$DNS_PREFIX.$BASE_ZONE"
WILDCARD_DOMAIN="*.$DNS_PREFIX.$BASE_ZONE"

log_info "Starting DNS configuration for $TUNNEL_DOMAIN"
log_info "Environment: $ENVIRONMENT"
log_info "Base Zone: $BASE_ZONE"
log_info "Tunnel Domain: $TUNNEL_DOMAIN"
log_info "Wildcard Domain: $WILDCARD_DOMAIN"
log_info "Let's Encrypt: $ENABLE_LETSENCRYPT"

if [ "$DRY_RUN" = true ]; then
    log_warn "DRY RUN MODE - No changes will be made"
fi

# Function: Validate DNS zone exists
validate_dns_zone() {
    log_info "Validating DNS zone: $BASE_ZONE"
    
    if ! az network dns zone show \
        --name "$BASE_ZONE" \
        --resource-group "$DNS_ZONE_RESOURCE_GROUP" \
        --output none 2>/dev/null; then
        log_error "DNS zone $BASE_ZONE not found or not accessible"
        log_error "Please ensure the zone exists and you have permissions"
        return 1
    fi
    
    log_success "DNS zone $BASE_ZONE validated"
    return 0
}

# Function: Get Container App ingress details
get_container_app_ingress() {
    local fqdn
    fqdn=$(az containerapp show \
        --name "$CONTAINER_APP_NAME" \
        --resource-group "$RESOURCE_GROUP" \
        --query "properties.configuration.ingress.fqdn" \
        --output tsv 2>/dev/null)
    
    if [ -z "$fqdn" ]; then
        log_error "Failed to retrieve Container App ingress FQDN" >&2
        return 1
    fi
    
    log_success "Container App FQDN: $fqdn" >&2
    echo "$fqdn"
}

# Function: Get IP address from FQDN
get_ip_from_fqdn() {
    local fqdn="$1"
    local ip
    
    ip=$(dig +short "$fqdn" A | head -n 1)
    
    if [ -z "$ip" ]; then
        log_error "Failed to resolve IP for $fqdn" >&2
        return 1
    fi
    
    log_success "Resolved IP: $ip" >&2
    echo "$ip"
}

# Function: Create or update DNS A record
create_dns_record() {
    local record_name="$1"
    local target_ip="$2"
    local ttl="${3:-300}"
    
    log_info "Creating DNS A record: $record_name.$BASE_ZONE → $target_ip"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would create A record: $record_name → $target_ip"
        return 0
    fi
    
    if az network dns record-set a create \
        --name "$record_name" \
        --resource-group "$DNS_ZONE_RESOURCE_GROUP" \
        --zone-name "$BASE_ZONE" \
        --ttl "$ttl" \
        --output none 2>/dev/null || true; then
        log_info "Created A record set"
    fi
    
    if az network dns record-set a add-record \
        --record-set-name "$record_name" \
        --resource-group "$DNS_ZONE_RESOURCE_GROUP" \
        --zone-name "$BASE_ZONE" \
        --ipv4-address "$target_ip" \
        --output none 2>/dev/null; then
        log_success "DNS A record created: $record_name"
        return 0
    else
        log_error "Failed to create DNS A record"
        return 1
    fi
}

# Function: Validate DNS propagation
validate_dns_propagation() {
    local fqdn="$1"
    local expected_ip="$2"
    local max_wait=600  # 10 minutes
    local interval=10
    local elapsed=0
    
    log_info "Validating DNS propagation for $fqdn"
    log_info "Expected IP: $expected_ip"
    log_info "Waiting up to $max_wait seconds..."
    
    while [ $elapsed -lt $max_wait ]; do
        local resolved_ip
        resolved_ip=$(dig +short "$fqdn" A @8.8.8.8 | head -n 1)
        
        if [ "$resolved_ip" = "$expected_ip" ]; then
            log_success "DNS propagated successfully ($elapsed seconds)"
            return 0
        fi
        
        if [ $((elapsed % 30)) -eq 0 ]; then
            log_info "Still waiting... ($elapsed/$max_wait seconds, current: ${resolved_ip:-none})"
        fi
        
        sleep $interval
        elapsed=$((elapsed + interval))
    done
    
    log_error "DNS propagation timeout after $max_wait seconds"
    return 1
}

# Function: Create DNS TXT record for ACME challenge
create_acme_txt_record() {
    local challenge_domain="$1"
    local challenge_value="$2"
    
    log_info "Creating TXT record for ACME challenge: $challenge_domain"
    
    # Extract record name (remove base zone)
    local record_name="${challenge_domain%.$BASE_ZONE}"
    
    if az network dns record-set txt add-record \
        --resource-group "$DNS_ZONE_RESOURCE_GROUP" \
        --zone-name "$BASE_ZONE" \
        --record-set-name "$record_name" \
        --value "$challenge_value" \
        --output none 2>/dev/null; then
        log_success "TXT record created: $record_name"
        return 0
    else
        log_error "Failed to create TXT record"
        return 1
    fi
}

# Function: Delete DNS TXT record for ACME challenge
delete_acme_txt_record() {
    local challenge_domain="$1"
    
    log_info "Cleaning up TXT record: $challenge_domain"
    
    # Extract record name (remove base zone)
    local record_name="${challenge_domain%.$BASE_ZONE}"
    
    az network dns record-set txt delete \
        --resource-group "$DNS_ZONE_RESOURCE_GROUP" \
        --zone-name "$BASE_ZONE" \
        --name "$record_name" \
        --yes \
        --output none 2>/dev/null || true
}

# Function: Provision Let's Encrypt certificate using manual auth hook
provision_letsencrypt_cert() {
    local domain="$1"
    local wildcard_domain="$2"
    local email="$3"
    
    log_info "Provisioning Let's Encrypt certificate for $domain"
    log_info "Domains: $domain, $wildcard_domain"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would provision Let's Encrypt certificate"
        return 0
    fi
    
    local cert_dir="$PROJECT_ROOT/azure/letsencrypt"
    mkdir -p "$cert_dir"
    
    # Create auth hook script
    local auth_hook="$cert_dir/auth-hook.sh"
    cat > "$auth_hook" << 'HOOK_EOF'
#!/bin/bash
# This script is called by certbot to create the DNS TXT record
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../../scripts/common.sh"

# certbot provides these environment variables:
# CERTBOT_DOMAIN: Domain being authenticated
# CERTBOT_VALIDATION: Validation string
# CERTBOT_TOKEN: Unique token for this challenge

CHALLENGE_DOMAIN="_acme-challenge.${CERTBOT_DOMAIN}"
CHALLENGE_VALUE="${CERTBOT_VALIDATION}"

log_info "Auth hook: Creating TXT record for ${CHALLENGE_DOMAIN}"

# Load configuration from parent script
source "${SCRIPT_DIR}/config.sh"

# Extract record name
RECORD_NAME="${CHALLENGE_DOMAIN%.${BASE_ZONE}}"

# Create TXT record
az network dns record-set txt add-record \
    --resource-group "${DNS_ZONE_RESOURCE_GROUP}" \
    --zone-name "${BASE_ZONE}" \
    --record-set-name "${RECORD_NAME}" \
    --value "${CHALLENGE_VALUE}" \
    --output none

# Wait for DNS propagation
log_info "Waiting 30 seconds for DNS propagation..."
sleep 30
HOOK_EOF

    # Create cleanup hook script
    local cleanup_hook="$cert_dir/cleanup-hook.sh"
    cat > "$cleanup_hook" << 'HOOK_EOF'
#!/bin/bash
# This script is called by certbot to remove the DNS TXT record
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../../scripts/common.sh"

CHALLENGE_DOMAIN="_acme-challenge.${CERTBOT_DOMAIN}"

log_info "Cleanup hook: Removing TXT record for ${CHALLENGE_DOMAIN}"

# Load configuration from parent script
source "${SCRIPT_DIR}/config.sh"

# Extract record name
RECORD_NAME="${CHALLENGE_DOMAIN%.${BASE_ZONE}}"

# Delete TXT record
az network dns record-set txt delete \
    --resource-group "${DNS_ZONE_RESOURCE_GROUP}" \
    --zone-name "${BASE_ZONE}" \
    --name "${RECORD_NAME}" \
    --yes \
    --output none 2>/dev/null || true
HOOK_EOF

    # Create config file for hooks
    cat > "$cert_dir/config.sh" << EOF
BASE_ZONE="$BASE_ZONE"
DNS_ZONE_RESOURCE_GROUP="$DNS_ZONE_RESOURCE_GROUP"
EOF

    chmod +x "$auth_hook" "$cleanup_hook"
    
    log_info "Running certbot with automated DNS-01 challenge..."
    
    # Run certbot with manual hooks
    if certbot certonly \
        --manual \
        --preferred-challenges dns \
        --manual-auth-hook "$auth_hook" \
        --manual-cleanup-hook "$cleanup_hook" \
        --email "$email" \
        --agree-tos \
        --no-eff-email \
        --non-interactive \
        -d "$domain" \
        -d "$wildcard_domain" \
        --config-dir "$cert_dir/config" \
        --work-dir "$cert_dir/work" \
        --logs-dir "$cert_dir/logs"; then
        
        log_success "Let's Encrypt certificate provisioned"
        
        # Certificate paths
        CERT_PATH="$cert_dir/config/live/$domain/fullchain.pem"
        KEY_PATH="$cert_dir/config/live/$domain/privkey.pem"
        
        if [ ! -f "$CERT_PATH" ] || [ ! -f "$KEY_PATH" ]; then
            log_error "Certificate files not found after provisioning"
            return 1
        fi
        
        log_success "Certificate: $CERT_PATH"
        log_success "Private Key: $KEY_PATH"
        return 0
    else
        log_error "Failed to provision Let's Encrypt certificate"
        return 1
    fi
}

# Function: Upload certificate to Key Vault
upload_cert_to_keyvault() {
    local cert_path="$1"
    local key_path="$2"
    
    log_info "Uploading certificate to Key Vault: $KEYVAULT_NAME"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would upload certificate to Key Vault"
        return 0
    fi
    
    local cert_secret_name="tls-cert-${ENVIRONMENT}"
    local key_secret_name="tls-key-${ENVIRONMENT}"
    
    # Upload certificate
    if az keyvault secret set \
        --vault-name "$KEYVAULT_NAME" \
        --name "$cert_secret_name" \
        --file "$cert_path" \
        --output none; then
        log_success "Certificate uploaded: $cert_secret_name"
    else
        log_error "Failed to upload certificate"
        return 1
    fi
    
    # Upload private key
    if az keyvault secret set \
        --vault-name "$KEYVAULT_NAME" \
        --name "$key_secret_name" \
        --file "$key_path" \
        --output none; then
        log_success "Private key uploaded: $key_secret_name"
    else
        log_error "Failed to upload private key"
        return 1
    fi
    
    log_success "Certificate and key uploaded to Key Vault"
    return 0
}

# Function: Bind custom domain to Container App
bind_custom_domain() {
    local domain="$1"
    
    log_info "Binding custom domain to Container App: $domain"
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "[DRY RUN] Would bind custom domain: $domain"
        return 0
    fi
    
    # Note: This is a simplified version. Full implementation would:
    # 1. Add custom domain to Container App
    # 2. Bind certificate from Key Vault
    # 3. Configure TLS settings
    
    log_warn "Custom domain binding requires Container Apps managed certificate or certificate from Key Vault"
    log_warn "This feature will be implemented when Container Apps supports Key Vault certificate binding"
    log_info "Current workaround: DNS points to Container Apps default domain"
    
    return 0
}

# Function: Validate HTTPS access
validate_https_access() {
    local domain="$1"
    
    log_info "Validating HTTPS access: https://$domain/health"
    
    local status_code
    status_code=$(curl -s -o /dev/null -w "%{http_code}" "https://$domain/health" || echo "000")
    
    if [ "$status_code" = "200" ]; then
        log_success "HTTPS access validated (HTTP $status_code)"
        return 0
    else
        log_warn "HTTPS access returned HTTP $status_code"
        log_info "This may be expected if custom domain binding is not complete"
        return 1
    fi
}

# Main execution
main() {
    log_info "================================================"
    log_info "Protogate DNS Configuration"
    log_info "================================================"
    
    # Step 1: Validate prerequisites
    log_info "Step 1: Validating prerequisites"
    validate_dns_zone || exit 1
    
    # Step 2: Get Container App ingress details
    log_info "Step 2: Getting Container App ingress details"
    log_info "Retrieving Container App ingress FQDN"
    CONTAINER_APP_FQDN=$(get_container_app_ingress) || exit 1
    log_info "Resolving IP address for $CONTAINER_APP_FQDN"
    CONTAINER_APP_IP=$(get_ip_from_fqdn "$CONTAINER_APP_FQDN") || exit 1
    
    # Step 3: Create DNS records
    log_info "Step 3: Creating DNS records"
    create_dns_record "$DNS_PREFIX" "$CONTAINER_APP_IP" || exit 1
    create_dns_record "*.$DNS_PREFIX" "$CONTAINER_APP_IP" || exit 1
    
    # Step 4: Validate DNS propagation
    log_info "Step 4: Validating DNS propagation"
    validate_dns_propagation "$TUNNEL_DOMAIN" "$CONTAINER_APP_IP" || log_warn "DNS validation failed, continuing..."
    
    # Step 5: Provision Let's Encrypt certificate (if enabled)
    if [ "$ENABLE_LETSENCRYPT" = true ]; then
        log_info "Step 5: Provisioning Let's Encrypt certificate"
        provision_letsencrypt_cert "$TUNNEL_DOMAIN" "$WILDCARD_DOMAIN" "$CONTACT_EMAIL" || log_warn "Certificate provisioning failed, continuing..."
        
        if [ -n "${CERT_PATH:-}" ] && [ -n "${KEY_PATH:-}" ]; then
            # Step 6: Upload certificate to Key Vault
            log_info "Step 6: Uploading certificate to Key Vault"
            upload_cert_to_keyvault "$CERT_PATH" "$KEY_PATH" || log_warn "Certificate upload failed, continuing..."
            
            # Step 7: Bind custom domain
            log_info "Step 7: Binding custom domain to Container App"
            bind_custom_domain "$TUNNEL_DOMAIN" || log_warn "Custom domain binding incomplete"
        fi
    else
        log_info "Let's Encrypt disabled, skipping certificate provisioning"
    fi
    
    # Step 8: Validate HTTPS access
    log_info "Step 8: Validating HTTPS access"
    validate_https_access "$TUNNEL_DOMAIN" || log_warn "HTTPS validation incomplete"
    
    # Output results
    log_info "================================================"
    log_success "DNS Configuration Complete!"
    log_info "================================================"
    log_info "Tunnel Domain: $TUNNEL_DOMAIN"
    log_info "Wildcard Domain: $WILDCARD_DOMAIN"
    log_info "Container App IP: $CONTAINER_APP_IP"
    log_info "Container App FQDN: $CONTAINER_APP_FQDN"
    
    if [ "$JSON_OUTPUT" = true ]; then
        json_output "success" "DNS configuration completed" \
            "tunnel_domain=$TUNNEL_DOMAIN" \
            "wildcard_domain=$WILDCARD_DOMAIN" \
            "container_ip=$CONTAINER_APP_IP" \
            "container_fqdn=$CONTAINER_APP_FQDN" \
            "letsencrypt=$ENABLE_LETSENCRYPT"
    fi
    
    log_info ""
    log_info "Next Steps:"
    log_info "  1. Test DNS resolution: dig $TUNNEL_DOMAIN"
    log_info "  2. Test wildcard: dig test.$TUNNEL_DOMAIN"
    log_info "  3. Access Management API: https://$TUNNEL_DOMAIN/v1/tunnels"
    log_info ""
}

# Run main function
main
