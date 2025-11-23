# Security: Protogate Core Server

**Version**: 1.0  
**Last Updated**: 2025-01-23  
**Classification**: Public  
**Owner**: Security Team

## Table of Contents

1. [Overview](#overview)
2. [Threat Model](#threat-model)
3. [Security Controls](#security-controls)
4. [Authentication & Authorization](#authentication--authorization)
5. [Encryption](#encryption)
6. [Audit & Compliance](#audit--compliance)
7. [Incident Response](#incident-response)

---

## Overview

Protogate Core Server is a security-critical component that bridges untrusted internet traffic with customer internal services. This document outlines the threat model, security controls, and incident response procedures.

**Security Principles**:
1. **Defense in Depth**: Multiple layers of security (TLS, tokens, IP allowlists, rate limiting)
2. **Least Privilege**: Minimal permissions for managed identity and network access
3. **Zero Trust**: All connections validated, no implicit trust
4. **Audit Everything**: Comprehensive logging of security events
5. **Fail Secure**: Reject on error, no fallback to insecure modes

---

## Threat Model

### STRIDE Analysis

#### 1. Spoofing

**Threat**: Attacker impersonates legitimate tunnel agent

**Attack Vectors**:
- Stolen token credentials
- DNS cache poisoning (subdomain hijacking)
- TLS certificate compromise

**Mitigations**:
- ✅ **Token Authentication**: 256-bit tokens with SHA-256 hashing
- ✅ **TLS Client Certificates** (optional): mTLS for agent authentication
- ✅ **Token Rotation**: Periodic token refresh (manual, to be automated)
- ✅ **DNSSEC** (recommended): Customer-configured for DNS zone
- ⚠️ **Residual Risk**: Stolen tokens remain valid until rotation (manual process)

**Risk Level**: **MEDIUM** (with controls), **HIGH** (without token rotation)

---

#### 2. Tampering

**Threat**: Attacker modifies traffic in transit

**Attack Vectors**:
- Man-in-the-Middle (MITM) attacks
- HTTP request/response manipulation
- DNS response tampering

**Mitigations**:
- ✅ **TLS 1.2+ Mandatory**: All traffic encrypted, no plaintext allowed
- ✅ **Certificate Validation**: Strict certificate pinning (agents validate server cert)
- ✅ **HSTS Headers**: HTTP Strict Transport Security enforced
- ✅ **TLS Cipher Suites**: Only strong ciphers (AES-GCM, ChaCha20-Poly1305)
- ⚠️ **Residual Risk**: Compromised CA or certificate key

**Risk Level**: **LOW** (with TLS 1.2+)

---

#### 3. Repudiation

**Threat**: User denies performing an action

**Attack Vectors**:
- Unauthorized access to tunnels
- Data exfiltration without audit trail

**Mitigations**:
- ✅ **Audit Logging**: All authentication attempts, tunnel creation/deletion logged
- ✅ **Immutable Logs**: Azure Log Analytics with 30-day retention
- ✅ **Timestamp Verification**: All log entries have UTC timestamps
- ✅ **IP Address Logging**: Source IP recorded for all connections
- ⚠️ **Residual Risk**: Logs not tamper-proof (no blockchain/ledger)

**Risk Level**: **LOW** (audit logs sufficient for non-financial transactions)

---

#### 4. Information Disclosure

**Threat**: Sensitive data leaked to unauthorized parties

**Attack Vectors**:
- Token logging in plaintext
- Payload logging (customer data)
- DNS enumeration (discovering tunnel subdomains)
- Error messages revealing internal details

**Mitigations**:
- ✅ **No Token Logging**: Tokens redacted from all logs (FR-020)
- ✅ **No Payload Logging**: Customer traffic never logged (FR-020)
- ✅ **Generic Error Messages**: No stack traces or internal details in responses
- ✅ **Rate Limiting**: Prevent brute-force token guessing
- ⚠️ **Residual Risk**: DNS zone enumeration (subdomain discovery)

**Risk Level**: **MEDIUM** (DNS enumeration possible, token brute-force mitigated)

---

#### 5. Denial of Service (DoS)

**Threat**: Attacker overwhelms server with requests

**Attack Vectors**:
- HTTP flood attacks
- TCP SYN flood
- Slowloris (slow HTTP headers)
- Agent connection exhaustion (50 concurrent limit)

**Mitigations**:
- ✅ **Connection Limits**: Max 50 agent connections, 10,000 HTTP connections
- ✅ **Rate Limiting**: 500 req/sec per tunnel, 1000 req/sec global
- ✅ **Timeout Enforcement**: 30-second idle timeout for HTTP, 60s for agent connections
- ✅ **Azure DDoS Protection** (optional): Customer-configured DDoS Standard
- ⚠️ **Residual Risk**: Application-layer DoS (valid tokens with excessive requests)

**Risk Level**: **MEDIUM** (rate limiting helps, but application-layer attacks possible)

---

#### 6. Elevation of Privilege

**Threat**: Attacker gains unauthorized access to admin functions

**Attack Vectors**:
- Management API bypass (no Entra ID token validation)
- Key Vault access escalation (managed identity compromise)
- Container escape (breaking out of Docker container)

**Mitigations**:
- ✅ **OAuth 2.0**: Management API requires Entra ID bearer tokens
- ✅ **RBAC**: Role-Based Access Control for Key Vault secrets
- ✅ **Managed Identity**: No credentials in code, Azure-managed rotation
- ✅ **Container Isolation**: Azure Container Apps enforces strict isolation
- ⚠️ **Residual Risk**: Container escape vulnerabilities (kernel exploits)

**Risk Level**: **LOW** (Azure platform security + RBAC)

---

### Trust Boundaries

```
┌─────────────────────────────────────────────────────────────────┐
│ Untrusted Zone: Internet                                        │
│ - Web browsers, API clients, TCP clients                        │
└───────────┬─────────────────────────────────────────────────────┘
            │ TLS 1.2+ (Public Internet)
            ▼
┌─────────────────────────────────────────────────────────────────┐
│ Perimeter: Protogate Core Server                                │
│ - TLS termination                                               │
│ - Token validation                                              │
│ - Rate limiting                                                 │
└───────────┬─────────────────────────────────────────────────────┘
            │ TLS 1.2+ + Token Auth (Authenticated)
            ▼
┌─────────────────────────────────────────────────────────────────┐
│ Trusted Zone: Tunnel Agents                                     │
│ - Inside customer network                                       │
│ - Access to local services                                      │
└───────────┬─────────────────────────────────────────────────────┘
            │ HTTP/TCP (Customer Network)
            ▼
┌─────────────────────────────────────────────────────────────────┐
│ Highly Trusted Zone: Local Services                             │
│ - Printers, internal APIs, databases                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Security Controls

### 1. Authentication

**Tunnel Agents**:
- **Method**: Bearer tokens (256-bit, cryptographically secure)
- **Storage**: Azure Key Vault (secrets)
- **Validation**: SHA-256 hash comparison (constant-time)
- **Rotation**: Manual (via `generate-token.sh`), automated rotation planned
- **Format**: `Authorization: Bearer <token>`

**Management API**:
- **Method**: OAuth 2.0 (Entra ID bearer tokens)
- **Scopes**: `Tunnel.Read`, `Tunnel.Write`, `Tunnel.Delete`
- **RBAC**: Role assignments in Azure (Contributor, Reader roles)

**TLS Client Certificates** (optional):
- **mTLS**: Mutual TLS for agent authentication
- **Certificate Authority**: Customer-managed CA or Azure Key Vault certificates
- **Validation**: Certificate fingerprint or CN matching

### 2. Authorization

**Tunnel Operations**:
| Operation | Required Role | Notes |
|-----------|---------------|-------|
| Create Tunnel | `Tunnel.Write` | Requires Entra ID token |
| List Tunnels | `Tunnel.Read` | Read-only access |
| Delete Tunnel | `Tunnel.Delete` | Admin-only |
| Connect Agent | Valid token | No role required |

**Key Vault Access**:
- Managed Identity has `Key Vault Secrets User` role
- Only `Get` and `List` permissions (no `Set` or `Delete`)

### 3. Encryption

**TLS Configuration**:
```cpp
// OpenSSL configuration
SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION); // TLS 1.2 minimum
SSL_CTX_set_cipher_list(ctx, 
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-CHACHA20-POLY1305");
SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
```

**Disallowed Cipher Suites**:
- ❌ RC4, DES, 3DES (weak algorithms)
- ❌ NULL ciphers (no encryption)
- ❌ Export ciphers (intentionally weakened)
- ❌ Anonymous Diffie-Hellman (no authentication)

**Key Storage**:
- TLS certificates: Azure Key Vault (PEM format)
- Tunnel tokens: Azure Key Vault (secrets, SHA-256 hashes stored)

### 4. Network Security

**IP Allowlisting** (optional):
```yaml
# Example: Only allow agents from customer IP ranges
allowedCIDRs:
  - 10.0.0.0/8       # Private network
  - 203.0.113.0/24   # Customer public IP range
```

**Port Isolation**:
- HTTP/HTTPS: Port 443 (public internet)
- Agent connections: Port 8443 (restricted, requires valid token)
- Management API: Port 443 (requires Entra ID token)

**Rate Limiting**:
| Endpoint | Limit | Window | Action |
|----------|-------|--------|--------|
| `/api/v1/tunnels` | 100 req/min | Per IP | 429 Too Many Requests |
| Agent auth | 10 attempts/min | Per IP | 401 Unauthorized + delay |
| HTTP tunneling | 500 req/sec | Per tunnel | 503 Service Unavailable |

### 5. Input Validation

**Tunnel Creation**:
- Tunnel ID: Alphanumeric + hyphens, 3-63 chars, lowercase
- Subdomain: DNS-compliant (no leading/trailing hyphens, no special chars)
- IP allowlist: Valid CIDR notation (e.g., 10.0.0.0/8)

**HTTP Headers**:
- Host header: Max 253 chars, valid DNS name
- Authorization header: Bearer token format, max 256 chars
- Custom headers: Max 8KB total size

**Rejection Rules**:
- SQL injection patterns (', --, ;)
- Path traversal (../, ..\)
- XSS payloads (<script>, onerror=)

### 6. Audit Logging

**Security Events Logged**:
```json
{
  "timestamp": "2025-01-23T10:30:00.000Z",
  "eventType": "AUTH_FAILURE",
  "severity": "WARN",
  "sourceIP": "203.0.113.42",
  "tunnelId": "my-tunnel",
  "tokenHash": "sha256:abc123...",
  "reason": "Invalid token",
  "action": "Rejected"
}
```

**Event Types**:
- `AUTH_SUCCESS`: Agent authenticated successfully
- `AUTH_FAILURE`: Invalid token or unauthorized IP
- `TUNNEL_CREATED`: New tunnel registered
- `TUNNEL_DELETED`: Tunnel removed
- `RATE_LIMIT_EXCEEDED`: Too many requests from IP/tunnel
- `TLS_HANDSHAKE_FAILED`: Certificate validation error

**Retention**:
- Azure Log Analytics: 30 days (configurable up to 730 days)
- Immutable: Logs cannot be deleted by managed identity

---

## Authentication & Authorization

### Token Lifecycle

1. **Generation**:
   ```bash
   ./deploy/scripts/generate-token.sh <key-vault-name> <tunnel-id>
   # Generates 256-bit token, stores in Key Vault
   ```

2. **Distribution**:
   - Securely shared with customer (encrypted email, secret manager, etc.)
   - Customer configures agent with token

3. **Validation**:
   ```cpp
   // Server-side validation (simplified)
   std::string token_hash = sha256(token);
   std::string stored_hash = key_vault_client.get_secret("tunnel-token-" + tunnel_id);
   if (constant_time_compare(token_hash, stored_hash)) {
       // Authenticated
   }
   ```

4. **Rotation**:
   - Manual: Delete old secret, generate new token
   - Automated (planned): Scheduled rotation with overlap period

### Multi-Factor Authentication (MFA)

**Management API**:
- Entra ID enforces MFA for admin operations
- Conditional Access policies (customer-configured)

**Agent Authentication**:
- Single-factor: Bearer token only
- Multi-factor (optional): Token + TLS client certificate (mTLS)

---

## Encryption

### Data at Rest

| Data Type | Encryption | Key Management |
|-----------|------------|----------------|
| Tunnel tokens | AES-256 (Key Vault) | Azure-managed keys |
| TLS certificates | AES-256 (Key Vault) | Azure-managed keys |
| Logs | AES-256 (Log Analytics) | Azure-managed keys |
| Container images | AES-256 (ACR) | Azure-managed keys |

**Customer-Managed Keys** (optional):
- BYOK: Bring Your Own Key (Key Vault Customer-Managed Keys)
- HSM-backed keys for PCI DSS compliance

### Data in Transit

| Connection | Encryption | Protocol |
|------------|------------|----------|
| Client → Server | TLS 1.2+ | HTTPS, TLS over TCP |
| Agent → Server | TLS 1.2+ | WebSocket over TLS |
| Server → Key Vault | TLS 1.2+ | HTTPS (Azure SDK) |
| Server → Log Analytics | TLS 1.2+ | HTTPS (Data Collector API) |

**Internal Traffic**:
- Within Container Apps Environment: Encrypted by Azure platform
- Between replicas: Not applicable (stateless design)

---

## Audit & Compliance

### Compliance Frameworks

**SOC 2 Type II**:
- ✅ Access controls (RBAC, token auth)
- ✅ Encryption at rest and in transit
- ✅ Audit logging (Azure Log Analytics)
- ✅ Incident response procedures (this document)

**GDPR**:
- ✅ Data minimization (no customer PII logged)
- ✅ Right to erasure (delete tunnel and logs)
- ✅ Data processing agreement (customer controls data in their Azure subscription)

**PCI DSS** (if handling payment data):
- ⚠️ **Not Certified**: Protogate does not log or store payment card data
- Customer responsibility: Ensure agents do not proxy payment card data through tunnels

### Audit Reports

**Weekly**:
- Failed authentication attempts by IP
- Rate limit violations
- High error rate alerts

**Monthly**:
- Token rotation status (manual check)
- Security patch compliance (container image updates)
- Log retention verification

**Quarterly**:
- Penetration testing (external security firm)
- Vulnerability scanning (container images, dependencies)
- Access review (Entra ID role assignments)

---

## Incident Response

### Severity Levels

| Level | Definition | Response Time | Examples |
|-------|------------|---------------|----------|
| **P0** | Critical security incident | 15 minutes | Active breach, token leak |
| **P1** | High-severity security issue | 1 hour | DDoS attack, certificate expiry |
| **P2** | Medium-severity issue | 4 hours | Vulnerability disclosure, rate limit exceeded |
| **P3** | Low-severity issue | 1 business day | False positive alert, minor config error |

### Incident Response Playbook

#### 1. Token Compromise (P0)

**Indicators**:
- Multiple failed auth attempts from unexpected IPs
- Successful auth from unauthorized geolocations
- Customer reports unauthorized tunnel access

**Response**:
1. **Immediate** (0-15 min):
   - Revoke compromised token in Key Vault
   - Block source IP in firewall rules
   - Notify customer via secure channel

2. **Short-term** (15-60 min):
   - Generate new token for customer
   - Review audit logs for unauthorized access
   - Check for data exfiltration

3. **Long-term** (1-24 hours):
   - Root cause analysis (how token was compromised)
   - Implement additional controls (IP allowlist, mTLS)
   - Post-mortem document

#### 2. DDoS Attack (P1)

**Indicators**:
- CPU usage >90% sustained for >5 minutes
- HTTP 503 errors increasing
- Agent connection failures

**Response**:
1. **Immediate** (0-15 min):
   - Enable Azure DDoS Protection Standard (if not already enabled)
   - Increase rate limits temporarily (if legitimate traffic)
   - Scale out Container Apps replicas (1 → 10)

2. **Short-term** (15-60 min):
   - Analyze attack patterns (source IPs, request types)
   - Block attack IPs in Azure Firewall
   - Contact Azure Support for mitigation assistance

3. **Long-term** (1-24 hours):
   - Review rate limiting rules
   - Implement CAPTCHA for suspicious traffic
   - Update incident response procedures

#### 3. Vulnerability Disclosure (P2)

**Indicators**:
- Security researcher reports vulnerability (CVE, GitHub issue)
- Automated scanning detects outdated dependency

**Response**:
1. **Immediate** (0-4 hours):
   - Acknowledge receipt (if external disclosure)
   - Reproduce vulnerability in isolated environment
   - Assess severity and exploitability

2. **Short-term** (4-24 hours):
   - Patch vulnerability (code fix or dependency update)
   - Deploy hotfix to production (emergency deployment)
   - Notify customers if action required

3. **Long-term** (1-7 days):
   - Publish security advisory
   - Update security documentation
   - Implement automated vulnerability scanning in CI/CD

---

## Security Contacts

**Security Team**:
- Email: security@example.com
- PagerDuty: #security-oncall
- Slack: #security-incidents

**Vulnerability Disclosure**:
- Email: security@example.com
- Bug Bounty: https://example.com/security/bug-bounty
- GPG Key: https://example.com/security/pgp-key.asc

**Azure Support**:
- Security incidents: https://portal.azure.com → Support → New Support Request
- DDoS mitigation: Contact Azure Support (Priority: Critical)

---

## Appendix: Security Checklist

**Pre-Deployment**:
- [ ] TLS certificates valid and not expiring within 30 days
- [ ] Tunnel tokens rotated within last 90 days
- [ ] Rate limiting configured and tested
- [ ] IP allowlists reviewed and up-to-date
- [ ] Managed identity has least-privilege permissions
- [ ] Azure DDoS Protection Standard enabled (prod only)
- [ ] Log Analytics alerts configured (high error rate, auth failures)
- [ ] Incident response team trained on playbooks

**Post-Deployment**:
- [ ] Health check endpoint returns 200 OK
- [ ] Authentication tested with valid and invalid tokens
- [ ] Rate limiting tested (verify 429 responses)
- [ ] Audit logs visible in Log Analytics
- [ ] Security scan passed (no critical vulnerabilities)
- [ ] Customer notified of deployment and new token (if rotated)

---

**Document Version**: 1.0  
**Next Review**: After T108 (integration testing) or security incident
