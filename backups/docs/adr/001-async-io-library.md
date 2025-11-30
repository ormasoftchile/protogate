# ADR-001: Async I/O Library Choice

**Status**: Accepted  
**Date**: 2025-01-23  
**Deciders**: Architecture Team  
**Context Owner**: Core Server Development

---

## Context

Protogate Core Server requires high-performance asynchronous I/O to handle thousands of concurrent connections efficiently. The server must:

1. **Handle concurrent connections**: 50+ tunnel agents, 10,000+ HTTP client connections
2. **Minimize latency**: <10ms p95 overhead for HTTP tunneling
3. **Maximize throughput**: 100+ Mbps for TCP forwarding
4. **Efficient resource usage**: <512MB memory for 50 tunnels
5. **Cross-platform support**: Linux (primary), with potential for Windows/macOS

**Problem Statement**: Which async I/O library should we use for the C++ server implementation?

---

## Decision

**We will use Boost.Asio as the async I/O library.**

---

## Options Considered

### Option 1: Boost.Asio ✅ **SELECTED**

**Pros**:
- ✅ **Mature & battle-tested**: Used in production by millions of applications (Beast, Proxygen, etc.)
- ✅ **Comprehensive API**: Timers, SSL/TLS, strand-based synchronization, coroutines (C++20)
- ✅ **Cross-platform**: Windows (IOCP), Linux (epoll), macOS (kqueue), BSD
- ✅ **Zero-copy operations**: scatter-gather I/O, native buffer sequences
- ✅ **C++ idioms**: RAII, type-safe, header-only option for easy integration
- ✅ **TLS integration**: Native SSL support with OpenSSL backend
- ✅ **Active development**: Regular updates, C++20 coroutine support
- ✅ **Documentation**: Excellent documentation and examples

**Cons**:
- ⚠️ **Large dependency**: Boost is a large library (though Asio can be standalone)
- ⚠️ **Compilation time**: Template-heavy code increases build times
- ⚠️ **Learning curve**: Async programming model requires understanding

**Performance**:
- Epoll-based on Linux (O(1) scalability)
- Zero-copy with native buffers
- Efficient strand-based concurrency

**Code Example**:
```cpp
boost::asio::io_context io_context;
boost::asio::ip::tcp::acceptor acceptor(io_context, 
    boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 443));

acceptor.async_accept([](boost::system::error_code ec, 
                          boost::asio::ip::tcp::socket socket) {
    if (!ec) {
        // Handle connection
    }
});

io_context.run();
```

---

### Option 2: libev

**Pros**:
- ✅ Lightweight (~10k LOC)
- ✅ Very fast event loop
- ✅ C API (simpler to understand)
- ✅ Battle-tested (used in Node.js v0.x)

**Cons**:
- ❌ **C API only**: Requires manual memory management, error-prone
- ❌ **No TLS support**: Must integrate OpenSSL separately
- ❌ **No C++ idioms**: No RAII, type safety
- ❌ **Less cross-platform**: Primarily Unix-focused
- ❌ **Maintenance concerns**: Less active development than Boost.Asio

**Verdict**: Rejected due to lack of C++ support and TLS integration.

---

### Option 3: libuv

**Pros**:
- ✅ Cross-platform (Windows, Linux, macOS)
- ✅ Battle-tested (Node.js, Julia, Rust async-std backend)
- ✅ Good performance
- ✅ Active development

**Cons**:
- ❌ **C API only**: Same issues as libev
- ❌ **No TLS support**: Requires separate OpenSSL integration
- ❌ **Thread pool required**: For file I/O, DNS resolution (adds complexity)
- ❌ **Less C++ friendly**: No native C++ wrapper, manual lifetime management

**Verdict**: Rejected due to C API and lack of C++ idioms.

---

### Option 4: POCO C++ Libraries

**Pros**:
- ✅ Full C++ framework (networking, HTTP, JSON, etc.)
- ✅ TLS support built-in
- ✅ Reactor and Proactor patterns
- ✅ Good documentation

**Cons**:
- ❌ **Less performant**: Not as optimized as Boost.Asio for high-throughput scenarios
- ❌ **Monolithic**: Large framework with many unused components
- ❌ **Less adoption**: Smaller community than Boost
- ❌ **Async API less mature**: Reactor pattern, not full async/await

**Verdict**: Rejected due to performance concerns and smaller community.

---

### Option 5: Custom epoll/IOCP wrapper

**Pros**:
- ✅ Minimal dependencies
- ✅ Full control over implementation
- ✅ Potentially faster for specific use cases

**Cons**:
- ❌ **High development cost**: Months of work to reach Boost.Asio feature parity
- ❌ **Bug-prone**: Event loop bugs are notoriously difficult to debug
- ❌ **Cross-platform complexity**: epoll (Linux), kqueue (macOS/BSD), IOCP (Windows)
- ❌ **No TLS integration**: Must build OpenSSL wrapper
- ❌ **Maintenance burden**: All bugs/features become team responsibility

**Verdict**: Rejected due to excessive development and maintenance cost.

---

## Rationale

**Boost.Asio was selected because**:

1. **Production-ready**: Proven in high-performance production systems (Facebook Proxygen, Beast HTTP library, gRPC C++ impl uses similar patterns)

2. **C++ native**: RAII, type safety, move semantics, C++20 coroutines support aligns with modern C++ best practices

3. **Comprehensive**: Timers, strand-based synchronization, SSL/TLS, scatter-gather I/O, buffer management—all required features built-in

4. **Performance**: Zero-copy operations, efficient epoll backend on Linux, optimized for high concurrency

5. **Ecosystem**: Boost.Beast (HTTP/WebSocket) built on Asio, nghttp2 can integrate with Asio buffers

6. **Future-proof**: Active development, C++20 coroutine support enables async/await patterns

**Trade-offs accepted**:
- Larger binary size (acceptable for containerized deployment)
- Longer compile times (mitigated with ccache and distributed builds)
- Boost dependency (acceptable, widely available via vcpkg/Conan)

---

## Consequences

### Positive

- ✅ Faster development: Rich API reduces boilerplate code
- ✅ Fewer bugs: Battle-tested library with extensive testing
- ✅ Easier hiring: Boost.Asio is widely known in C++ community
- ✅ Maintainability: Less custom networking code to maintain
- ✅ Extensibility: Easy to add new protocols (WebSocket, HTTP/3) with Beast

### Negative

- ⚠️ Dependency on Boost: Must keep Boost version up-to-date
- ⚠️ Compile times: Template-heavy code increases build duration
- ⚠️ Binary size: Boost adds ~5-10MB to binary size (acceptable for container deployment)

### Neutral

- 🔄 Learning curve: Team must learn async programming patterns (required for any async I/O)
- 🔄 C++17/20 requirement: Boost.Asio works best with modern C++ (already our target)

---

## Implementation Notes

**vcpkg dependency**:
```json
{
  "name": "protogate",
  "dependencies": [
    "boost-asio",
    "boost-beast",
    "openssl"
  ]
}
```

**CMake integration**:
```cmake
find_package(Boost REQUIRED COMPONENTS system)
target_link_libraries(protogate PRIVATE Boost::system Boost::asio)
```

**Performance tuning**:
- Use strand-based synchronization to avoid mutex contention
- Preallocate buffer pools for zero-allocation hot paths
- Consider C++20 coroutines for cleaner async code (future enhancement)

---

## Alternatives for Future Consideration

If Boost.Asio proves insufficient (unlikely), consider:

1. **io_uring (Linux 5.1+)**: Kernel-space zero-copy I/O, 2-3x faster than epoll for some workloads
   - Requires Linux 5.1+ kernel (Azure Container Apps supports this)
   - Can be integrated via liburing + custom Asio executor

2. **C++20 Coroutines + custom executor**: Full async/await with Asio as backend
   - Cleaner code than callback-based approach
   - Already supported by Boost.Asio 1.77+

---

## References

- [Boost.Asio Documentation](https://www.boost.org/doc/libs/1_82_0/doc/html/boost_asio.html)
- [Boost.Beast HTTP library](https://github.com/boostorg/beast)
- [C++ Networking TS](https://cplusplus.github.io/networking-ts/draft.pdf) (Asio-based)
- [Comparison of async I/O libraries](https://think-async.com/Asio/asio-1.18.2/doc/asio/overview/core/async.html)
- [Facebook Proxygen uses Boost.Asio patterns](https://github.com/facebook/proxygen)

---

**Status**: Accepted and implemented  
**Last Reviewed**: 2025-01-23  
**Next Review**: After T109 (performance benchmarks)
