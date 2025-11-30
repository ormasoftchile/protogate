# Project Vision: Protogate + Printer4All + Email Printing

This document presents the three core ideas that together define a powerful, secure, and extensible platform for remote device access and printing.

---

## 1 — Protogate (Infrastructure Layer)
A self-hosted, secure alternative to ngrok, built in C++ and deployed inside each company’s Azure subscription.

### Purpose
Provide enterprises with a fully controlled, internal-only reverse tunneling solution for exposing on-prem or edge services (HTTP and TCP) to cloud apps without VPNs, without external SaaS, and without opening inbound firewall ports.

### Key Features
- Reverse tunnels over outbound TLS connections
- Supports HTTP and TCP tunnels
- Secure by design:
  - TLS / mTLS
  - Per-tunnel tokens
  - IP allowlists / Azure Front Door WAF
- Azure-native deployment:
  - Azure Container Apps, AKS, or VM
  - DNS Zone integration (e.g. *.tunnel.mycorp.com)
  - Key Vault for certs/keys
  - Log Analytics for observability
- Minimal cost for users (as low as $6–12 per month)

### Conceptual Azure Deployment

```
Protogate Resource (marketplace-style)
├── Tunnel Server (Container App / AKS / VM)
├── DNS Zone: *.tunnel.mycorp.com
├── Key Vault: TLS + token signing keys
└── Log Analytics workspace
```

### Example Tunnels
| Tunnel ID | Type | Public URL                                | Local Target         |
|-----------|------|---------------------------------------------|----------------------|
| api       | http | https://api.tunnel.mycorp.com               | 127.0.0.1:5000       |
| printer1  | tcp  | https://printer1.store123.tunnel.mycorp.com | 10.0.0.50:9100       |

---

# ngrok-Alternatives Comparison Matrix

| Name                        | Protocols (HTTP/HTTPS, TCP, UDP)        | Custom Domain / Subdomain        | Self-Hosting Supported?        | Entry Pricing*           | Notes / Strengths & Weaknesses                                                                   |
|-----------------------------|-----------------------------------------|----------------------------------|-------------------------------|--------------------------|-----------------------------------------------------------------------------------------------|
| ngrok                       | HTTP, HTTPS, TCP (no UDP)               | Yes (paid)                       | No (official self-hosted limited) | ~$10/month (Personal)    | Very popular; rich features (inspection, replay) but proprietary, traffic caps, no UDP.       |
| Localtunnel       | HTTP/HTTPS                              | Yes (subdomain on dev domain)   | Yes (self-host via server)     | Free (community)         | Very simple; mostly dev use; limited protocol support; less enterprise feature set.            |
| PageKite         | HTTP, HTTPS, TCP                        | Yes                              | Yes                            | ~$5.99/month (basic)     | Open source; broad OS support; fewer polish features; more DIY.                                |
| frp              | HTTP, HTTPS, TCP, UDP                   | Custom domains via config        | Yes                            | Free (open source)       | Strong self-hosting tool; very configurable, good for production DIY.                           |
| boringproxy     | HTTP, HTTPS, TCP                        | Yes                              | Yes                            | Free (open source)       | Simpler self-host proxy; good for small teams; fewer enterprise features.                       |
| inlets          | HTTP, HTTPS, TCP                        | Yes                              | Yes                            | Free (open source)       | Designed also for Kubernetes/edge; good self-host option.                                       |
| Zrok             | HTTP, HTTPS, TCP, UDP                   | Yes                              | Yes                            | Free tier / paid SaaS    | More advanced protocol support (including UDP); good self-hosting potential.                     |
| LocalXpose      | HTTP, HTTPS, TCP, UDP                   | Yes                              | Limited self-host option      | ~$6/month entry          | Strong feature-set; supports UDP; less strong self-host story.                                   |
| Teleport        | HTTP, HTTPS, TCP (access proxy mostly)  | Yes                              | Yes (self-host community edition) | Contact enterprise       | More than tunneling: full access proxy/auth/security platform.                                  |
| Cloudflare Tunnel| HTTP/HTTPS, TCP                         | Yes                              | No (managed SaaS)             | Free tier + paid plans   | Managed by Cloudflare; very strong infra; less control if you self-host is requirement.         |

\*Pricing is approximate entry/lowest tier as of 2025 and may change.

---

### Key Observations & How They Relate to Your Project  
- **Self-hosting is a strong differentiator.** Many alternatives are SaaS or limited self-host; your focus on deployment within the user’s own Azure subscription gives you a unique positioning.  
- **Protocol breadth matters.** Some tools support only HTTP; your design (HTTP + TCP tunnels) places you above many dev tools for enterprise use.  
- **Custom domain/subdomain support** is standard; make sure your Protogate includes this out of the gate (e.g., `*.tunnel.mycorp.com`).  
- **Enterprise features** (tokens, mTLS, IP whitelists, audit logs) are less common in many tools — nice for you to highlight.  
- **Cost transparency & control** is a plus. Many tools lock features behind paid tiers; self-hosting shifts cost model to user infra (which aligns with your pitch).

---
