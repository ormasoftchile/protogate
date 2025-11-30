# Restore Instructions

## To Restore All Files

Run these commands from repository root:

```bash
cd /Volumes/Projects/protogate
BACKUP_DIR="backups/websocket-attempt-20251129-214938"

# Restore modified files
cp "$BACKUP_DIR/settings.json" .vscode/settings.json
cp "$BACKUP_DIR/.managed-identity-test" azure/.managed-identity-test
cp "$BACKUP_DIR/spec.md" specs/001-azure-deployment-test/spec.md
cp "$BACKUP_DIR/tasks.md" specs/001-azure-deployment-test/tasks.md
cp "$BACKUP_DIR/tunnels_handler.cpp" src/api/tunnels_handler.cpp
cp "$BACKUP_DIR/tunnels_handler.h" src/api/tunnels_handler.h
cp "$BACKUP_DIR/agent_server.cpp" src/server/agent_server.cpp
cp "$BACKUP_DIR/agent_server.h" src/server/agent_server.h
cp "$BACKUP_DIR/http_server.cpp" src/server/http_server.cpp
cp "$BACKUP_DIR/http_server.h" src/server/http_server.h
cp "$BACKUP_DIR/server_main.cpp" src/server/main.cpp
cp "$BACKUP_DIR/CMakeLists.txt" tunnel-agent/CMakeLists.txt
cp "$BACKUP_DIR/config.json" tunnel-agent/config.json
cp "$BACKUP_DIR/http_client.cpp" tunnel-agent/src/client/http_client.cpp
cp "$BACKUP_DIR/http_client.h" tunnel-agent/src/client/http_client.h
cp "$BACKUP_DIR/http2_session.cpp" tunnel-agent/src/client/http2_session.cpp
cp "$BACKUP_DIR/http2_session.h" tunnel-agent/src/client/http2_session.h
cp "$BACKUP_DIR/agent_main.cpp" tunnel-agent/src/main.cpp

# Restore documentation
cp "$BACKUP_DIR/AZURE_INGRESS_ANALYSIS.md" specs/001-azure-deployment-test/AZURE_INGRESS_ANALYSIS.md
cp "$BACKUP_DIR/IMPLEMENTATION_SUMMARY.md" specs/001-azure-deployment-test/IMPLEMENTATION_SUMMARY.md
cp "$BACKUP_DIR/INTEGRATION_GAPS.md" specs/001-azure-deployment-test/INTEGRATION_GAPS.md
cp "$BACKUP_DIR/HANDS_ON_VERIFICATION.md" ./HANDS_ON_VERIFICATION.md
cp "$BACKUP_DIR/SPEC_CONSOLIDATION.md" ./SPEC_CONSOLIDATION.md

# Restore test scripts
cp "$BACKUP_DIR/test-integration-fix.sh" ./test-integration-fix.sh
cp "$BACKUP_DIR/test-manual.sh" ./test-manual.sh

echo "All files restored from backup"
```

## File Inventory

### Agent Files (tunnel-agent/)
- http_client.cpp / .h - WebSocket upgrade implementation
- http2_session.cpp / .h - HTTP/2 session with nghttp2
- agent_main.cpp - Agent entry point
- CMakeLists.txt - Build configuration
- config.json - Agent configuration

### Server Files (src/)
- api/tunnels_handler.cpp / .h - WebSocket 101 response
- server/agent_server.cpp / .h - Agent authentication
- server/http_server.cpp / .h - HTTP server
- server/server_main.cpp - Server entry point

### Configuration
- .vscode/settings.json - VSCode settings
- azure/.managed-identity-test - Azure identity

### Documentation
- specs/001-azure-deployment-test/spec.md - Feature spec
- specs/001-azure-deployment-test/tasks.md - Task breakdown
- specs/001-azure-deployment-test/AZURE_INGRESS_ANALYSIS.md - Protocol investigation
- specs/001-azure-deployment-test/IMPLEMENTATION_SUMMARY.md - Work summary
- specs/001-azure-deployment-test/INTEGRATION_GAPS.md - Integration issues
- HANDS_ON_VERIFICATION.md - Testing documentation
- SPEC_CONSOLIDATION.md - Spec consolidation
- README.md - This backup's overview
- GIT_STATUS.txt - Git state at backup

### Test Scripts
- test-integration-fix.sh - Integration test
- test-manual.sh - Manual test
