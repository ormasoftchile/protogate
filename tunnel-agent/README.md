# Protogate Tunnel Agent

A Python client for the Protogate reverse tunnel server. Enables secure access to local services through persistent outbound connections.

## Features

- 🔒 **Secure TLS Connection**: TLS 1.2+ encryption
- 🚀 **HTTP/2 Protocol**: Efficient multiplexed connections
- 🔑 **Token Authentication**: Bearer token authentication
- ❤️ **Health Monitoring**: Automatic heartbeat and reconnection
- 🔄 **Auto Reconnect**: Exponential backoff on connection failures
- ⚙️ **Flexible Config**: CLI, config file, or environment variables

## Quick Start

### Installation

```bash
# Install from source
cd tunnel-agent
pip install -e .

# Or install dependencies directly
pip install -r requirements.txt
```

### Basic Usage

```bash
# Run with CLI arguments
tunnel-agent \
  --server tunnel-agent.mycorp.com:8443 \
  --token tnl_abc123... \
  --tunnel-id api \
  --local-url http://localhost:3000

# Or use config file
tunnel-agent --config tunnel-agent.yaml

# Or use environment variables
export TUNNEL_SERVER=tunnel-agent.mycorp.com:8443
export TUNNEL_TOKEN=tnl_abc123...
export TUNNEL_ID=api
export LOCAL_URL=http://localhost:3000
tunnel-agent
```

### Configuration File

Create `tunnel-agent.yaml`:

```yaml
server:
  host: tunnel-agent.mycorp.com
  port: 8443
  verify_tls: true

tunnel:
  id: api
  token: tnl_abc123...

local:
  url: http://localhost:3000
  timeout: 1800  # 30 minutes

health:
  heartbeat_interval: 30
  heartbeat_timeout: 60

reconnect:
  initial_delay: 1
  max_delay: 60
  max_attempts: 0  # 0 = infinite
```

## Architecture

```
Internet → Protogate Server → Tunnel Agent → Local Service
                                   (You)        (localhost:3000)
```

1. Agent connects to server with TLS on port 8443
2. Agent authenticates with bearer token
3. Server forwards HTTP requests to agent via HTTP/2
4. Agent forwards requests to local service
5. Agent returns responses to server

## Development

### Setup Development Environment

```bash
# Install dependencies
pip install -r requirements.txt

# Install development dependencies
pip install pytest pytest-asyncio pytest-cov black mypy

# Run tests
pytest tests/

# Run with coverage
pytest --cov=agent tests/

# Format code
black agent/ tests/
```

### Project Structure

```
tunnel-agent/
├── agent/
│   ├── __init__.py
│   ├── main.py           # CLI entry point
│   ├── config.py         # Configuration
│   ├── client.py         # TLS/HTTP2 client
│   ├── forwarder.py      # Request forwarding
│   ├── heartbeat.py      # Health monitoring
│   └── reconnect.py      # Reconnection logic
├── tests/
│   ├── test_client.py
│   ├── test_forwarder.py
│   └── test_config.py
├── requirements.txt
├── setup.py
└── README.md
```

## Troubleshooting

### Connection Refused

```
Error: Connection refused to tunnel-agent.mycorp.com:8443
```

**Solution**: Verify server is running and port 8443 is accessible.

### Authentication Failed

```
Error: 401 Unauthorized - Invalid token
```

**Solution**: Check token is valid and not expired. Generate new token via Management API.

### Local Service Unavailable

```
Error: 502 Bad Gateway - Connection to localhost:3000 refused
```

**Solution**: Ensure local service is running on the configured port.

## Security

- Never commit tokens to version control
- Use environment variables or secure config files
- Enable TLS verification in production (`verify_tls: true`)
- Rotate tokens regularly via Management API

## License

MIT License - see LICENSE file

## Support

- Documentation: https://github.com/ormasoftchile/protogate
- Issues: https://github.com/ormasoftchile/protogate/issues
