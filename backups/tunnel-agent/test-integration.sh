#!/bin/bash
set -e

echo "🧪 Protogate Tunnel Agent - Integration Test"
echo "============================================="
echo ""

# Configuration
AGENT_BIN="./build/tunnel-agent"
SERVER_HOST="localhost"
SERVER_PORT="8443"
TUNNEL_ID="test-api"
TUNNEL_TOKEN="tnl_test_token_123"
LOCAL_PORT="3000"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

fail() {
    echo -e "${RED}✗ FAIL:${NC} $1"
    exit 1
}

pass() {
    echo -e "${GREEN}✓ PASS:${NC} $1"
}

warn() {
    echo -e "${YELLOW}⚠ WARN:${NC} $1"
}

info() {
    echo "ℹ $1"
}

# Check if agent binary exists
info "Checking agent binary..."
if [ ! -f "$AGENT_BIN" ]; then
    fail "Agent binary not found at $AGENT_BIN. Run: cmake --build build"
fi
pass "Agent binary found"

# Check if server is running
info "Checking if Protogate server is running..."
if ! curl -k -s "https://${SERVER_HOST}:8080/health" > /dev/null 2>&1; then
    warn "Server not running at https://${SERVER_HOST}:8080"
    echo "  Start server with: cd ../build && ./protogate-server"
    echo "  This test requires the server to be running."
    exit 0
fi
pass "Server is running"

# Check if local service is running
info "Checking if local service is running on port ${LOCAL_PORT}..."
if ! curl -s "http://localhost:${LOCAL_PORT}" > /dev/null 2>&1; then
    warn "Local service not running on port ${LOCAL_PORT}"
    echo "  Start a test service with: python3 -m http.server ${LOCAL_PORT}"
    echo "  This test requires a local HTTP service."
    exit 0
fi
pass "Local service is running"

# Test 1: Agent help command
info "Test 1: Help command"
if $AGENT_BIN --help | grep -q "Protogate Tunnel Agent"; then
    pass "Help command works"
else
    fail "Help command failed"
fi

# Test 2: Agent starts with config
info "Test 2: Agent startup with configuration"
# Create test config
cat > /tmp/test-agent-config.json <<EOF
{
  "server": {
    "host": "${SERVER_HOST}",
    "port": ${SERVER_PORT},
    "verify_tls": false
  },
  "tunnel": {
    "id": "${TUNNEL_ID}",
    "token": "${TUNNEL_TOKEN}"
  },
  "local": {
    "url": "http://localhost:${LOCAL_PORT}",
    "timeout_seconds": 30
  },
  "health": {
    "heartbeat_interval_seconds": 30,
    "heartbeat_timeout_seconds": 60
  },
  "reconnect": {
    "initial_delay_seconds": 1,
    "max_delay_seconds": 60,
    "max_attempts": 0
  },
  "logging": {
    "level": "info",
    "format": "json"
  }
}
EOF

# Start agent in background
info "Starting agent..."
timeout 10 $AGENT_BIN --config /tmp/test-agent-config.json > /tmp/agent.log 2>&1 &
AGENT_PID=$!

# Wait for agent to start
sleep 2

# Check if agent is still running
if ps -p $AGENT_PID > /dev/null 2>&1; then
    pass "Agent started successfully"
    
    # Give it time to connect
    sleep 2
    
    # Kill agent
    kill $AGENT_PID 2>/dev/null || true
    wait $AGENT_PID 2>/dev/null || true
    
    # Check logs for successful connection attempt
    if grep -q "Connecting to server" /tmp/agent.log; then
        pass "Agent attempted connection to server"
    else
        warn "No connection attempt in logs"
    fi
    
    if grep -q "error" /tmp/agent.log; then
        warn "Errors found in agent logs:"
        grep "error" /tmp/agent.log | tail -5
    fi
else
    fail "Agent crashed on startup. Check /tmp/agent.log"
fi

# Cleanup
rm -f /tmp/test-agent-config.json
rm -f /tmp/agent.log

echo ""
echo "============================================="
echo -e "${GREEN}✓ Integration tests completed${NC}"
echo ""
echo "Note: Full end-to-end testing requires:"
echo "  1. Protogate server running with tunnel management"
echo "  2. Valid tunnel token created via Management API"
echo "  3. Test HTTP requests sent through tunnel URL"
