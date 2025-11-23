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
- **vcpkg**: For dependency management (automatically set up)
- **Docker** (optional): For containerized builds
- **Azure CLI** (optional): For Azure deployments

### Build from Source

```bash
# Clone repository
git clone https://github.com/ormasoftchile/protogate.git
cd protogate

# Configure and build (vcpkg is automatically detected)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests to verify build
./build/unit_tests
./build/integration_tests

# Run server (requires configuration)
./build/protogate-server
```

### Environment Configuration

Create a `.env` file with required variables:

```bash
# Required
KEY_VAULT_URI=https://your-keyvault.vault.azure.net/
DNS_ZONE=tunnel.example.com

# Optional
PORT=443
AGENT_PORT=8443
LOG_ANALYTICS_WORKSPACE_ID=<workspace-id>
LOG_ANALYTICS_KEY=<workspace-key>
TCP_PORTS=9100,9200  # Additional TCP ports
LOG_LEVEL=INFO  # DEBUG, INFO, WARNING, ERROR
MAX_AGENTS=50  # Maximum concurrent tunnel agents
```

### Build with Docker

```bash
# Build container image
docker build -t protogate-server:latest -f docker/Dockerfile.alpine .

# Run with environment variables
docker run -p 443:443 -p 8443:8443 \
  -e PORT=443 \
  -e AGENT_PORT=8443 \
  -e KEY_VAULT_URI=https://your-vault.vault.azure.net/ \
  -e DNS_ZONE=tunnel.example.com \
  protogate-server:latest
```

### Deploy to Azure (Production)

Full Azure deployment with Container Apps, Key Vault, DNS Zone, and Log Analytics:

```bash
# Navigate to deployment directory
cd deploy/azure

# Log in to Azure
az login

# Create resource group
az group create --name protogate-prod --location eastus

# Deploy infrastructure (choose environment)
./deploy.sh protogate-prod eastus production

# Generate and store tunnel token
../scripts/generate-token.sh \
  --tunnel-id my-first-tunnel \
  --resource-group protogate-prod \
  --vault-name protogate-kv-prod
```

See [deploy/azure/README.md](deploy/azure/README.md) and [docs/quickstart.md](specs/001-tunnel-core-server/quickstart.md) for detailed deployment instructions.

### Test Your Deployment

```bash
# Create a tunnel
curl -X POST https://api.tunnel.example.com/v1/tunnels \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "my-app",
    "protocol": "HTTP",
    "target": "localhost:8080"
  }'

# Verify tunnel is accessible
curl https://my-app.tunnel.example.com/health

# View tunnel metrics
curl https://api.tunnel.example.com/v1/tunnels/my-app/metrics

# Delete tunnel
curl -X DELETE https://api.tunnel.example.com/v1/tunnels/my-app
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

## Troubleshooting

### Build Issues

**Problem**: `vcpkg not found` error during CMake configuration

```bash
# Solution: Set vcpkg root explicitly
export VCPKG_ROOT=/path/to/vcpkg
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
```

**Problem**: Missing dependencies (Boost, OpenSSL, nlohmann-json)

```bash
# Solution: Install via vcpkg (automatic during build)
./vcpkg/vcpkg install boost-asio boost-beast openssl nlohmann-json

# Or use system packages (Debian/Ubuntu)
sudo apt-get install libboost-dev libssl-dev nlohmann-json3-dev

# Or use system packages (macOS)
brew install boost openssl nlohmann-json
```

**Problem**: C++17 not supported by compiler

```bash
# Solution: Upgrade compiler
# Ubuntu/Debian
sudo apt-get install g++-9

# macOS
brew install llvm
```

### Runtime Issues

**Problem**: `Connection refused` when accessing tunnel

```bash
# Check server is running
docker ps  # or: ps aux | grep protogate-server

# Check server logs
docker logs protogate-container  # or: journalctl -u protogate

# Verify ports are accessible
nc -zv tunnel.example.com 443
nc -zv tunnel.example.com 8443

# Check firewall rules
sudo iptables -L -n | grep 443
```

**Problem**: `No tunnel found for hostname` error

```bash
# Verify tunnel exists
curl https://api.tunnel.example.com/v1/tunnels

# Check DNS resolution
nslookup my-app.tunnel.example.com
dig my-app.tunnel.example.com

# Verify DNS wildcard record
nslookup *.tunnel.example.com
```

**Problem**: `Token validation failed: not found`

```bash
# Generate new token
cd deploy/scripts
./generate-token.sh --tunnel-id my-app --resource-group protogate-prod --vault-name protogate-kv

# Verify token is in Key Vault
az keyvault secret show \
  --vault-name protogate-kv \
  --name tunnel-token-my-app

# Check token format (should be 256-bit base64)
echo "token-value" | base64 -d | wc -c  # Should be 32 bytes
```

**Problem**: `Failed to load certificate from Key Vault`

```bash
# Check certificate exists
az keyvault certificate show \
  --vault-name protogate-kv \
  --name wildcard-tunnel-example-com

# Verify managed identity has access
az keyvault set-policy \
  --vault-name protogate-kv \
  --object-id <managed-identity-id> \
  --certificate-permissions get list \
  --secret-permissions get list

# Check certificate format (must be PEM)
az keyvault certificate download \
  --vault-name protogate-kv \
  --name wildcard-tunnel-example-com \
  --file cert.pem
openssl x509 -in cert.pem -text -noout
```

**Problem**: High latency or slow performance

```bash
# Check server metrics
curl https://api.tunnel.example.com/v1/metrics

# Monitor resource usage
docker stats protogate-container
# Or: top -p $(pidof protogate-server)

# Check Azure Container App scaling
az containerapp show \
  --name protogate \
  --resource-group protogate-prod \
  --query "properties.template.scale"

# Enable AddressSanitizer for memory leak detection
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
./build/protogate-server
```

**Problem**: `IP blocked by allowlist` errors

```bash
# Check tunnel allowlist configuration
curl https://api.tunnel.example.com/v1/tunnels/my-app | jq '.allowlist'

# Update allowlist via API
curl -X PUT https://api.tunnel.example.com/v1/tunnels/my-app/allowlist \
  -H "Content-Type: application/json" \
  -d '{
    "allowlist": [
      "192.168.1.0/24",
      "10.0.0.0/8",
      "203.0.113.45/32"
    ]
  }'

# Test IP allowlist matching
curl -X POST https://api.tunnel.example.com/v1/test-allowlist \
  -H "Content-Type: application/json" \
  -d '{
    "tunnel_id": "my-app",
    "source_ip": "192.168.1.100"
  }'
```

### Azure-Specific Issues

**Problem**: `Managed identity authentication failed`

```bash
# Verify managed identity is enabled
az containerapp show \
  --name protogate \
  --resource-group protogate-prod \
  --query "identity"

# Check Key Vault access policies
az keyvault show \
  --name protogate-kv \
  --query "properties.accessPolicies"

# Test managed identity token acquisition
curl 'http://169.254.169.254/metadata/identity/oauth2/token?api-version=2018-02-01&resource=https%3A%2F%2Fvault.azure.net' \
  -H "Metadata: true"
```

**Problem**: `Log Analytics not receiving logs`

```bash
# Verify workspace credentials
az monitor log-analytics workspace show \
  --resource-group protogate-prod \
  --workspace-name protogate-logs

# Check environment variables
az containerapp show \
  --name protogate \
  --resource-group protogate-prod \
  --query "properties.template.containers[0].env"

# Query logs manually
az monitor log-analytics query \
  --workspace <workspace-id> \
  --analytics-query "ContainerAppConsoleLogs_CL | where ContainerAppName_s == 'protogate' | order by TimeGenerated desc | limit 100"
```

**Problem**: `DNS resolution not working`

```bash
# Verify DNS Zone exists
az network dns zone show \
  --name tunnel.example.com \
  --resource-group protogate-prod

# Check NS records are delegated
dig NS tunnel.example.com

# Verify CNAME wildcard record
az network dns record-set cname show \
  --zone-name tunnel.example.com \
  --resource-group protogate-prod \
  --name '*'

# Test DNS propagation
nslookup test.tunnel.example.com 8.8.8.8
```

### Debugging Tips

**Enable debug logging:**

```bash
# Set environment variable
export LOG_LEVEL=DEBUG

# Or in Docker
docker run -e LOG_LEVEL=DEBUG ...

# Or in Azure Container Apps
az containerapp update \
  --name protogate \
  --resource-group protogate-prod \
  --set-env-vars "LOG_LEVEL=DEBUG"
```

**Run static analysis:**

```bash
# Enable clang-tidy
cmake -B build -S . -DENABLE_CLANG_TIDY=ON
cmake --build build

# Enable cppcheck
cmake -B build -S . -DENABLE_CPPCHECK=ON
cmake --build build

# Run both
cmake -B build -S . -DENABLE_CLANG_TIDY=ON -DENABLE_CPPCHECK=ON
cmake --build build
```

**Memory leak detection:**

```bash
# Build with AddressSanitizer (automatically enabled in Debug)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/protogate-server

# Or use valgrind
valgrind --leak-check=full --show-leak-kinds=all ./build/protogate-server
```

**Network debugging:**

```bash
# Capture TLS handshake
tcpdump -i any -nn -s0 -X port 443

# Monitor connections
ss -tpn | grep protogate-server

# Check open file descriptors
lsof -p $(pidof protogate-server) | wc -l
```

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
