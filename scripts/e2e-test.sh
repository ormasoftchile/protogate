#!/bin/bash
# End-to-end test for protogate tunnel flow

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

TEST_SERVICE_PORT=3000
TEST_SERVICE_PID=""

usage() {
    cat << EOF
Usage: $0 --env <test|prod> [options]

Run end-to-end tests for protogate tunnel flow

Required:
    --env <test|prod>   Environment (test or prod)

Options:
    --dry-run           Show what would be done without executing
    --json              Output results in JSON format
    --verbose, -v       Enable verbose output
    --skip-cleanup      Don't cleanup test resources

Examples:
    $0 --env test
    $0 --env test --verbose
EOF
    exit 1
}

load_server_url() {
    local url_file="$SCRIPT_DIR/../azure/.server-url-${ENVIRONMENT}"
    
    if [ ! -f "$url_file" ]; then
        error_exit "Server URL file not found: $url_file. Run: ./scripts/deploy-test-env.sh --env $ENVIRONMENT"
    fi
    
    SERVER_URL=$(cat "$url_file")
    log_info "Server URL: $SERVER_URL"
}

start_test_service() {
    log_info "Starting test HTTP service on port $TEST_SERVICE_PORT..."
    
    if [ "$DRY_RUN" = true ]; then
        log_info "[DRY RUN] Would start test service"
        return 0
    fi
    
    # Start Python test service in background
    python3 "$SCRIPT_DIR/../test/e2e/test-service.py" $TEST_SERVICE_PORT > /tmp/test-service.log 2>&1 &
    TEST_SERVICE_PID=$!
    
    # Wait for service to start
    sleep 2
    
    if kill -0 $TEST_SERVICE_PID 2>/dev/null; then
        log_success "Test service started (PID: $TEST_SERVICE_PID)"
    else
        error_exit "Failed to start test service"
    fi
}

stop_test_service() {
    if [ -n "$TEST_SERVICE_PID" ] && kill -0 $TEST_SERVICE_PID 2>/dev/null; then
        log_info "Stopping test service (PID: $TEST_SERVICE_PID)..."
        kill $TEST_SERVICE_PID 2>/dev/null || true
        wait $TEST_SERVICE_PID 2>/dev/null || true
        log_success "Test service stopped"
    fi
}

test_server_health() {
    log_info "Test 1: Checking server health..."
    
    if [ "$DRY_RUN" = true ]; then
        log_success "[DRY RUN] ✓ Health check would pass"
        return 0
    fi
    
    local response=$(curl -s -w "\n%{http_code}" "${SERVER_URL}/health" 2>/dev/null)
    local body=$(echo "$response" | head -n -1)
    local status=$(echo "$response" | tail -n 1)
    
    if [ "$status" = "200" ]; then
        log_success "✓ Server is healthy"
        log_info "  Response: $body"
        return 0
    else
        log_error "✗ Health check failed (HTTP $status)"
        return 1
    fi
}

test_create_tunnel() {
    log_info "Test 2: Creating tunnel via Management API..."
    
    if [ "$DRY_RUN" = true ]; then
        log_success "[DRY RUN] ✓ Tunnel creation would succeed"
        TUNNEL_ID="test-tunnel-123"
        TUNNEL_TOKEN="test-token-456"
        return 0
    fi
    
    # Create tunnel
    local response=$(curl -s -w "\n%{http_code}" -X POST \
        "${SERVER_URL}/v1/tunnels" \
        -H "Content-Type: application/json" \
        -d '{"name":"e2e-test-tunnel"}' 2>/dev/null)
    
    local body=$(echo "$response" | head -n -1)
    local status=$(echo "$response" | tail -n 1)
    
    if [ "$status" = "201" ] || [ "$status" = "200" ]; then
        TUNNEL_ID=$(echo "$body" | grep -o '"id":"[^"]*"' | cut -d'"' -f4 || echo "")
        TUNNEL_TOKEN=$(echo "$body" | grep -o '"token":"[^"]*"' | cut -d'"' -f4 || echo "")
        
        if [ -n "$TUNNEL_ID" ]; then
            log_success "✓ Tunnel created: $TUNNEL_ID"
            return 0
        fi
    fi
    
    log_warn "✗ Tunnel creation returned HTTP $status"
    log_info "  Note: Management API may not be fully implemented yet"
    log_info "  Response: $body"
    return 1
}

test_local_service() {
    log_info "Test 3: Testing local service directly..."
    
    if [ "$DRY_RUN" = true ]; then
        log_success "[DRY RUN] ✓ Local service would respond"
        return 0
    fi
    
    local response=$(curl -s "http://localhost:${TEST_SERVICE_PORT}/test" 2>/dev/null)
    
    if echo "$response" | grep -q "Hello from test service"; then
        log_success "✓ Local test service responding"
        return 0
    else
        log_error "✗ Local service not responding"
        return 1
    fi
}

cleanup() {
    log_info "Cleaning up test resources..."
    
    stop_test_service
    
    # TODO: Delete tunnel if created
    # if [ -n "$TUNNEL_ID" ]; then
    #     curl -s -X DELETE "${SERVER_URL}/v1/tunnels/${TUNNEL_ID}" >/dev/null 2>&1 || true
    # fi
    
    log_success "Cleanup completed"
}

main() {
    parse_args "$@"
    
    if [ $# -eq 0 ]; then
        usage
    fi
    
    local SKIP_CLEANUP=false
    
    # Parse additional options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --env)
                shift 2
                ;;
            --skip-cleanup)
                SKIP_CLEANUP=true
                shift
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
    
    log_info "Starting E2E tests..."
    log_info "Environment: $ENVIRONMENT"
    
    # Setup trap for cleanup
    if [ "$SKIP_CLEANUP" = false ]; then
        trap cleanup EXIT
    fi
    
    # Load server URL
    load_server_url
    
    # Run tests
    local passed=0
    local failed=0
    
    log_info ""
    log_info "========================================="
    log_info "Running test suite..."
    log_info "========================================="
    log_info ""
    
    if test_server_health; then
        ((passed++))
    else
        ((failed++))
    fi
    
    echo ""
    
    start_test_service
    
    if test_local_service; then
        ((passed++))
    else
        ((failed++))
    fi
    
    echo ""
    
    if test_create_tunnel; then
        ((passed++))
    else
        ((failed++))
        log_warn "Continuing with remaining tests..."
    fi
    
    echo ""
    
    # Summary
    log_info "========================================="
    if [ $failed -eq 0 ]; then
        log_success "All tests passed! ($passed/$((passed + failed)))"
        log_info "========================================="
        exit 0
    else
        log_warn "Some tests failed: $passed passed, $failed failed"
        log_info "========================================="
        log_info ""
        log_info "Note: Some features may not be fully implemented yet."
        log_info "      Check IMPLEMENTATION_STATUS.md for details."
        exit 1
    fi
}

main "$@"
