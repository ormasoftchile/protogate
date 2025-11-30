#!/bin/bash
# Test AgentHandshake integration fix locally

set -e

echo "================================="
echo "Integration Fix Test"
echo "================================="
echo ""

# Configuration
LISTEN_PORT=8080
AGENT_PORT=8443
TEST_TOKEN="tnl_test_$(openssl rand -hex 16)"
TUNNEL_ID="integration-test"
LOCAL_PORT=9999

echo "1. Building server with fix..."
cd /Volumes/Projects/protogate
cmake --build build --target protogate-server -j $(sysctl -n hw.ncpu)
echo "   ✅ Build successful"
echo ""

echo "2. Creating test config..."
cat > /tmp/protogate-test-config.json <<EOF
{
  "listen_address": "0.0.0.0",
  "listen_port": $LISTEN_PORT,
  "agent_port": $AGENT_PORT,
  "health_port": 9090,
  "key_vault_uri": "https://mock-kv.vault.azure.net",
  "log_level": "DEBUG",
  "max_agents": 50
}
EOF
echo "   ✅ Config created"
echo ""

echo "3. Starting local HTTP server on port $LOCAL_PORT..."
python3 -m http.server $LOCAL_PORT > /tmp/test-http-server.log 2>&1 &
HTTP_SERVER_PID=$!
sleep 2
echo "   ✅ HTTP server running (PID: $HTTP_SERVER_PID)"
echo ""

echo "4. Starting protogate server..."
export KEY_VAULT_URI="https://mock-kv.vault.azure.net"
export AZURE_LOG_LEVEL="WARNING"
export DNS_ZONE="test.local"
./build/protogate-server \
  --config /tmp/protogate-test-config.json > /tmp/protogate-server.log 2>&1 &
SERVER_PID=$!
sleep 3

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "   ❌ Server failed to start"
    cat /tmp/protogate-server.log
    kill $HTTP_SERVER_PID 2>/dev/null || true
    exit 1
fi
echo "   ✅ Server running (PID: $SERVER_PID)"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    kill $HTTP_SERVER_PID 2>/dev/null || true
    kill $SERVER_PID 2>/dev/null || true
    kill $AGENT_PID 2>/dev/null || true
    rm -f /tmp/protogate-test-config.json
}
trap cleanup EXIT

echo "5. Creating tunnel via Management API..."
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST http://localhost:$LISTEN_PORT/v1/tunnels \
  -H "Content-Type: application/json" \
  -d "{\"tunnel_id\":\"$TUNNEL_ID\",\"target_host\":\"localhost\",\"target_port\":$LOCAL_PORT}")

HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
BODY=$(echo "$RESPONSE" | head -n-1)

if [ "$HTTP_CODE" != "201" ]; then
    echo "   ❌ Failed to create tunnel (HTTP $HTTP_CODE)"
    echo "   Response: $BODY"
    echo ""
    echo "Server logs:"
    tail -30 /tmp/protogate-server.log
    exit 1
fi

if echo "$BODY" | jq -e .token > /dev/null 2>&1; then
    TOKEN=$(echo "$BODY" | jq -r .token)
    echo "   ✅ Tunnel created"
    echo "   Token: ${TOKEN:0:16}..."
else
    echo "   ❌ Failed to parse token from response"
    echo "   Response: $BODY"
    exit 1
fi
echo ""

echo "6. Starting tunnel agent..."
cd /Volumes/Projects/protogate/tunnel-agent
./build/tunnel-agent \
  --server localhost:$AGENT_PORT \
  --tunnel-id $TUNNEL_ID \
  --token $TOKEN \
  --local-url http://localhost:$LOCAL_PORT \
  --insecure > /tmp/agent.log 2>&1 &
AGENT_PID=$!
cd /Volumes/Projects/protogate
sleep 5

if ! kill -0 $AGENT_PID 2>/dev/null; then
    echo "   ❌ Agent failed to start or crashed"
    echo "   Last 20 lines of agent log:"
    tail -20 /tmp/agent.log
    exit 1
fi
echo "   ✅ Agent running (PID: $AGENT_PID)"
echo ""

echo "7. Checking agent status..."
sleep 2
if tail -10 /tmp/agent.log | grep -q "End of file"; then
    echo "   ❌ FAILED: Agent showing 'End of file' errors (reconnect loop)"
    echo "   Agent log:"
    tail -20 /tmp/agent.log
    exit 1
elif tail -10 /tmp/agent.log | grep -q "Connected to server"; then
    echo "   ✅ SUCCESS: Agent connected successfully (no reconnect loop)"
else
    echo "   ⚠️  WARNING: Unable to determine agent status from logs"
    echo "   Agent log:"
    tail -20 /tmp/agent.log
fi
echo ""

echo "8. Testing tunnel proxy..."
sleep 1
PROXY_RESPONSE=$(curl -s -H "Host: $TUNNEL_ID" http://localhost:$LISTEN_PORT/ 2>&1)

if echo "$PROXY_RESPONSE" | grep -q "Directory listing"; then
    echo "   ✅ SUCCESS: Tunnel working! Received response from local HTTP server"
    echo "   Response preview: $(echo "$PROXY_RESPONSE" | head -1)"
else
    echo "   ❌ FAILED: Did not receive response from local server"
    echo "   Response: ${PROXY_RESPONSE:0:200}"
    echo ""
    echo "Server logs:"
    tail -30 /tmp/protogate-server.log
fi
echo ""

echo "================================="
echo "Test Complete"
echo "================================="
echo ""
echo "Log files:"
echo "  - Server: /tmp/protogate-server.log"
echo "  - Agent: /tmp/agent.log"
echo "  - HTTP Server: /tmp/test-http-server.log"
echo ""
