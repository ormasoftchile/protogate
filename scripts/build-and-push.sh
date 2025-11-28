#!/bin/bash
# Build and push multi-architecture Docker images to Azure Container Registry

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Configuration
ACR_NAME="protogatedevacr"
ACR_REGISTRY="${ACR_NAME}.azurecr.io"
PLATFORMS="linux/amd64,linux/arm64"
DOCKERFILE="docker/Dockerfile.alpine"
BUILDER_NAME="protogate-builder"

usage() {
    cat << EOF
Usage: $0 <version> [options]

Build and push multi-arch Docker images to Azure Container Registry

Arguments:
    version             Version tag (e.g., v1.0.0, v1.0.0-rc1)

Options:
    --dry-run           Show what would be done without executing
    --json              Output results in JSON format
    --verbose, -v       Enable verbose output
    --platforms <list>  Comma-separated list of platforms (default: $PLATFORMS)
    --latest            Also tag as 'latest'

Examples:
    $0 v1.0.0
    $0 v1.0.0-rc1 --dry-run
    $0 v1.0.0 --latest --verbose
EOF
    exit 1
}

setup_buildx() {
    log_info "Setting up Docker buildx builder..."
    
    # Check if builder already exists
    if docker buildx inspect "$BUILDER_NAME" &> /dev/null; then
        log_info "Builder '$BUILDER_NAME' already exists"
        docker buildx use "$BUILDER_NAME"
        return 0
    fi
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would create buildx builder: $BUILDER_NAME"
        return 0
    fi
    
    # Create new builder
    log_info "Creating new buildx builder: $BUILDER_NAME"
    docker buildx create --name "$BUILDER_NAME" --use || error_exit "Failed to create builder"
    docker buildx inspect --bootstrap || error_exit "Failed to bootstrap builder"
    
    log_success "Builder '$BUILDER_NAME' created and ready"
}

validate_dockerfile() {
    if [ ! -f "$DOCKERFILE" ]; then
        error_exit "Dockerfile not found: $DOCKERFILE"
    fi
    log_info "Using Dockerfile: $DOCKERFILE"
}

build_image() {
    local repo=$1
    local version=$2
    local tag_latest=$3
    
    local image_name="${ACR_REGISTRY}/${repo}"
    local version_tag="${image_name}:${version}"
    
    log_info "Building multi-arch image: $repo"
    log_info "  Platforms: $PLATFORMS"
    log_info "  Version: $version"
    
    # Build tags
    local tags="--tag ${version_tag}"
    if [ "$tag_latest" = true ]; then
        tags="$tags --tag ${image_name}:latest"
    fi
    
    # Build command (no --target since Dockerfile doesn't have multi-stage targets by name)
    local build_cmd="docker buildx build \
        --platform $PLATFORMS \
        --file $DOCKERFILE \
        $tags \
        --push \
        ."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would run: $build_cmd"
        return 0
    fi
    
    log_info "Building and pushing image..."
    if eval "$build_cmd"; then
        log_success "Image built and pushed: $version_tag"
        json_output "image" "$version_tag"
        return 0
    else
        error_exit "Failed to build image: $repo"
    fi
}

verify_image() {
    local repo=$1
    local version=$2
    
    log_info "Verifying image in ACR: $repo:$version"
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would verify image in ACR"
        return 0
    fi
    
    # Check if image exists
    if az acr repository show-tags \
        --name "$ACR_NAME" \
        --repository "$repo" \
        --output tsv 2>/dev/null | grep -q "^${version}$"; then
        log_success "Image verified in ACR: $repo:$version"
    else
        error_exit "Image not found in ACR: $repo:$version"
    fi
    
    # Get manifest info
    local manifest=$(az acr repository show \
        --name "$ACR_NAME" \
        --repository "$repo" \
        --query "manifestCount" \
        -o tsv 2>/dev/null || echo "unknown")
    log_info "Manifest count: $manifest"
}

get_image_size() {
    local repo=$1
    local version=$2
    
    if [ "$DRY_RUN" = true ]; then
        return 0
    fi
    
    log_info "Getting image size for: $repo:$version"
    
    # Get image size (requires pulling manifest)
    local size=$(az acr repository show \
        --name "$ACR_NAME" \
        --image "${repo}:${version}" \
        --query "imageSize" \
        -o tsv 2>/dev/null || echo "unknown")
    
    if [ "$size" != "unknown" ] && [ -n "$size" ]; then
        local size_mb=$((size / 1024 / 1024))
        log_info "Image size: ${size_mb} MB"
        
        if [ $size_mb -gt 100 ]; then
            log_warn "Image size exceeds 100MB target: ${size_mb} MB"
        fi
    fi
}

main() {
    # Parse version argument
    if [ $# -eq 0 ]; then
        usage
    fi
    
    local VERSION="$1"
    shift
    
    # Default options
    local TAG_LATEST=false
    
    # Parse remaining options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --platforms)
                PLATFORMS="$2"
                shift 2
                ;;
            --latest)
                TAG_LATEST=true
                shift
                ;;
            --dry-run|--json|--verbose|-v)
                # Already handled by parse_args in common.sh
                shift
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                ;;
        esac
    done
    
    # Initialize
    parse_args "$@" || true
    
    log_info "Starting multi-arch Docker build..."
    log_info "Version: $VERSION"
    log_info "ACR: $ACR_REGISTRY"
    
    # Validate prerequisites
    check_docker
    check_docker_buildx
    check_azure_cli
    check_azure_login
    validate_dockerfile
    
    # Login to ACR
    acr_login
    
    # Setup buildx
    setup_buildx
    
    # Build and push server image
    log_info "========================================="
    log_info "Building protogate-server..."
    log_info "========================================="
    build_image "protogate-server" "$VERSION" "$TAG_LATEST"
    verify_image "protogate-server" "$VERSION"
    get_image_size "protogate-server" "$VERSION"
    
    # TODO: Build agent image when Dockerfile is ready
    # log_info ""
    # log_info "========================================="
    # log_info "Building protogate-agent..."
    # log_info "========================================="
    # build_image "protogate-agent" "$VERSION" "$TAG_LATEST"
    # verify_image "protogate-agent" "$VERSION"
    # get_image_size "protogate-agent" "$VERSION"
    
    # Summary
    log_info ""
    log_success "========================================="
    log_success "Build and push completed successfully!"
    log_success "========================================="
    log_info "Images available at:"
    log_info "  - ${ACR_REGISTRY}/protogate-server:${VERSION}"
    
    if [ "$TAG_LATEST" = true ]; then
        log_info "  - ${ACR_REGISTRY}/protogate-server:latest"
    fi
    
    log_info ""
    log_info "Next steps:"
    log_info "  1. Deploy test environment: ./scripts/deploy-test-env.sh"
    log_info "  2. Run e2e tests: ./scripts/e2e-test.sh --env test"
}

main "$@"
