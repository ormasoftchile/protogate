<!--
Sync Impact Report:
- Version: Initial → 1.0.0
- New constitution created for Protogate project
- Added principles: Security-First, Azure-Native, Self-Hosting Control, Performance & Cost Efficiency, Observability & Auditability
- Added sections: Security Requirements, Development Standards
- Templates status:
  ✅ plan-template.md - Constitution Check section compatible
  ✅ spec-template.md - User story format compatible with principles
  ✅ tasks-template.md - Task categorization aligns with principles
- Follow-up: None
-->

# Protogate Constitution

## Core Principles

### I. Security-First (NON-NEGOTIABLE)

Security MUST be the foundational design constraint for all components. Every feature, protocol, and deployment 
configuration MUST default to secure settings. Insecure options are prohibited unless explicitly justified 
by enterprise requirements and documented with mitigation strategies.

**Non-negotiable requirements:**
- TLS 1.2+ mandatory for all tunnel connections (outbound and inbound)
- mTLS support MUST be implemented for high-security scenarios
- Per-tunnel authentication tokens MUST be cryptographically secure (minimum 256-bit entropy)
- All secrets (tokens, certificates, keys) MUST be stored in Azure Key Vault
- IP allowlists and Azure Front Door WAF integration MUST be supported
- No plaintext credentials in logs, configuration files, or environment variables
- Security patches MUST be prioritized over feature development

**Rationale**: Protogate handles sensitive enterprise traffic and replaces VPN infrastructure. A single security 
vulnerability could expose customer networks. Security cannot be retrofitted—it must be foundational.

### II. Azure-Native Architecture

All deployment patterns, infrastructure components, and operational practices MUST prioritize Azure-native 
services. The system MUST leverage Azure's managed services for reliability, scaling, and enterprise integration.

**Requirements:**
- Primary deployment targets: Azure Container Apps, AKS, or Azure VMs
- DNS management via Azure DNS Zones (e.g., `*.tunnel.mycorp.com`)
- Certificate and secret management via Azure Key Vault
- Observability via Azure Monitor and Log Analytics
- Identity integration with Microsoft Entra ID where applicable
- Infrastructure-as-Code using Azure Bicep or Terraform with Azure providers
- Cost optimization: leverage Azure Reserved Instances, spot instances where appropriate

**Rationale**: Customers choose Protogate specifically for Azure integration. Multi-cloud abstractions would 
dilute the value proposition and increase complexity without proportional benefit.

### III. Self-Hosting Control

Users MUST maintain full ownership and operational control of their Protogate infrastructure. The architecture 
MUST NOT introduce external dependencies that could create vendor lock-in, data exfiltration risks, or 
compliance issues.

**Requirements:**
- All components deploy within customer Azure subscriptions
- No telemetry or data transmitted to external SaaS providers (including the Protogate vendor)
- Configuration, logs, and tunnel data remain in customer-controlled storage
- Open-source components where feasible to enable auditability
- Clear documentation for offline/air-gapped deployment scenarios
- Support for customer-managed encryption keys (BYOK)

**Rationale**: Enterprise customers require sovereignty over security-critical infrastructure. External dependencies 
contradict the core value proposition versus ngrok and similar SaaS offerings.

### IV. Performance & Cost Efficiency

The system MUST deliver enterprise-grade performance while maintaining deployment costs under $15/month for 
typical small-to-medium usage patterns. Performance MUST NOT degrade below acceptable thresholds as tunnel 
count or traffic volume increases.

**Requirements:**
- Tunnel establishment latency: <500ms p95
- HTTP tunnel overhead: <10ms p95 added latency
- TCP tunnel throughput: minimum 100 Mbps per tunnel
- Memory footprint: <512MB per tunnel server instance for 50 concurrent tunnels
- Support horizontal scaling (add instances to increase capacity)
- Efficient connection pooling and multiplexing
- Graceful degradation under load (reject new connections vs. crash)
- Azure Container Apps deployment MUST stay within free/minimal tier for light usage

**Rationale**: Cost efficiency differentiates Protogate from enterprise alternatives. Performance guarantees 
ensure production viability for real-time and high-throughput applications.

### V. Observability & Auditability

Every tunnel connection, authentication attempt, and operational event MUST be logged and traceable. 
Observability MUST enable rapid troubleshooting and compliance auditing without requiring code changes.

**Requirements:**
- Structured logging (JSON format) to Azure Log Analytics
- Minimum log data: timestamp, tunnel ID, source IP, destination, byte counts, duration, auth status
- Support for custom audit log retention policies (90 days minimum)
- Metrics exported to Azure Monitor: active tunnels, throughput, error rates, connection latency
- Distributed tracing for multi-hop requests (via OpenTelemetry where applicable)
- Health check endpoints for monitoring systems
- Alerting templates for common failure scenarios

**Rationale**: Enterprises require compliance evidence and operational visibility. Silent failures are 
unacceptable in production infrastructure.

## Security Requirements

### Authentication & Authorization
- MUST support token-based authentication for tunnel agents
- MUST support certificate-based authentication (mTLS) as an option
- MUST implement role-based access control for management APIs
- MUST integrate with Microsoft Entra ID for administrative access
- Token rotation MUST be supported with zero-downtime

### Network Security
- MUST support IP allowlisting at tunnel and server levels
- MUST integrate with Azure Front Door Web Application Firewall
- MUST enforce rate limiting to prevent abuse
- MUST implement DDoS protection via Azure DDoS Protection Standard (recommended)
- MUST support network isolation via Azure Virtual Networks

### Data Protection
- Tunnel traffic MUST be encrypted in transit (TLS 1.2+)
- Secrets MUST be encrypted at rest via Azure Key Vault
- MUST support customer-managed encryption keys (BYOK)
- MUST NOT log sensitive data (credentials, tokens, payload content)
- MUST provide configuration for GDPR/CCPA compliance (data residency, retention)

### Incident Response
- MUST provide security event logging for forensic analysis
- MUST support immediate tunnel termination without service restart
- MUST document incident response procedures
- MUST maintain a security disclosure policy

## Development Standards

### Code Quality
- C++ codebase MUST follow modern C++17/20 standards
- MUST use RAII patterns for resource management
- Memory safety tools (AddressSanitizer, Valgrind) MUST pass on all builds
- Static analysis (clang-tidy, cppcheck) MUST be integrated in CI/CD
- Code coverage MUST exceed 80% for critical security and network paths

### Testing Requirements
- Unit tests MUST cover all authentication and authorization logic
- Integration tests MUST validate tunnel establishment and data transfer
- Performance tests MUST validate latency and throughput requirements
- Security tests MUST include fuzzing for protocol parsers
- Load tests MUST validate scaling behavior (1, 10, 50, 100 concurrent tunnels)

### Documentation Requirements
- Every deployment scenario MUST have a quickstart guide
- Security configuration MUST be documented with threat model
- API contracts MUST be versioned and documented (OpenAPI where applicable)
- Troubleshooting runbooks MUST cover common failure scenarios
- Architecture Decision Records (ADRs) MUST document major design choices

### Deployment & Operations
- Infrastructure-as-Code MUST be provided for all Azure resources (Bicep/Terraform)
- CI/CD pipelines MUST enforce all quality gates before merge
- MUST support zero-downtime upgrades for tunnel servers
- MUST provide automated backup/restore procedures for configuration
- MUST document disaster recovery procedures (RTO: <1 hour, RPO: <15 minutes)

## Governance

This constitution supersedes all other development practices and guidelines. All feature specifications, 
implementation plans, and code reviews MUST verify compliance with these principles.

**Amendment Process:**
- Amendments require documented justification with impact analysis
- Major amendments (principle removal/redefinition) require version MAJOR bump
- Minor amendments (new principles/sections) require version MINOR bump
- Clarifications and corrections require version PATCH bump
- All amendments MUST update dependent templates (plan, spec, tasks)
- Amendment proposals MUST be reviewed before implementation work begins

**Compliance Verification:**
- All feature specifications MUST include Constitution Check section
- Code reviews MUST reference applicable constitutional principles
- Security-related PRs MUST be reviewed by security-designated maintainers
- Performance-related PRs MUST include benchmark results vs. requirements

**Conflict Resolution:**
- When requirements conflict, Security-First principle takes precedence
- Cost vs. performance tradeoffs MUST favor meeting minimum performance requirements
- Feature requests that violate core principles MUST be rejected or redesigned

**Version**: 1.0.0 | **Ratified**: 2025-11-21 | **Last Amended**: 2025-11-21
