#!/bin/bash
# Start tunnel-agent for local testing
# This script prevents path-related errors by using absolute paths

set -e

# Always use absolute paths
REPO_ROOT="/Volumes/Projects/protogate"
AGENT_BIN="${REPO_ROOT}/tunnel-agent/build/tunnel-agent"

# Check if binary exists
if [ ! -f "$AGENT_BIN" ]; then
    echo "ERROR: Agent binary not found at: $AGENT_BIN"
    echo "Build it first: cd $REPO_ROOT && cmake --build build --target tunnel-agent"
    exit 1
fi

# Check if server is running
if ! pgrep -f "protogate-server" > /dev/null; then
    echo "WARNING: protogate-server not running. Start it first:"
    echo "  ${REPO_ROOT}/start-server-local.sh"
    echo ""
fi

# Check if target HTTP server is running
if ! nc -z localhost 3000 2>/dev/null; then
    echo "WARNING: Target HTTP server not running on port 3000"
    echo "Start it: cd /tmp && python3 -m http.server 3000 &"
    echo ""
fi

echo "Starting tunnel-agent..."
echo "Logs: /tmp/protogate-agent.log"

cd "$REPO_ROOT"

"$AGENT_BIN" \
  --server-url https://localhost:8443 \
  --tunnel-id test-api \
  --token tnl_local_test_123 \
  --agent-id test-agent-1 \
  --target-host localhost \
  --target-port 3000 \
  --tcp-forwarding \
  --tcp-ports 9100 \
  --no-verify-tls \
  > /tmp/protogate-agent.log 2>&1 &

AGENT_PID=$!
echo "Agent started (PID: $AGENT_PID)"
echo "Monitor: tail -f /tmp/protogate-agent.log"
