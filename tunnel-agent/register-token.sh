#!/bin/bash
# Register test token with Protogate server

TUNNEL_ID="${1:-test-api}"
TOKEN="${2:-tnl_local_test_123}"

# For MVP, the server doesn't have a management API yet
# Tokens need to be pre-configured or added via database

echo "Note: Token registration via API not yet implemented"
echo "The server needs tokens to be pre-configured in the token cache"
echo ""
echo "For testing, you can:"
echo "1. Modify server code to add test tokens at startup"
echo "2. Implement the management API endpoints"
echo "3. Use database seeding with test tokens"
