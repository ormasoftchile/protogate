# Protogate Project Structure

## Important: Multi-Directory Layout

This project has **TWO main codebases**:

### 1. Server (Root Level)
- `src/` - C++ server source code
- `docker/` - Dockerfile configurations
- `build/` - Compiled server binary
- `.specify/scripts/` - Project automation scripts

### 2. Tunnel Agent (Subdirectory)
- **Location**: `tunnel-agent/` (NOT just `tunnel-agent/src/`)
- **Full structure**:
  - `src/` - C++ agent source code
  - `build/` - Compiled agent binary
  - `scripts/` - Agent-specific scripts
  - `tests/` - Agent test files
  - `*.md` - Comprehensive documentation files:
    - `README.md` - Main agent documentation
    - `HTTP2_COMPATIBILITY.md` - Protocol implementation details
    - `IMPLEMENTATION_COMPLETE.md` - Completion status
    - `TESTING.md` - Test procedures
    - `SUMMARY.md` - Feature summary
    - `LOCAL_TESTING_STATUS.md` - Local test results
  - `*.json` - Configuration files (vcpkg, config)
  - `*.sh` - Shell scripts (register-token.sh, test-*.sh)

### 3. Specifications
- `specs/` - Feature specifications (spec-kit format)
  - `001-tunnel-core-server/` - Completed feature
  - `002-management-api-http-proxy/` - Current feature

## Key Points for AI Assistants

1. **When working with tunnel-agent**: Always consider the ENTIRE `tunnel-agent/` directory, not just `tunnel-agent/src/`
2. **Documentation matters**: The `.md` files contain critical context about implementation status, testing, and design decisions
3. **Build artifacts**: Both `build/` and `tunnel-agent/build/` contain compiled binaries
4. **Test scripts**: Check both `tunnel-agent/test-*.sh` and `.specify/scripts/bash/test-*.sh`

## Common Mistakes to Avoid

- ❌ "tunnel-agent only has src/" - NO, it has docs, tests, scripts, configs
- ❌ Ignoring `tunnel-agent/*.md` files when analyzing completeness
- ❌ Missing `tunnel-agent/build/tunnel-agent` binary in tests
- ✅ Always `list_dir` on `tunnel-agent/` to see full structure
- ✅ Read `IMPLEMENTATION_COMPLETE.md` for status verification
- ✅ Check `test-local.sh` for existing test procedures
