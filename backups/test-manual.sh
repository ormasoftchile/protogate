#!/bin/bash
# Simple manual test of the integration fix

export KEY_VAULT_URI="https://mock-kv.vault.azure.net"
export DNS_ZONE="test.local"
export AZURE_LOG_LEVEL="WARNING"

cd /Volumes/Projects/protogate

echo "Starting server on port 8080 (HTTP) and 8443 (Agent)..."
echo "Press Ctrl+C to stop"
echo ""

./build/protogate-server \
  --listen-port 8080 \
  --agent-port 8443 \
  --health-port 9090
