# ADR-002: TLS Library Choice

**Status**: Accepted  
**Date**: 2025-01-23  
**Deciders**: Architecture Team, Security Team  
**Context Owner**: Core Server Development

---

## Context

Protogate Core Server requires TLS 1.2+ encryption for all connections (client-to-server and agent-to-server). The TLS library must:

1. **Security**: Support TLS 1.2 and TLS 1.3 with strong cipher suites
2. **Performance**: Low-latency handshakes, high-throughput bulk encryption
3. **Compliance**: FIPS 140-2 compliance for government/enterprise customers (optional)
4. **Integration**: Works seamlessly with Boost.Asio (ADR-001)
5. **Maintenance**: Active security updates, vulnerability patching

**Problem Statement**: Which TLS library should we use for encryption?

---

## Decision

**We will use OpenSSL 3.0+ as the TLS library.**

---

## Options Considered

### Option 1: OpenSSL 3.0+ ✅ **SELECTED**

**Pros**:
- ✅ **Industry standard**: Used by 60%+ of internet servers (Apache, Nginx, HAProxy)
- ✅ **TLS 1.3 support**: Full support for latest TLS version
- ✅ **FIPS 140-2 certified**: Available FIPS module for compliance
- ✅ **Boost.Asio integration**: Native support via `boost::asio::ssl::context`
- ✅ **Comprehensive**: X.509 certificates, OCSP, session resumption, ALPN
- ✅ **Performance**: Hardware acceleration (AES-NI, AVX-512), session caching
- ✅ **Active maintenance**: Critical CVEs patched within days
- ✅ **Licensing**: Apache 2.0 (OpenSSL 3.0+), compatible with commercial use

**Cons**:
- ⚠️ **API complexity**: C API, requires careful memory management
- ⚠️ **Legacy baggage**: Supports old protocols (SSLv2, SSLv3) that must be disabled
- ⚠️ **Past vulnerabilities**: Heartbleed (2014), though modern versions are hardened

**Performance**:
- TLS 1.3 handshake: ~1 RTT (vs 2 RTT for TLS 1.2)
- AES-GCM throughput: 3-5 GB/s on modern CPUs (AES-NI)
- RSA signature: ~2000 ops/sec (2048-bit)

**Code Example**:
```cpp
boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tlsv12_server);
ssl_ctx.set_options(
    boost::asio::ssl::context::default_workarounds |
    boost::asio::ssl::context::no_sslv2 |
    boost::asio::ssl::context::no_sslv3 |
    boost::asio::ssl::context::no_tlsv1 |
    boost::asio::ssl::context::no_tlsv1_1);

ssl_ctx.use_certificate_chain_file("server.crt");
ssl_ctx.use_private_key_file("server.key", boost::asio::ssl::context::pem);
```

---

### Option 2: BoringSSL

**Pros**:
- ✅ **Performance optimized**: Google's fork of OpenSSL with aggressive optimizations
- ✅ **Modern API**: Cleaner C++ API, no legacy cruft
- ✅ **Security hardened**: Removes insecure algorithms by default
- ✅ **TLS 1.3 support**: Early adopter of TLS 1.3

**Cons**:
- ❌ **No API stability guarantee**: Google states "BoringSSL does not have a stable API"
- ❌ **Poor Boost.Asio integration**: Requires custom SSL context wrapper
- ❌ **No FIPS module**: Cannot be used in FIPS 140-2 environments
- ❌ **Limited documentation**: Internal Google documentation, sparse public docs
- ❌ **Build complexity**: Requires Bazel or CMake with custom scripts

**Verdict**: Rejected due to lack of API stability and poor Asio integration.

---

### Option 3: LibreSSL

**Pros**:
- ✅ **Security-focused**: OpenBSD project, conservative security approach
- ✅ **Cleaner codebase**: Removed legacy code, modern C standards
- ✅ **OpenSSL-compatible API**: Drop-in replacement for OpenSSL 1.0.1

**Cons**:
- ❌ **Slower TLS 1.3 adoption**: TLS 1.3 support added later than OpenSSL
- ❌ **No FIPS module**: Cannot be FIPS certified
- ❌ **Limited hardware acceleration**: Less optimized for AES-NI than OpenSSL
- ❌ **Smaller community**: Fewer contributors than OpenSSL

**Verdict**: Rejected due to lack of FIPS support and slower feature adoption.

---

### Option 4: wolfSSL

**Pros**:
- ✅ **Embedded-friendly**: Small footprint (~100KB)
- ✅ **FIPS 140-2 certified**: Available FIPS module
- ✅ **TLS 1.3 support**: Full support
- ✅ **Commercial support**: Available for enterprise

**Cons**:
- ❌ **License**: Dual-licensed (GPLv2 or commercial), commercial license required for proprietary use
- ❌ **Less adoption**: Smaller community than OpenSSL
- ❌ **Boost.Asio integration**: Requires custom wrapper, not officially supported
- ❌ **Performance**: Optimized for embedded, not necessarily for high-throughput servers

**Verdict**: Rejected due to licensing concerns and poor Asio integration.

---

### Option 5: mbedTLS (formerly PolarSSL)

**Pros**:
- ✅ **Lightweight**: ~200KB footprint
- ✅ **Readable code**: High-quality C code, easier to audit
- ✅ **Apache 2.0 license**: Open-source friendly
- ✅ **TLS 1.3 support**: In development (as of 2024)

**Cons**:
- ❌ **No Boost.Asio integration**: Requires extensive custom code
- ❌ **Performance**: Not optimized for high-throughput (designed for IoT)
- ❌ **Limited hardware acceleration**: Less mature than OpenSSL
- ❌ **TLS 1.3 incomplete**: Not all features implemented

**Verdict**: Rejected due to lack of Asio integration and performance concerns.

---

## Rationale

**OpenSSL 3.0 was selected because**:

1. **Battle-tested**: Powers majority of internet infrastructure (Apache, Nginx, HAProxy, AWS ELB)

2. **Boost.Asio native support**: `boost::asio::ssl::stream` is designed for OpenSSL, zero integration effort

3. **FIPS compliance**: FIPS 140-2 module available for government/enterprise customers (optional)

4. **Performance**: Hardware acceleration (AES-NI, AVX-512), optimized for high-throughput servers

5. **Security**: Active CVE monitoring, rapid patching (typically <7 days for critical issues)

6. **Ecosystem**: Works with Azure Key Vault (X.509 certificates), certbot (Let's Encrypt), common tooling

**Trade-offs accepted**:
- API complexity (mitigated by Boost.Asio wrapper)
- Larger binary size vs embedded libraries (acceptable for server deployment)
- Legacy protocol support (mitigated by explicit disable of SSLv2/v3, TLS 1.0/1.1)

---

## Consequences

### Positive

- ✅ **Zero integration effort**: Boost.Asio SSL support is OpenSSL-first
- ✅ **Familiar to team**: Most C++ developers have OpenSSL experience
- ✅ **Tooling support**: openssl CLI, certbot, Azure Key Vault integration
- ✅ **Performance**: Hardware-accelerated AES-GCM, optimized for x86_64
- ✅ **Future-proof**: TLS 1.3, post-quantum crypto (experimental)

### Negative

- ⚠️ **CVE exposure**: Must monitor OpenSSL CVEs and patch promptly
- ⚠️ **Binary size**: OpenSSL adds ~2-3MB to binary (acceptable)
- ⚠️ **Build dependency**: Must ensure OpenSSL 3.0+ available (vcpkg handles this)

### Neutral

- 🔄 **FIPS mode**: Optional, can be enabled with FIPS-certified OpenSSL build
- 🔄 **Memory safety**: C API requires careful RAII wrappers (already done by Asio)

---

## Implementation Notes

**vcpkg dependency**:
```json
{
  "name": "protogate",
  "dependencies": [
    "openssl[core,tools]"
  ]
}
```

**CMake integration**:
```cmake
find_package(OpenSSL REQUIRED)
target_link_libraries(protogate PRIVATE OpenSSL::SSL OpenSSL::Crypto)
```

**Security hardening**:
```cpp
// Disable weak protocols
SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

// Use only strong cipher suites
SSL_CTX_set_cipher_list(ctx, 
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-CHACHA20-POLY1305");

// Disable compression (CRIME attack mitigation)
SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);

// Enable OCSP stapling
SSL_CTX_set_tlsext_status_type(ctx, TLSEXT_STATUSTYPE_ocsp);
```

**Certificate management**:
- Load certificates from Azure Key Vault at startup
- Automatic renewal via Let's Encrypt (certbot + Key Vault integration)
- Certificate pinning for agent connections (optional)

---

## Monitoring & Maintenance

**CVE tracking**:
- Subscribe to openssl-announce mailing list
- Automated Dependabot alerts in GitHub
- Monthly review of OpenSSL security advisories

**Update policy**:
- Critical CVEs: Patch within 48 hours
- High CVEs: Patch within 7 days
- Medium/Low CVEs: Patch in next monthly release

**Testing**:
- SSL Labs test (A+ rating required): https://www.ssllabs.com/ssltest/
- Qualys SSL Server Test for TLS configuration
- Unit tests for TLS handshake, cipher negotiation

---

## Alternatives for Future Consideration

If OpenSSL proves insufficient (unlikely), consider:

1. **OpenSSL 3.1+ with QUIC**: For HTTP/3 support in future
   - Native QUIC support in OpenSSL 3.2+
   - Enables HTTP/3 tunneling for lower latency

2. **BoringSSL + custom Asio integration**: For maximum performance
   - Only if API stability improves
   - Requires significant engineering effort (custom SSL context)

3. **AWS-LC (AWS LibCrypto)**: AWS-maintained OpenSSL fork
   - AWS-optimized cryptography
   - Compatible with OpenSSL API (easier migration than BoringSSL)

---

## References

- [OpenSSL 3.0 Documentation](https://www.openssl.org/docs/man3.0/)
- [Boost.Asio SSL Documentation](https://www.boost.org/doc/libs/1_82_0/doc/html/boost_asio/overview/ssl.html)
- [TLS 1.3 RFC 8446](https://datatracker.ietf.org/doc/html/rfc8446)
- [OpenSSL FIPS 140-2 Module](https://www.openssl.org/docs/fips.html)
- [Mozilla TLS Configuration Generator](https://ssl-config.mozilla.org/)
- [SSL Labs Best Practices](https://github.com/ssllabs/research/wiki/SSL-and-TLS-Deployment-Best-Practices)

---

**Status**: Accepted and implemented  
**Last Reviewed**: 2025-01-23  
**Next Review**: After security audit or major OpenSSL version update
