#!/bin/bash
# Simple script to start Protogate local test environment

echo "🚀 Starting Protogate Local Test"
echo "================================"
echo ""

# Kill any existing instances
pkill -f protogate-server 2>/dev/null || true
pkill -f tunnel-agent 2>/dev/null || true
pkill -f "python3.*http.server.*3000" 2>/dev/null || true
sleep 2

# Start Python HTTP server on port 3000
echo "1. Starting local test service on port 3000..."
cd /tmp && python3 -m http.server 3000 > /tmp/test-service.log 2>&1 &
SERVICE_PID=$!
sleep 2
echo "   ✓ Service running (PID: $SERVICE_PID)"

# Start Protogate server
echo "2. Starting Protogate server..."
cd /Volumes/Projects/protogate
PORT=8080 \
AGENT_PORT=8443 \
KEY_VAULT_URI="https://mock.vault.azure.net/" \
DNS_ZONE="tunnel.local" \
LOG_ANALYTICS_WORKSPACE_ID="mock-id" \
AZURE_STORAGE_CONNECTION_STRING="mock" \
./build/protogate-server > /tmp/protogate-server.log 2>&1 &
SERVER_PID=$!
sleep 3
echo "   ✓ Server running (PID: $SERVER_PID)"
echo "   - HTTP/HTTPS: localhost:8080"
echo "   - Agent port: localhost:8443"

# Start tunnel agent
echo "3. Starting tunnel agent..."
cd /Volumes/Projects/protogate/tunnel-agent
./build/tunnel-agent \
  --server localhost:8443 \
  --tunnel-id test-api \
  --token tnl_local_test_123 \
  --local-url http://localhost:3000 \
  --no-verify-tls \
  > /tmp/protogate-agent.log 2>&1 &
AGENT_PID=$!
sleep 3
echo "   ✓ Agent running (PID: $AGENT_PID)"

echo ""
echo "================================"
echo "✅ All services started!"
echo "================================"
echo ""
echo "Components:"
echo "  - Server: PID $SERVER_PID"
echo "  - Agent:  PID $AGENT_PID"  
echo "  - Service: PID $SERVICE_PID"
echo ""
echo "Logs:"
echo "  - Server: /tmp/protogate-server.log"
echo "  - Agent:  /tmp/protogate-agent.log"
echo "  - Service: /tmp/test-service.log"
echo ""
echo "Test the tunnel:"
echo "  curl http://localhost:8080/test-api/"
echo ""
echo "Stop all services:"
echo "  kill $SERVER_PID $AGENT_PID $SERVICE_PID"
echo ""
echo "Or use: pkill -f protogate-server && pkill -f tunnel-agent && pkill -f 'python3.*3000'"
echo ""

# Wait and show agent connection status
sleep 2
echo "Agent connection status:"
tail -5 /tmp/protogate-agent.log | grep -E "(connected|ready|error)" || echo "Check /tmp/protogate-agent.log for details"
