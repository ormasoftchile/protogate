#!/bin/bash
# Quick local test script for Protogate server
# This runs the server with minimal configuration for testing

set -e

echo "🚀 Starting Protogate Server (Local Test Mode)"
echo ""
echo "Configuration:"
echo "  PORT: 8080 (HTTP ingress)"
echo "  AGENT_PORT: 8443 (Agent connections)"
echo "  DNS_ZONE: tunnel.local"
echo "  LOG_LEVEL: INFO"
echo ""

export KEY_VAULT_URI="https://test-vault.vault.azure.net/"
export DNS_ZONE="tunnel.local"
export PORT=8080
export AGENT_PORT=8443
export LOG_LEVEL=INFO

echo "⚠️  Note: Using mock Key Vault URI - TLS features will not work"
echo "⚠️  This is for basic connectivity testing only"
echo ""
echo "Press Ctrl+C to stop the server"
echo ""

./build/protogate-server
