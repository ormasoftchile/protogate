#!/bin/bash
# Test TCP tunnel E2E
# This script sends an HTTP request through the TCP tunnel and captures the response

echo "Testing TCP tunnel on port 9100..."
echo ""

# Send HTTP request and wait for response
(echo -e "GET / HTTP/1.0\r\n\r\n"; sleep 2) | nc localhost 9100

echo ""
echo "Test complete. If you see HTTP response above, TCP tunnel is working!"
