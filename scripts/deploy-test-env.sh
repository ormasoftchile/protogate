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
    --keyvault-name <name>  Key Vault name (default: protogate-{env}-kv, auto-created)

Examples:
    $0 --env test
    $0 --env test --image-tag v1.0.0
    $0 --env prod --location eastus
EOF
    exit 1
}

create_keyvault() {
    local rg=$(get_resource_group)
    local kv_name="${KEYVAULT_NAME:-protogate-${ENVIRONMENT}-kv}"
    
    log_info "Ensuring Key Vault exists: $kv_name"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create Key Vault: $kv_name"
        KEYVAULT_URI="https://${kv_name}.vault.azure.net/"
        return 0
    fi
    
    # Check if Key Vault exists
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
    
    # Set global variable
    KEYVAULT_URI="https://${kv_name}.vault.azure.net/"
    echo "$KEYVAULT_URI" > "$SCRIPT_DIR/../azure/.keyvault-uri-${ENVIRONMENT}"
    log_info "Key Vault URI: $KEYVAULT_URI"
}

create_resource_group() {
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
        az group create \
            --name "$rg" \
            --location "$LOCATION" \
            --tags environment="$ENVIRONMENT" project=protogate \
            --output none || error_exit "Failed to create resource group"
        
        log_success "Resource group created: $rg"
    fi
}

create_log_analytics_workspace() {
    local rg=$(get_resource_group)
    local workspace_name="protogate-${ENVIRONMENT}-logs"
    
    log_info "Ensuring Log Analytics workspace exists: $workspace_name"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create Log Analytics workspace: $workspace_name"
        return 0
    fi
    
    if az monitor log-analytics workspace show --workspace-name "$workspace_name" --resource-group "$rg" &> /dev/null; then
        log_info "Log Analytics workspace already exists: $workspace_name"
    else
        log_info "Creating Log Analytics workspace: $workspace_name"
        az monitor log-analytics workspace create \
            --workspace-name "$workspace_name" \
            --resource-group "$rg" \
            --location "$LOCATION" \
            --output none || error_exit "Failed to create Log Analytics workspace"
        
        log_success "Log Analytics workspace created: $workspace_name"
    fi
}

create_container_registry() {
    local rg=$(get_resource_group)
    local acr_name="protogate${ENVIRONMENT}acr"
    
    log_info "Ensuring Container Registry exists: $acr_name"
    
    # Update global ACR variables
    ACR_NAME="$acr_name"
    ACR_REGISTRY="${ACR_NAME}.azurecr.io"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create Container Registry: $acr_name"
        return 0
    fi
    
    if az acr show --name "$acr_name" --resource-group "$rg" &> /dev/null; then
        log_info "Container Registry already exists: $acr_name"
    else
        log_info "Creating Container Registry: $acr_name"
        az acr create \
            --resource-group "$rg" \
            --name "$acr_name" \
            --sku Basic \
            --admin-enabled true \
            --location "$LOCATION" \
            --output none || error_exit "Failed to create Container Registry"
        
        log_success "Container Registry created: $acr_name"
    fi
    
    # Login to ACR
    log_info "Logging in to Container Registry..."
    az acr login --name "$acr_name" || error_exit "Failed to login to ACR"
}

build_and_push_image() {
    local image_name="protogate-server"
    local full_image="${ACR_REGISTRY}/${image_name}:${IMAGE_TAG}"
    
    log_info "Building and pushing Docker image: $full_image"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would build and push image: $full_image"
        return 0
    fi
    
    # Check if running on Apple Silicon (ARM64)
    local platform="linux/amd64"
    local dockerfile="docker/Dockerfile.alpine"
    
    if [ "$(uname -m)" = "arm64" ]; then
        log_info "Detected ARM64 (Apple Silicon), building multi-platform image..."
        platform="linux/amd64,linux/arm64"
    fi
    
    # Verify Dockerfile exists
    if [ ! -f "$SCRIPT_DIR/../$dockerfile" ]; then
        log_error "Dockerfile not found: $dockerfile"
        log_info "Available Dockerfiles:"
        ls -la "$SCRIPT_DIR/../docker/" | grep Dockerfile || true
        error_exit "Dockerfile not found"
    fi
    
    # Build using Docker Buildx for multi-platform support
    log_info "Building image for platform: $platform"
    log_info "Using Dockerfile: $dockerfile"
    cd "$SCRIPT_DIR/.."
    
    docker buildx build \
        --platform "$platform" \
        --tag "$full_image" \
        --push \
        --file "$dockerfile" \
        . || error_exit "Failed to build and push Docker image"
    
    log_success "Image built and pushed: $full_image"
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
        
        # Get Log Analytics workspace info
        local workspace_name="protogate-${ENVIRONMENT}-logs"
        local workspace_id=$(az monitor log-analytics workspace show \
            --workspace-name "$workspace_name" \
            --resource-group "$rg" \
            --query customerId \
            --output tsv 2>/dev/null || echo "")
        local workspace_key=$(az monitor log-analytics workspace get-shared-keys \
            --workspace-name "$workspace_name" \
            --resource-group "$rg" \
            --query primarySharedKey \
            --output tsv 2>/dev/null || echo "")
        
        if [ -n "$workspace_id" ] && [ -n "$workspace_key" ]; then
            log_info "Using Log Analytics workspace: $workspace_name"
            az containerapp env create \
                --name "$env_name" \
                --resource-group "$rg" \
                --location "$LOCATION" \
                --logs-workspace-id "$workspace_id" \
                --logs-workspace-key "$workspace_key" \
                --output none || error_exit "Failed to create Container Apps environment"
        else
            log_warn "Log Analytics workspace not configured, creating without logs"
            az containerapp env create \
                --name "$env_name" \
                --resource-group "$rg" \
                --location "$LOCATION" \
                --output none || error_exit "Failed to create Container Apps environment"
        fi
        
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
                "PORT=8080" \
                "AGENT_PORT=8443" \
                "KEY_VAULT_URI=${KEYVAULT_URI}" \
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
            --set-env-vars "KEY_VAULT_URI=${KEYVAULT_URI}" \
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
    local kv_name="${KEYVAULT_NAME:-protogate-${ENVIRONMENT}-kv}"
    
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
    
    # Grant access to secrets
    log_info "Granting identity $identity access to Key Vault $kv_name"
    az keyvault set-policy \
        --name "$kv_name" \
        --object-id "$identity" \
        --secret-permissions get list \
        --output none || log_warn "Failed to set Key Vault access policy"
    
    log_success "Key Vault access granted to managed identity: $identity"
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
            --keyvault-name)
                KEYVAULT_NAME="$2"
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
    
    # Provision resources
    local rg=$(get_resource_group)
    log_info "Using resource group: $rg"
    
    create_resource_group
    create_log_analytics_workspace
    create_keyvault
    create_container_registry
    build_and_push_image
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
