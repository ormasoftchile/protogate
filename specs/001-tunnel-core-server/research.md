# Phase 0: Research - Protogate Core Server

**Created**: 2025-11-22  
**Purpose**: Resolve all NEEDS CLARIFICATION items from Technical Context and research best practices for implementation decisions.

---

## Research Tasks

### 1. C++ Async I/O Library Selection

**Decision**: Use **Boost.Asio** for async networking

**Rationale**:
- Mature, production-proven library (used by MongoDB, WebRTC implementations)
- Cross-platform (Linux, Windows, macOS)
- Native support for async TCP/TLS, timers, signals
- io_context pattern aligns with high-performance server design
- Active development and strong community
- Compatible with C++17/20 features (std::optional, std::variant)

**Alternatives considered**:
- **libev/libuv**: Lower-level, more manual resource management; Boost.Asio provides better C++ abstractions
- **io_uring (Linux-only)**: Cutting-edge but Linux-specific; reduces portability
- **Raw epoll/kqueue**: Too low-level, reinventing the wheel

**Implementation approach**:
- Single `io_context` per thread (thread pool pattern for multi-core)
- Async operations with completion handlers (lambdas with move semantics)
- Strand for serializing agent connection access
- Deadline timers for timeouts and heartbeats

---

### 2. TLS Library Choice

**Decision**: Use **OpenSSL 1.1.1+** (or 3.x) with Boost.Asio integration

**Rationale**:
- Industry standard, well-audited
- Native support in Boost.Asio (`ssl::stream`)
- TLS 1.2 and 1.3 support
- SNI (Server Name Indication) support for wildcard certificates
- Certificate chain validation
- Available in all major Linux distributions

**Alternatives considered**:
- **BoringSSL**: Used by Google, but less community support and documentation
- **mbedTLS**: Lightweight but less feature-complete for enterprise scenarios
- **LibreSSL**: OpenBSD focus, less portable

**Implementation approach**:
- `ssl::context` with TLS 1.2 minimum version enforced
- Load certificates from memory (fetched from Azure Key Vault at startup)
- SNI callback to select certificate based on hostname
- Cipher suite hardening (disable weak ciphers)
- Certificate verification for mTLS (optional, future feature)

---

### 3. HTTP/2 Protocol Support

**Decision**: Use **nghttp2** library for HTTP/2 multiplexing

**Rationale**:
- Mature, widely-used C library (used by curl, H2O web server)
- Handles HTTP/2 framing, flow control, stream multiplexing
- Can be integrated with Boost.Asio for async operation
- Supports server push (not needed but available)
- Well-documented API

**Alternatives considered**:
- **Custom HTTP/2 implementation**: Too complex, reinventing wheel, high bug risk
- **HTTP/1.1 only**: Simpler but lacks multiplexing efficiency for multiple agent connections

**Implementation approach**:
- nghttp2 session per tunnel agent connection
- Async read/write integration with Boost.Asio
- Each HTTP request from client becomes an HTTP/2 stream to agent
- Stream priority for QoS (optional, future enhancement)

---

### 4. Azure SDK for C++ Integration

**Decision**: Use **Azure SDK for C++** for Key Vault and logging

**Rationale**:
- Official Microsoft SDK
- Native C++ API (no FFI or language interop)
- Supports Key Vault Secrets, Certificates, Log Analytics
- Uses Azure Core for authentication (managed identity or service principal)
- Active development, enterprise support

**Components to use**:
- `azure-security-keyvault-secrets`: Fetch tunnel tokens
- `azure-security-keyvault-certificates`: Load TLS certificates
- `azure-core`: HTTP client, authentication, logging
- `azure-identity`: Managed Identity authentication (for Container Apps)

**Implementation approach**:
- Load secrets at startup and cache in memory (1-hour TTL)
- Async refresh of secrets before expiry
- Fallback to cached tokens if Key Vault unreachable (transient failures)
- Structured logging to stdout (Container Apps captures to Log Analytics)

---

### 5. JSON Library for Configuration and Logging

**Decision**: Use **nlohmann/json** for JSON parsing and serialization

**Rationale**:
- Header-only library (easy integration)
- Modern C++ API (feels native, not C-style)
- Excellent error handling
- Fast enough for config/logging (not hot path)
- Wide adoption in C++ community

**Alternatives considered**:
- **rapidjson**: Faster but less ergonomic API
- **Boost.JSON**: Requires Boost dependency (we already have Boost.Asio, so acceptable, but nlohmann is simpler)

**Usage**:
- Parse environment variables (JSON format for complex config)
- Structured log output (JSON to stdout)
- Configuration file parsing (optional, env vars are primary)

---

### 6. Azure Table Storage for Tunnel Registry

**Decision**: Use **Azure Storage Blobs SDK** or **REST API** for tunnel metadata

**Rationale**:
- Azure Table Storage is semi-deprecated; Microsoft recommends Cosmos DB Table API or Blobs
- For MVP, **in-memory registry with periodic backup to Blob Storage** is simpler and faster
- Table Storage or Cosmos DB can be added later for HA scenarios

**Implementation approach (MVP)**:
- In-memory `std::unordered_map<tunnel_id, TunnelMetadata>`
- Serialize to JSON, upload to Blob Storage every 5 minutes
- Load from Blob on startup
- For HA: switch to Cosmos DB Table API or Redis (Azure Cache for Redis)

**Alternatives considered**:
- **Azure Table Storage**: Works but semi-deprecated, clunky API
- **Cosmos DB Table API**: More expensive ($25+/month), overkill for MVP
- **PostgreSQL/MySQL**: Requires separate database deployment, more ops overhead

---

### 7. Performance Benchmarking Tools

**Decision**: Use **Google Benchmark** for micro-benchmarks, **Apache Bench (ab)** and **wrk** for load testing

**Rationale**:
- Google Benchmark: Standard C++ benchmarking library, integrates with CMake/gtest
- Apache Bench: Simple, ubiquitous HTTP load tester
- wrk: More advanced, Lua scripting for complex scenarios
- iperf3: Standard for TCP throughput measurement

**Benchmarking strategy**:
- Micro-benchmarks: Token validation, routing lookup, JSON parsing
- Integration benchmarks: End-to-end HTTP request latency, TCP throughput
- Load tests: 100k req/s target with wrk, 50 concurrent tunnels

**CI/CD integration**:
- Run micro-benchmarks on every PR
- Compare against baseline (fail if regression >10%)
- Weekly load tests in Azure test environment

---

### 8. Memory Safety Tools

**Decision**: **AddressSanitizer (ASan)** for development, **Valgrind** for deep analysis

**Rationale**:
- ASan: Fast, catches most memory errors (buffer overflows, use-after-free, leaks), minimal performance overhead (~2x slowdown)
- Valgrind: Slower but more thorough, catches subtle issues
- Both integrate with CMake and CI/CD

**Implementation approach**:
- ASan enabled in debug builds (`-fsanitize=address`)
- Run all tests with ASan in CI/CD
- Weekly Valgrind runs for comprehensive analysis
- Zero tolerance policy: all memory errors must be fixed before merge

---

### 9. Static Analysis Integration

**Decision**: **clang-tidy** for linting, **cppcheck** for additional checks

**Rationale**:
- clang-tidy: Official LLVM tool, integrates with CMake, modern C++ checks
- cppcheck: Catches different issues (e.g., uninitialized variables)
- Both run in CI/CD, fail build on errors

**Configuration**:
- clang-tidy checks: modernize, performance, bugprone, security
- Enforce const-correctness, RAII patterns, no raw pointers (use smart pointers)
- Suppress false positives in `.clang-tidy` config

---

### 10. Build System and Dependency Management

**Decision**: **CMake 3.20+** with **vcpkg** for dependencies

**Rationale**:
- CMake: Industry standard, excellent IDE support (VS Code, CLion)
- vcpkg: Microsoft-backed, cross-platform, easy C++ package management
- Integrates with CMake via toolchain file
- Supports Azure SDK, Boost, OpenSSL, nghttp2, gtest, nlohmann/json

**Alternatives considered**:
- **Conan**: More complex, less Azure-friendly
- **Manual dependency building**: Error-prone, poor reproducibility

**CMake structure**:
- Top-level CMakeLists.txt with subdirectories (src/, tests/)
- Modern targets (`target_link_libraries`, `target_include_directories`)
- Separate executables for server and tests
- Install target for deployment

---

### 11. Docker Image Optimization

**Decision**: **Multi-stage build** with **Alpine Linux** base image

**Rationale**:
- Alpine: Small footprint (~5MB base), fast startup
- Multi-stage: Build in full dev image (Ubuntu + compilers), copy binary to Alpine runtime
- Final image size: ~50-100MB (vs. 500MB+ for full Ubuntu)

**Dockerfile structure**:
```dockerfile
# Stage 1: Builder
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y build-essential cmake git vcpkg
COPY . /src
RUN cd /src && cmake -B build -S . && cmake --build build --target protogate-server

# Stage 2: Runtime
FROM alpine:3.18
RUN apk add --no-cache libstdc++ libssl1.1 ca-certificates
COPY --from=builder /src/build/protogate-server /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/protogate-server"]
```

---

### 12. Azure Container Apps Configuration

**Decision**: **Environment variables** for configuration (12-factor app pattern)

**Configuration parameters**:
- `KEYVAULT_URI`: Azure Key Vault endpoint (e.g., `https://myvault.vault.azure.net`)
- `DNS_ZONE`: DNS zone for tunnels (e.g., `tunnel.mycorp.com`)
- `LOG_ANALYTICS_WORKSPACE_ID`: For structured logging
- `LOG_ANALYTICS_SHARED_KEY`: Authentication for log upload (or use managed identity)
- `TUNNEL_TOKEN_CACHE_TTL`: Token cache duration (default 3600 seconds)
- `MAX_CONCURRENT_TUNNELS`: Per-instance limit (default 50)
- `REQUEST_TIMEOUT_SECONDS`: Default timeout (default 1800 = 30 minutes)

**Container Apps features to use**:
- **Managed Identity**: Authenticate to Key Vault without secrets in config
- **Ingress**: HTTPS on port 443, custom domain support
- **Scaling**: CPU-based autoscaling (70% threshold, min 1, max 10 replicas)
- **Health probes**: HTTP GET /health every 30 seconds
- **Log streaming**: Stdout/stderr to Log Analytics

---

## Summary of Decisions

| **Area** | **Decision** | **Rationale** |
|----------|--------------|---------------|
| Async I/O | Boost.Asio | Mature, cross-platform, C++ native |
| TLS | OpenSSL 1.1.1+ | Industry standard, Boost.Asio integration |
| HTTP/2 | nghttp2 | Mature, handles multiplexing complexity |
| Azure SDK | Azure SDK for C++ | Official, native C++, Key Vault + logging |
| JSON | nlohmann/json | Header-only, modern C++ API |
| Storage | In-memory + Blob backup | Fast, simple for MVP; Cosmos DB for HA later |
| Benchmarking | Google Benchmark, wrk, iperf3 | Standard tools, CI/CD integration |
| Memory Safety | ASan + Valgrind | Catch all memory errors |
| Static Analysis | clang-tidy + cppcheck | Enforce modern C++ best practices |
| Build System | CMake + vcpkg | Industry standard, Azure-friendly |
| Docker | Alpine multi-stage build | Small image, fast startup |
| Configuration | Environment variables | 12-factor app, Container Apps native |

---

## Next Steps

All NEEDS CLARIFICATION items resolved. Proceed to **Phase 1: Design & Contracts** to define data models and API specifications.
