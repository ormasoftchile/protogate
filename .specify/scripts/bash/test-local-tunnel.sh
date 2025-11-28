#!/usr/bin/env bash
# Test script for local tunnel functionality
# Usage: ./test-local-tunnel.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

echo "=== Protogate Local Tunnel Test ==="
echo ""

# Kill any existing processes
echo "1. Cleaning up existing processes..."
pkill -9 protogate-server 2>/dev/null || true
pkill -9 tunnel-agent 2>/dev/null || true
pkill -9 -f "http.server" 2>/dev/null || true
sleep 1

# Start server
echo "2. Starting server..."
cd "$REPO_ROOT"
env KEY_VAULT_URI=https://mock-vault.vault.azure.net/ DNS_ZONE=tunnel.local \
  ./build/protogate-server > /tmp/server.log 2>&1 &
SERVER_PID=$!
echo "   Server PID: $SERVER_PID"
sleep 2

# Check server started
if ! tail -5 /tmp/server.log | grep -q "Protogate server ready"; then
    echo "   ERROR: Server failed to start"
    tail -20 /tmp/server.log
    exit 1
fi
echo "   ✓ Server started on ports 443, 8080, 8443"

# Start test service
echo "3. Starting test HTTP service..."
mkdir -p /tmp/testdir
cd /tmp/testdir
python3 -m http.server 3000 > /tmp/test-service.log 2>&1 &
TEST_PID=$!
echo "   Test service PID: $TEST_PID"
sleep 1
echo "   ✓ Test service running on port 3000"

# Start agent
echo "4. Starting tunnel agent..."
cd "$REPO_ROOT/tunnel-agent"
./build/tunnel-agent --config config.json > /tmp/agent.log 2>&1 &
AGENT_PID=$!
echo "   Agent PID: $AGENT_PID"
sleep 3

# Check agent connected
if ! tail -20 /tmp/agent.log | grep -q "Frame received"; then
    echo "   ERROR: Agent failed to connect"
    tail -30 /tmp/agent.log
    exit 1
fi
echo "   ✓ Agent connected via HTTP/2"

# Test tunnel
echo "5. Testing HTTP tunnel..."
RESPONSE=$(curl -s -H 'Host: test-api' --max-time 5 http://localhost:443/ 2>&1)
if echo "$RESPONSE" | grep -q "DOCTYPE HTML"; then
    echo "   ✓ Tunnel working! Received HTML response from local service"
else
    echo "   ERROR: Tunnel test failed"
    echo "   Response: $RESPONSE"
    exit 1
fi

echo ""
echo "=== All Tests Passed ==="
echo ""
echo "Running processes:"
echo "  Server:       PID $SERVER_PID (logs: /tmp/server.log)"
echo "  Agent:        PID $AGENT_PID (logs: /tmp/agent.log)"
echo "  Test service: PID $TEST_PID (logs: /tmp/test-service.log)"
echo ""
echo "To test manually:"
echo "  curl -H 'Host: test-api' http://localhost:443/"
echo ""
echo "To stop all:"
echo "  kill $SERVER_PID $AGENT_PID $TEST_PID"
