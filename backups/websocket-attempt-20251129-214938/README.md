# WebSocket Attempt Backup - November 29, 2025

## Context
This backup captures the WebSocket protocol implementation attempt for Azure Container Apps deployment.

## Problem Summary
- **Original Issue**: Azure HTTP ingress blocks custom `Upgrade: protogate-agent` protocol (HTTP 403)
- **Solution Attempted**: WebSocket protocol (Azure whitelisted, gets HTTP 401 instead of 403)
- **Result**: WebSocket upgrade succeeds (HTTP 101) but connection closes immediately with "End of file"
- **Root Cause**: Protocol mismatch - nghttp2 sends raw HTTP/2 frames, WebSocket expects framed data with headers/masking

## Files Modified

### Agent Side
- `http_client.cpp` - Changed to send WebSocket upgrade headers (lines 95-103)
  - Added: `Upgrade: websocket`, `Sec-WebSocket-Version: 13`, `Sec-WebSocket-Key`
  - Fixed: Header validation bug (lines 175-179) - changed from stream re-read to string search

### Server Side
- `tunnels_handler.cpp` - Changed to respond with WebSocket 101 (lines 493-498)
  - Added: `upgrade: websocket`, `sec-websocket-accept`, proper HTTP 101 status
- `agent_server.cpp` - Added unused WebSocket response method (lines 362-382)
  - Note: Dead code, actual response sent from tunnels_handler.cpp
- `agent_server.h` - Added declaration for unused method
- `http_server.cpp` - Fixed lambda capture warning (line 185)

### Documentation
- `AZURE_INGRESS_ANALYSIS.md` - Complete investigation of Azure ingress protocol limitations
  - Protocol test results: custom (403), WebSocket (401), h2c (401)
  - Azure documentation references
  - Solution comparison matrix

## Test Results

### What Worked
✅ TCP connection established
✅ TLS handshake complete (HTTP/1.1 via ALPN)
✅ WebSocket upgrade request sent
✅ Server responds with HTTP 101 Switching Protocols
✅ Headers validated correctly: `upgrade: websocket`, `sec-websocket-accept`
✅ Agent logs "HTTP upgrade successful"

### What Failed
❌ Connection closes immediately after upgrade
❌ Error: "End of file" (bytes: 0)
❌ Agent enters reconnect loop

### Root Cause Analysis
Agent uses `HTTP2Session` with nghttp2 library which sends:
- Raw HTTP/2 binary frames (SETTINGS, HEADERS, DATA)
- No WebSocket framing headers (FIN, opcode, mask, length)
- No payload masking (XOR with 32-bit key)

WebSocket protocol requires:
- Frame header: 2-14 bytes (FIN bit, opcode, mask bit, payload length)
- Masking key: 4 bytes (for client→server)
- Payload: XOR masked data
- Close handshake: Proper WebSocket close frames

**Conclusion**: Cannot send raw HTTP/2 frames over WebSocket connection without implementing complete WebSocket framing layer.

## Deployment History
- 2 full Azure deployments (90 minutes total build + push time)
- Multiple agent rebuilds and tests
- Verified end-to-end: Agent gets 101 response but protocol mismatch causes disconnect

## Next Steps (Not Implemented)

### Option 1: h2c (HTTP/2 Cleartext) - RECOMMENDED
- Time: 15 minutes
- Change headers to `Upgrade: h2c` + `HTTP2-Settings`
- Compatible with existing nghttp2 code
- Risk: Need to verify nghttp2_session_upgrade2() initialization

### Option 2: WebSocket Framing Layer
- Time: 2-3 hours
- Implement WebSocketFramer class
- Wrap all HTTP/2 frames in WebSocket frames
- Add masking/unmasking logic
- High complexity, performance overhead

### Option 3: Direct HTTP/2 (Skip Upgrade)
- Time: 4-5 hours
- Major refactor: Remove HTTP/1.1 upgrade
- Use ALPN to negotiate h2 directly
- Unknown Azure support via ALPN

## Lessons Learned
1. Azure Container Apps HTTP ingress has protocol whitelist (WebSocket ✅, h2c ✅, custom ❌)
2. Protocol compatibility must be verified at architecture planning phase
3. WebSocket framing is incompatible with raw HTTP/2 binary frames
4. h2c is likely better fit for HTTP/2-based communication after HTTP/1.1 upgrade

## References
- AZURE_INGRESS_ANALYSIS.md - Full investigation document
- Agent logs: /tmp/agent-final.log (final test run)
- Server deployment: protogate-test-server.whitesea-e4a76aae.westus2.azurecontainerapps.io
