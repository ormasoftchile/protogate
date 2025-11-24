# Local Testing Guide

This guide shows how to test the tunnel agent locally with the Protogate server.

## Quick Start (Automated)

Run the automated test script:

```bash
cd tunnel-agent
./test-local.sh
```

This will:
1. Build server and agent (if needed)
2. Start the Protogate server
3. Start a local test service (Python HTTP server)
4. Start the tunnel agent
5. Show logs and keep everything running

Press `Ctrl+C` to stop all services.

---

## Manual Testing (Step by Step)

### Terminal 1: Start the Server

```bash
cd protogate
cmake --build build --target protogate-server
./build/protogate-server
```

Verify: `curl -k https://localhost:8080/health`

### Terminal 2: Start a Local Service

```bash
# Option A: Python simple HTTP server
cd /tmp
python3 -m http.server 3000

# Option B: Node.js/Express app
cd your-app
npm start

# Option C: Any other service on port 3000
```

Verify: `curl http://localhost:3000`

### Terminal 3: Start the Tunnel Agent

```bash
cd protogate/tunnel-agent

# Using config file
./build/tunnel-agent --config config.json

# Or using CLI args
./build/tunnel-agent \
  --server localhost:8443 \
  --token tnl_test_token_123 \
  --tunnel-id test-api \
  --local-url http://localhost:3000

# Or using environment variables
export TUNNEL_SERVER=localhost:8443
export TUNNEL_TOKEN=tnl_test_token_123
export TUNNEL_ID=test-api
export LOCAL_URL=http://localhost:3000
./build/tunnel-agent
```

### Terminal 4: Test Requests

Currently, the server needs tunnel management API to register tunnels. For now, you can verify:

```bash
# Check agent logs for successful connection
# You should see:
#   "Connecting to server" 
#   "TCP connected, starting TLS handshake"
#   "TLS handshake complete"
#   "Sent CONNECT request"
```

---

## What to Look For

### ✅ Success Indicators

**Server logs:**
- `Server listening on port 8080 (HTTPS)`
- `Agent server listening on port 8443`

**Agent logs:**
- `Starting Protogate Tunnel Agent`
- `Connecting to server`
- `TLS handshake complete`
- `HTTP/2 session started`
- `Agent connected and ready`

### ⚠️ Common Issues

**Connection Refused**
```
Error: Connection refused to localhost:8443
```
→ Server not running. Start it first.

**TLS Handshake Failed**
```
Error: TLS handshake failed
```
→ Check server is using TLS. Disable `verify_tls` in agent config.

**401 Unauthorized**
```
Error: 401 Unauthorized - Invalid token
```
→ Server is rejecting the token. Need to implement tunnel management API first.

**Local Service Unavailable**
```
Error: 502 Bad Gateway
```
→ Local service not running on configured port.

---

## Testing Individual Components

### Test Agent Build

```bash
cd tunnel-agent
./build/tunnel-agent --help
```

Should show help text.

### Test Agent Config

```bash
# Test with invalid config
./build/tunnel-agent --config /nonexistent
# Should error: "Failed to open config file"

# Test with valid config
./build/tunnel-agent --config config.json
# Should start connecting
```

### Test TLS Connection

```bash
# Agent should connect to server on port 8443
# Check with:
lsof -i :8443
# Should show both server and agent processes
```

---

## Next Steps

To fully test end-to-end request flow:

1. **Implement Tunnel Management API** on server
   - POST `/api/tunnels` - Create tunnel, return token
   - GET `/api/tunnels/:id` - Get tunnel info
   - DELETE `/api/tunnels/:id` - Delete tunnel

2. **Register Tunnel via API**
   ```bash
   curl -X POST https://localhost:8080/api/tunnels \
     -H "Content-Type: application/json" \
     -d '{"id": "test-api", "subdomain": "myapp"}'
   ```

3. **Test Request Through Tunnel**
   ```bash
   curl https://myapp.localhost:8080/
   # Should forward to local service and return response
   ```

---

## Logs

All logs are in `/tmp/`:
- `/tmp/protogate-server.log` - Server logs
- `/tmp/protogate-agent.log` - Agent logs

View live:
```bash
tail -f /tmp/protogate-server.log
tail -f /tmp/protogate-agent.log
```

---

## Cleanup

Stop all services:
```bash
pkill -f protogate-server
pkill -f tunnel-agent
pkill -f "python3 -m http.server"
```

Remove temp files:
```bash
rm /tmp/protogate-*.log
rm /tmp/test-agent-config.json
```
