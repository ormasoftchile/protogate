# Protogate - Secure Reverse Tunneling Server

A high-performance, self-hosted reverse tunneling server built in C++ for exposing on-premises services securely through the cloud without VPNs or inbound firewall rules.

## Features

- 🔒 **Security-First**: TLS 1.2+ mandatory, token-based authentication, Azure Key Vault integration
- 🚀 **High Performance**: <10ms HTTP overhead, 100+ Mbps TCP throughput, 100k+ req/s per vCPU
- ☁️ **Azure-Native**: Seamless integration with Container Apps, DNS Zones, Key Vault, Log Analytics
- 🎯 **Protocol Support**: HTTP/HTTPS and raw TCP tunneling
- 📊 **Full Observability**: Structured logging, metrics, distributed tracing, health checks
- 🛡️ **Security Controls**: IP allowlisting (CIDR), rate limiting, audit logging
- 💰 **Cost-Effective**: Deploy for <$15/month with Azure Container Apps

## Quick Start

### Prerequisites

- **C++ Compiler**: GCC 9+ or Clang 10+ with C++17 support
- **CMake**: 3.20 or later
- **vcpkg**: For dependency management
- **Docker** (optional): For containerized builds
- **Azure CLI** (optional): For Azure deployments

### Build from Source

```bash
# Clone repository
git clone https://github.com/ormasoftchile/protogate.git
cd protogate

# Install vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=$(pwd)/vcpkg

# Configure and build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run server
./build/protogate-server
```

### Build with Docker

```bash
# Build container image
docker build -t protogate-server:latest -f docker/Dockerfile.alpine .

# Run server
docker run -p 443:443 -p 8443:8443 \
  -e PORT=443 \
  -e AGENT_PORT=8443 \
  -e KEY_VAULT_URI=https://your-vault.vault.azure.net \
  protogate-server:latest
```

### Deploy to Azure

See [quickstart.md](specs/001-tunnel-core-server/quickstart.md) for detailed Azure deployment instructions using Bicep.

```bash
cd deploy/azure
az deployment group create \
  --resource-group protogate-rg \
  --template-file main.bicep \
  --parameters @parameters.json
```

## Configuration

Protogate uses environment variables for configuration (12-factor app):

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `PORT` | No | `443` | HTTPS ingress port |
| `AGENT_PORT` | No | `8443` | Agent connection port |
| `KEY_VAULT_URI` | Yes | - | Azure Key Vault URL for certificates/tokens |
| `DNS_ZONE` | Yes | - | DNS zone for tunnel routing (e.g., `tunnel.mycorp.com`) |
| `LOG_ANALYTICS_WORKSPACE_ID` | No | - | Log Analytics workspace for structured logs |
| `LOG_ANALYTICS_KEY` | No | - | Log Analytics shared key |
| `TCP_PORTS` | No | - | Comma-separated TCP ports (e.g., `9100,9200`) |

## Architecture

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   Internet  │ HTTPS   │   Protogate  │  TLS    │ Tunnel Agent│
│   Client    │────────▶│    Server    │◀────────│  (On-Prem)  │
└─────────────┘         └──────────────┘         └─────────────┘
                              │                         │
                              │                         │
                        ┌─────▼──────┐            ┌────▼────┐
                        │ Azure Key  │            │  Local  │
                        │   Vault    │            │ Service │
                        └────────────┘            └─────────┘
```

## Testing

```bash
# Run unit tests
cmake --build build --target unit_tests
./build/unit_tests

# Run integration tests (requires Azure resources)
./build/integration_tests

# Run performance benchmarks
./build/benchmarks
```

## Performance Characteristics

- **Latency**: <10ms p95 overhead for HTTP tunneling
- **Throughput**: 100,000+ HTTP requests/sec per vCPU
- **TCP Performance**: 100+ Mbps per tunnel connection
- **Memory**: <512MB for 50 concurrent tunnels
- **Connections**: 10,000+ concurrent HTTP connections
- **Scalability**: Horizontal scaling to 10+ instances

## Security

- **TLS 1.2+**: Mandatory for all connections (server and agent)
- **Token Auth**: SHA-256 hashed tokens with 256-bit entropy
- **IP Allowlisting**: CIDR-based filtering per tunnel
- **Rate Limiting**: Token bucket algorithm, configurable per tunnel
- **Audit Logging**: Security events logged to Azure Log Analytics
- **No Secrets in Logs**: Tokens, credentials, and payload data never logged

See [docs/security.md](docs/security.md) for complete threat model and security controls.

## Documentation

- [Feature Specification](specs/001-tunnel-core-server/spec.md)
- [Implementation Plan](specs/001-tunnel-core-server/plan.md)
- [Data Model](specs/001-tunnel-core-server/data-model.md)
- [Azure Quickstart Guide](specs/001-tunnel-core-server/quickstart.md)
- [Agent Protocol](specs/001-tunnel-core-server/contracts/agent-protocol.md)
- [Management API](specs/001-tunnel-core-server/contracts/management-api.yaml)
- [Architecture Decisions](docs/adr/)

## Contributing

Contributions are welcome! Please read our contributing guidelines and code of conduct.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Support

- **Issues**: https://github.com/ormasoftchile/protogate/issues
- **Discussions**: https://github.com/ormasoftchile/protogate/discussions

## Acknowledgments

Built with:
- [Boost.Asio](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html) - Async I/O
- [OpenSSL](https://www.openssl.org/) - TLS/SSL support
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing
- [GoogleTest](https://github.com/google/googletest) - Testing framework
- [Google Benchmark](https://github.com/google/benchmark) - Performance benchmarks
