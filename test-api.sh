#!/bin/bash
echo "Testing Protogate Management API"
echo "================================="
echo ""

echo "1. Health Check:"
curl -s http://localhost:8080/health | jq . || echo "No response"
echo ""
echo ""

echo "2. Create a tunnel:"
curl -s -X POST http://localhost:8080/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "test-app",
    "protocol": "HTTP",
    "target": "localhost:3000",
    "dns_subdomain": "test-app"
  }' | jq . || echo "Failed"
echo ""
echo ""

echo "3. List all tunnels:"
curl -s http://localhost:8080/v1/tunnels | jq . || echo "Failed"
echo ""
echo ""

echo "4. Get specific tunnel:"
curl -s http://localhost:8080/v1/tunnels/test-app | jq . || echo "Failed"
echo ""
echo ""

echo "5. Get tunnel metrics:"
curl -s http://localhost:8080/v1/tunnels/test-app/metrics | jq . || echo "Failed"
echo ""
echo ""

echo "6. Delete tunnel:"
curl -s -X DELETE http://localhost:8080/v1/tunnels/test-app | jq . || echo "Failed"
echo ""

