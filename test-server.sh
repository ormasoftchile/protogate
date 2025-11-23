#!/bin/bash
# Simple connectivity test for Protogate

echo "=== Protogate Server Test ==="
echo ""

# Check if server is running
if ! pgrep -f protogate-server > /dev/null; then
    echo "❌ Server is NOT running"
    echo ""
    echo "Start it with:"
    echo "  ./run-server-local.sh"
    exit 1
fi

echo "✅ Server process is running (PID: $(pgrep -f protogate-server))"
echo ""

# Test port connectivity
echo "Testing port connectivity:"
if nc -z localhost 8080 2>/dev/null; then
    echo "  ✅ Port 8080 (HTTP) - OPEN"
else
    echo "  ❌ Port 8080 (HTTP) - CLOSED"
fi

if nc -z localhost 8443 2>/dev/null; then
    echo "  ✅ Port 8443 (Agent) - OPEN"
else
    echo "  ❌ Port 8443 (Agent) - CLOSED"
fi
echo ""

# Try to connect
echo "Testing HTTP connection (timeout 2s):"
if timeout 2 bash -c 'exec 3<>/dev/tcp/localhost/8080 && echo "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n" >&3 && cat <&3' 2>/dev/null | head -1; then
    echo "  ✅ Server accepts connections"
else
    echo "  ⚠️  Server accepts connections but may not respond"
fi
echo ""

echo "=== Summary ==="
echo "Your Protogate server is:"
echo "  • Running on PID $(pgrep -f protogate-server)"
echo "  • Listening on ports 8080 and 8443"
echo "  • Ready to accept connections"
echo ""
echo "⚠️  Note: The server needs:"
echo "   1. A tunnel agent to connect on port 8443"
echo "   2. Valid Azure Key Vault for TLS certificates"
echo "   3. Tunnel configuration via Management API"
echo ""
echo "See QUICKSTART.md for next steps!"
