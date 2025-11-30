#!/bin/bash

echo "🚀 Protogate Local Testing Setup"
echo "================================="
echo ""

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

success() {
    echo -e "${GREEN}✓${NC} $1"
}

warn() {
    echo -e "${YELLOW}⚠${NC} $1"
}

# Configuration
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
SERVER_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$SERVER_DIR/protogate-server"
AGENT_BIN="$SCRIPT_DIR/build/tunnel-agent"
LOCAL_SERVICE_PORT=3000
TUNNEL_ID="test-api"
TUNNEL_TOKEN="tnl_local_test_123"

# Cleanup function
cleanup() {
    echo ""
    info "Cleaning up..."
    pkill -f "protogate-server" 2>/dev/null || true
    pkill -f "tunnel-agent" 2>/dev/null || true
    pkill -f "python3 -m http.server $LOCAL_SERVICE_PORT" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Step 1: Check if server is built
info "Step 1: Checking server binary..."
if [ ! -f "$SERVER_BIN" ]; then
    warn "Server not built. Building now..."
    cd .. && cmake --build build --target protogate-server -j$(sysctl -n hw.ncpu) && cd tunnel-agent
fi
success "Server binary ready"

# Step 2: Check if agent is built
info "Step 2: Checking agent binary..."
if [ ! -f "$AGENT_BIN" ]; then
    warn "Agent not built. Building now..."
    cmake --build build -j$(sysctl -n hw.ncpu)
fi
success "Agent binary ready"

# Step 3: Start the server
info "Step 3: Starting Protogate server..."

# Set required environment variables for local testing
export KEY_VAULT_URI="https://mock-keyvault.vault.azure.net"
export DATABASE_URL="sqlite://test.db"
export DNS_ZONE="local.test"

# Start server from project root (where localhost.crt/key are located)
cd "$PROJECT_ROOT"
"$SERVER_DIR/protogate-server" > /tmp/protogate-server.log 2>&1 &
SERVER_PID=$!
cd - > /dev/null

# Wait for server to start
sleep 2

# Check if server is running
if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    warn "Server failed to start. Check /tmp/protogate-server.log"
    cat /tmp/protogate-server.log
    exit 1
fi

# Verify server health
if curl -k -s https://localhost:8080/health > /dev/null 2>&1; then
    success "Server running on https://localhost:8080"
else
    warn "Server started but health check failed"
fi

# Register the test token
info "Registering test token with server..."
curl -k -X POST https://localhost:8080/api/v1/tunnels \
  -H "Content-Type: application/json" \
  -d "{
    \"tunnel_id\": \"$TUNNEL_ID\",
    \"token\": \"$TUNNEL_TOKEN\"
  }" > /dev/null 2>&1 || true

success "Token registration attempted (server will auto-add if using mock mode)"

# Step 4: Start local test service
info "Step 4: Starting local test service on port $LOCAL_SERVICE_PORT..."
cd /tmp
python3 -m http.server $LOCAL_SERVICE_PORT > /dev/null 2>&1 &
LOCAL_SERVICE_PID=$!
cd - > /dev/null
sleep 1

if curl -s http://localhost:$LOCAL_SERVICE_PORT > /dev/null 2>&1; then
    success "Local test service running on http://localhost:$LOCAL_SERVICE_PORT"
else
    warn "Local service failed to start"
    exit 1
fi

# Step 5: Create agent config
info "Step 5: Creating agent configuration..."
cat > /tmp/test-agent-config.json <<EOF
{
  "server": {
    "host": "localhost",
    "port": 8443,
    "verify_tls": false
  },
  "tunnel": {
    "id": "$TUNNEL_ID",
    "token": "$TUNNEL_TOKEN"
  },
  "local": {
    "url": "http://localhost:$LOCAL_SERVICE_PORT",
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
    "format": "text"
  }
}
EOF
success "Agent configuration created"

# Step 6: Start the tunnel agent
info "Step 6: Starting tunnel agent..."
$AGENT_BIN --config /tmp/test-agent-config.json > /tmp/protogate-agent.log 2>&1 &
AGENT_PID=$!
sleep 3

if ps -p $AGENT_PID > /dev/null 2>&1; then
    success "Agent started (PID: $AGENT_PID)"
else
    warn "Agent failed to start. Check /tmp/protogate-agent.log"
    cat /tmp/protogate-agent.log
    exit 1
fi

# Step 7: Test the setup
echo ""
echo "================================="
echo "🎉 Local Testing Environment Ready!"
echo "================================="
echo ""
echo "Components running:"
echo "  • Protogate Server: https://localhost:8080 (PID: $SERVER_PID)"
echo "  • Tunnel Agent: Connected to server (PID: $AGENT_PID)"
echo "  • Local Service: http://localhost:$LOCAL_SERVICE_PORT (PID: $LOCAL_SERVICE_PID)"
echo ""
echo "Testing the tunnel..."
echo ""

# Try to access through tunnel
# Note: This requires the tunnel to be registered. For now, test direct access
info "Testing local service directly..."
if curl -s http://localhost:$LOCAL_SERVICE_PORT | head -5; then
    echo ""
    success "Local service accessible"
fi

echo ""
echo "Logs available at:"
echo "  • Server: /tmp/protogate-server.log"
echo "  • Agent: /tmp/protogate-agent.log"
echo ""
echo "Agent logs (last 20 lines):"
echo "----------------------------"
tail -20 /tmp/protogate-agent.log
echo ""
echo "Press Ctrl+C to stop all services..."
echo ""

# Keep running until interrupted
wait $AGENT_PID
