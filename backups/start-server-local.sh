#!/bin/bash
# Quick script to start protogate-server with valid mock configuration
# This avoids common configuration validation errors

cd "$(dirname "$0")"

export PORT=8080
export AGENT_PORT=8443
export TCP_PORTS=9100
export KEY_VAULT_URI="https://mock.vault.azure.net"
export DNS_ZONE="tunnel.local"
export LOG_ANALYTICS_WORKSPACE_ID="mock-id"
export AZURE_STORAGE_CONNECTION_STRING="mock"
export TOKEN_TYPE="mock"
export TUNNEL_ID="test-api"

echo "Starting protogate-server with mock configuration..."
echo "Logs: /tmp/protogate-server.log"
./build/protogate-server > /tmp/protogate-server.log 2>&1 &

sleep 2
tail -20 /tmp/protogate-server.log
