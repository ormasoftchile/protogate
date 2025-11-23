# Protogate Quickstart Guide

Your server is running! Here's how to use it.

## Current Status

Your server is listening on:
- **Port 8080**: HTTP ingress (for tunnel traffic)
- **Port 8443**: Agent connections (for tunnel agents to connect)

## What You Need

Protogate is a **reverse tunnel server**. To actually use it, you need:

1. **This server** ✅ (You have this running!)
2. **A tunnel agent** (A client that connects from your private network)
3. **A backend service** (The service you want to expose)

Think of it like this:
```
Internet Client → Protogate Server (8080) → Tunnel Agent (8443) → Your Local Service
     (you)           (running now)              (MISSING)            (your app)
```

## Testing Without an Agent (Limited)

You can test the Management API endpoints directly:

### 1. Health Check
```bash
curl http://localhost:8080/health
```

### 2. Create a Tunnel
```bash
curl -X POST http://localhost:8080/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "my-test-app",
    "protocol": "HTTP",
    "target": "localhost:3000",
    "dns_subdomain": "my-test-app"
  }'
```

### 3. List All Tunnels
```bash
curl http://localhost:8080/v1/tunnels
```

### 4. Get Specific Tunnel
```bash
curl http://localhost:8080/v1/tunnels/my-test-app
```

### 5. Delete Tunnel
```bash
curl -X DELETE http://localhost:8080/v1/tunnels/my-test-app
```

## What's Missing: The Tunnel Agent

To actually route traffic, you need a **tunnel agent** that:
1. Connects to port 8443 with a valid token
2. Establishes a persistent TLS connection
3. Forwards requests to your local service
4. Sends responses back through the tunnel

### Simple Test Agent (Python Example)

Here's a minimal agent to test with:

```python
#!/usr/bin/env python3
import socket
import ssl
import sys

AGENT_HOST = "localhost"
AGENT_PORT = 8443
TUNNEL_ID = "my-test-app"
TOKEN = "test-token-12345678901234567890123456789012"

# Connect to agent server
context = ssl.create_default_context()
context.check_hostname = False
context.verify_mode = ssl.CERT_NONE  # For testing only!

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ssl_sock = context.wrap_socket(sock, server_hostname=AGENT_HOST)

try:
    ssl_sock.connect((AGENT_HOST, AGENT_PORT))
    print(f"Connected to {AGENT_HOST}:{AGENT_PORT}")
    
    # Send authentication
    auth_msg = f"TUNNEL {TUNNEL_ID}\r\nAuthorization: Bearer {TOKEN}\r\n\r\n"
    ssl_sock.send(auth_msg.encode())
    
    # Wait for response
    response = ssl_sock.recv(4096)
    print(f"Server response: {response.decode()}")
    
    # Keep connection alive
    input("Press Enter to disconnect...")
    
finally:
    ssl_sock.close()
```

Save as `test-agent.py` and run:
```bash
python3 test-agent.py
```

## Full End-to-End Test

### Step 1: Start a local web service
```bash
# Simple Python HTTP server on port 3000
python3 -m http.server 3000
```

### Step 2: Create tunnel via API
```bash
curl -X POST http://localhost:8080/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "my-web-server",
    "protocol": "HTTP",
    "target": "localhost:3000"
  }'
```

### Step 3: Connect agent (you need to build this)
The agent would connect to port 8443 and forward traffic to localhost:3000

### Step 4: Access via tunnel
```bash
# This would work if the agent was connected:
curl -H "Host: my-web-server.tunnel.local" http://localhost:8080/
```

## What Works Now vs What Doesn't

✅ **Works**:
- Server starts and listens on ports
- Management API endpoints (create/list/delete tunnels)
- Configuration via environment variables
- Graceful shutdown (Ctrl+C)
- Structured JSON logging
- Token validation infrastructure

❌ **Doesn't Work Yet** (needs agent):
- Actual HTTP request tunneling
- TCP port forwarding
- TLS certificate loading (needs real Azure Key Vault)
- End-to-end traffic forwarding

## Next Steps

To make this fully functional, you need to either:

1. **Build a tunnel agent** - A client program that:
   - Connects to your Protogate server on port 8443
   - Authenticates with a token
   - Forwards requests to local services
   - Returns responses through the tunnel

2. **Use with Azure** - Deploy to Azure where:
   - Azure Key Vault provides TLS certificates
   - Azure DNS provides wildcard domain routing
   - You can access via real domains like `*.tunnel.yourcompany.com`

3. **Check existing agent implementations** - Look for compatible agents like:
   - `localtunnel` protocol
   - `ngrok` agent protocol
   - Build your own following the [agent protocol spec](specs/001-tunnel-core-server/contracts/agent-protocol.md)

## Useful Commands

### View logs
```bash
# Logs are printed to stdout in JSON format
# Just watch the terminal where server is running
```

### Stop server
```bash
# Gracefully:
pkill -INT protogate-server

# Or just Ctrl+C in the terminal
```

### Rebuild after changes
```bash
cmake --build build --target protogate-server
```

### Run tests
```bash
./build/unit_tests          # 85 unit tests
./build/integration_tests   # 124 integration tests
./build/security_tests      # Security fuzzing tests
```

## Need Help?

- Read the [architecture docs](docs/architecture.md)
- Check the [agent protocol spec](specs/001-tunnel-core-server/contracts/agent-protocol.md)
- Review the [management API spec](specs/001-tunnel-core-server/contracts/management-api.yaml)
- See [security documentation](docs/security.md)
