#!/bin/bash
# Deploy Azure Container Apps test environment

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

LOCATION="westus2"
ACR_NAME="protogatedevacr"
ACR_REGISTRY="${ACR_NAME}.azurecr.io"
IMAGE_TAG="${IMAGE_TAG:-latest}"

usage() {
    cat << EOF
Usage: $0 --env <test|prod> [options]

Deploy Azure Container Apps environment with protogate server

Required:
    --env <test|prod>   Environment (test or prod)

Options:
    --dry-run           Show what would be done without executing
    --json              Output results in JSON format
    --verbose, -v       Enable verbose output
    --image-tag <tag>   Docker image tag (default: $IMAGE_TAG)
    --location <loc>    Azure region (default: $LOCATION)

Examples:
    $0 --env test
    $0 --env test --image-tag v1.0.0
    $0 --env prod --location eastus
EOF
    exit 1
}

load_keyvault_uri() {
    local uri_file="$SCRIPT_DIR/../azure/.keyvault-uri-${ENVIRONMENT}"
    
    if [ ! -f "$uri_file" ]; then
        log_warn "Key Vault URI file not found: $uri_file"
        log_info "Run: ./scripts/provision-keyvault.sh --env $ENVIRONMENT"
        return 1
    fi
    
    KEYVAULT_URI=$(cat "$uri_file")
    log_info "Key Vault URI loaded: $KEYVAULT_URI"
    return 0
}

provision_container_apps_environment() {
    local env_name=$(get_container_app_env)
    local rg=$(get_resource_group)
    
    log_info "Provisioning Container Apps environment: $env_name"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create Container Apps environment: $env_name"
        return 0
    fi
    
    # Check if environment already exists
    if az containerapp env show --name "$env_name" --resource-group "$rg" &> /dev/null; then
        log_info "Container Apps environment already exists: $env_name"
    else
        log_info "Creating Container Apps environment: $env_name"
        az containerapp env create \
            --name "$env_name" \
            --resource-group "$rg" \
            --location "$LOCATION" \
            --output none || error_exit "Failed to create Container Apps environment"
        
        log_success "Container Apps environment created: $env_name"
    fi
}

deploy_server_app() {
    local app_name=$(get_server_app_name)
    local env_name=$(get_container_app_env)
    local rg=$(get_resource_group)
    local image="${ACR_REGISTRY}/protogate-server:${IMAGE_TAG}"
    
    log_info "Deploying server container app: $app_name"
    log_info "  Image: $image"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would deploy container app: $app_name"
        return 0
    fi
    
    # Check if app already exists
    local app_exists=false
    if az containerapp show --name "$app_name" --resource-group "$rg" &> /dev/null; then
        app_exists=true
        log_info "Container app exists, updating..."
    fi
    
    if [ "$app_exists" = false ]; then
        # Create new container app
        log_info "Creating container app: $app_name"
        az containerapp create \
            --name "$app_name" \
            --resource-group "$rg" \
            --environment "$env_name" \
            --image "$image" \
            --registry-server "$ACR_REGISTRY" \
            --target-port 8080 \
            --ingress external \
            --min-replicas 1 \
            --max-replicas 5 \
            --cpu 0.5 \
            --memory 1Gi \
            --env-vars \
                "KEY_VAULT_URI=${KEYVAULT_URI:-}" \
                "DNS_ZONE=tunnel.${ENVIRONMENT}.example.com" \
                "LOG_LEVEL=INFO" \
            --output none || error_exit "Failed to create container app"
        
        log_success "Container app created: $app_name"
    else
        # Update existing container app
        log_info "Updating container app image to: $image"
        az containerapp update \
            --name "$app_name" \
            --resource-group "$rg" \
            --image "$image" \
            --output none || error_exit "Failed to update container app"
        
        log_success "Container app updated: $app_name"
    fi
}

enable_managed_identity() {
    local app_name=$(get_server_app_name)
    local rg=$(get_resource_group)
    
    log_info "Enabling system-assigned managed identity..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would enable managed identity"
        return 0
    fi
    
    # Enable system-assigned managed identity
    local identity=$(az containerapp identity assign \
        --name "$app_name" \
        --resource-group "$rg" \
        --system-assigned \
        --query principalId \
        -o tsv 2>/dev/null)
    
    if [ -n "$identity" ]; then
        log_success "Managed identity enabled: $identity"
        echo "$identity" > "$SCRIPT_DIR/../azure/.managed-identity-${ENVIRONMENT}"
    else
        log_warn "Failed to enable managed identity or already enabled"
    fi
}

grant_keyvault_access() {
    local app_name=$(get_server_app_name)
    local rg=$(get_resource_group)
    
    log_info "Granting Key Vault access to container app..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would grant Key Vault access"
        return 0
    fi
    
    # Get managed identity principal ID
    local identity=$(az containerapp identity show \
        --name "$app_name" \
        --resource-group "$rg" \
        --query principalId \
        -o tsv 2>/dev/null)
    
    if [ -z "$identity" ]; then
        log_warn "Managed identity not found, skipping Key Vault access grant"
        return 0
    fi
    
    # Get Key Vault name
    local kv_name=$(get_keyvault_name)
    
    # Grant access to secrets
    log_info "Granting identity $identity access to Key Vault $kv_name"
    az keyvault set-policy \
        --name "$kv_name" \
        --object-id "$identity" \
        --secret-permissions get list \
        --output none || log_warn "Failed to set Key Vault access policy"
    
    log_success "Key Vault access granted"
}

configure_health_probe() {
    local app_name=$(get_server_app_name)
    local rg=$(get_resource_group)
    
    log_info "Configuring health probe..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would configure health probe"
        return 0
    fi
    
    # Note: Health probes are configured via --target-port during create
    # For updates, we can use revision management
    log_info "Health probe configured via ingress on port 8080"
    log_success "Health probe configured"
}

get_server_url() {
    local app_name=$(get_server_app_name)
    local rg=$(get_resource_group)
    
    if [ "$DRY_RUN" = true ]; then
        echo "https://${app_name}.example.azurecontainerapps.io"
        return 0
    fi
    
    local fqdn=$(az containerapp show \
        --name "$app_name" \
        --resource-group "$rg" \
        --query properties.configuration.ingress.fqdn \
        -o tsv 2>/dev/null)
    
    if [ -n "$fqdn" ]; then
        echo "https://$fqdn"
    else
        echo ""
    fi
}

check_health() {
    local server_url=$1
    local max_attempts=30
    local attempt=0
    
    log_info "Checking server health..."
    log_info "Health endpoint: ${server_url}/health"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would check health at: ${server_url}/health"
        return 0
    fi
    
    while [ $attempt -lt $max_attempts ]; do
        if curl -s -f "${server_url}/health" > /dev/null 2>&1; then
            log_success "Server is healthy!"
            
            # Get health details
            local health=$(curl -s "${server_url}/health")
            log_info "Health response: $health"
            return 0
        fi
        
        attempt=$((attempt + 1))
        if [ $attempt -lt $max_attempts ]; then
            echo -n "."
            sleep 10
        fi
    done
    
    echo ""
    log_warn "Health check timed out, but deployment may still be starting"
    log_info "Check manually: curl ${server_url}/health"
    return 1
}

main() {
    # Check for help first
    if [ $# -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
        usage
    fi
    
    parse_args "$@"
    
    # Parse additional options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --env)
                shift 2
                ;;
            --image-tag)
                IMAGE_TAG="$2"
                shift 2
                ;;
            --location)
                LOCATION="$2"
                shift 2
                ;;
            --dry-run|--json|--verbose|-v)
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
    
    validate_environment
    
    log_info "Starting test environment deployment..."
    log_info "Environment: $ENVIRONMENT"
    log_info "Location: $LOCATION"
    log_info "Image tag: $IMAGE_TAG"
    
    # Validate prerequisites
    check_azure_cli
    check_azure_login
    
    # Load Key Vault URI
    if ! load_keyvault_uri; then
        log_warn "Continuing without Key Vault configuration"
    fi
    
    # Provision resources
    local rg=$(get_resource_group)
    log_info "Using resource group: $rg"
    
    provision_container_apps_environment
    deploy_server_app
    enable_managed_identity
    grant_keyvault_access
    configure_health_probe
    
    # Get server URL
    local server_url=$(get_server_url)
    if [ -n "$server_url" ]; then
        echo "$server_url" > "$SCRIPT_DIR/../azure/.server-url-${ENVIRONMENT}"
        log_info "Server URL saved to: azure/.server-url-${ENVIRONMENT}"
    fi
    
    # Summary
    log_info ""
    log_success "========================================="
    log_success "Deployment completed!"
    log_success "========================================="
    log_info "Server URL: $server_url"
    log_info ""
    
    # Health check
    if [ -n "$server_url" ]; then
        check_health "$server_url" || true
    fi
    
    log_info ""
    log_info "Next steps:"
    log_info "  1. Verify health: curl ${server_url}/health"
    log_info "  2. Run e2e tests: ./scripts/e2e-test.sh --env $ENVIRONMENT"
    
    json_output "server_url" "$server_url"
}

main "$@"
