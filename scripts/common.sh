#!/bin/bash
# Common utilities for Azure deployment scripts

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

# Error handler
error_exit() {
    log_error "$1"
    exit 1
}

# JSON output
json_output() {
    local key=$1
    local value=$2
    if [ "${JSON_OUTPUT:-false}" = "true" ]; then
        echo "{\"$key\": \"$value\"}"
    fi
}

# Script arguments
DRY_RUN=false
JSON_OUTPUT=false
VERBOSE=false
ENVIRONMENT=""

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --env)
                ENVIRONMENT="$2"
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
            *)
                log_error "Unknown option: $1"
                return 1
                ;;
        esac
    done

    if [ "$VERBOSE" = true ]; then
        set -x
    fi
}

# Validate environment
validate_environment() {
    if [ -z "$ENVIRONMENT" ]; then
        error_exit "Environment not specified. Use --env test or --env prod"
    fi
    if [[ "$ENVIRONMENT" != "test" && "$ENVIRONMENT" != "prod" ]]; then
        error_exit "Invalid environment: $ENVIRONMENT. Must be 'test' or 'prod'"
    fi
}

# Azure resource naming
get_resource_group() {
    echo "protogate-${ENVIRONMENT}-rg"
}

get_container_app_env() {
    echo "protogate-${ENVIRONMENT}-env"
}

get_server_app_name() {
    echo "protogate-${ENVIRONMENT}-server"
}

get_agent_app_name() {
    echo "protogate-${ENVIRONMENT}-agent"
}

get_keyvault_name() {
    # Key Vault names must be globally unique and 3-24 chars
    local suffix=$(echo -n "${ENVIRONMENT}" | md5sum | cut -c1-6 2>/dev/null || echo -n "${ENVIRONMENT}" | md5 | cut -c1-6)
    echo "pg-${ENVIRONMENT}-kv-${suffix}"
}

get_insights_name() {
    echo "protogate-${ENVIRONMENT}-insights"
}

get_log_analytics_name() {
    echo "protogate-${ENVIRONMENT}-logs"
}

# Azure CLI validation
check_azure_cli() {
    if ! command -v az &> /dev/null; then
        error_exit "Azure CLI not found. Please install: https://aka.ms/azure-cli"
    fi
    
    local version=$(az version --query '"azure-cli"' -o tsv)
    log_info "Azure CLI version: $version"
}

check_azure_login() {
    if ! az account show &> /dev/null; then
        error_exit "Not logged in to Azure. Run: az login"
    fi
    
    local subscription=$(az account show --query name -o tsv)
    log_info "Using Azure subscription: $subscription"
}

# Docker validation
check_docker() {
    if ! command -v docker &> /dev/null; then
        error_exit "Docker not found. Please install Docker Desktop"
    fi
    
    if ! docker info &> /dev/null; then
        error_exit "Docker daemon not running. Please start Docker"
    fi
}

check_docker_buildx() {
    if ! docker buildx version &> /dev/null; then
        error_exit "Docker buildx not available. Update Docker to latest version"
    fi
}

# ACR operations
acr_login() {
    local acr_name="protogatedevacr"
    log_info "Logging in to Azure Container Registry: $acr_name"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would login to ACR: $acr_name"
        return 0
    fi
    
    az acr login --name "$acr_name" || error_exit "Failed to login to ACR"
    log_success "Logged in to ACR"
}

# Wait for resource
wait_for_resource() {
    local resource_type=$1
    local resource_name=$2
    local max_wait=${3:-300}
    local interval=10
    local elapsed=0
    
    log_info "Waiting for $resource_type '$resource_name' to be ready..."
    
    while [ $elapsed -lt $max_wait ]; do
        if check_resource_ready "$resource_type" "$resource_name"; then
            log_success "$resource_type '$resource_name' is ready"
            return 0
        fi
        sleep $interval
        elapsed=$((elapsed + interval))
        echo -n "."
    done
    
    echo ""
    error_exit "Timeout waiting for $resource_type '$resource_name'"
}

check_resource_ready() {
    local resource_type=$1
    local resource_name=$2
    
    case $resource_type in
        "containerapp")
            az containerapp show -n "$resource_name" -g "$(get_resource_group)" --query "properties.provisioningState" -o tsv 2>/dev/null | grep -q "Succeeded"
            ;;
        *)
            return 1
            ;;
    esac
}
