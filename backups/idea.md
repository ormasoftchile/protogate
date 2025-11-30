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
Protogate Resource (marketplace-style)
|
|-- Tunnel Server (Container App / AKS / VM)
|-- DNS Zone: *.tunnel.mycorp.com
|-- Key Vault: TLS + token signing keys
|-- Log Analytics workspace

### Example Tunnels
| Tunnel ID | Type | Public URL                                | Local Target         |
|-----------|------|---------------------------------------------|----------------------|
| api       | http | https://api.tunnel.mycorp.com               | 127.0.0.1:5000       |
| printer1  | tcp  | https://printer1.store123.tunnel.mycorp.com | 10.0.0.50:9100       |

---

## 2 — Printer4All (Application Layer)
A remote printing service built on top of Protogate, enabling businesses to print securely to any branch, POS, or on-prem printer, including legacy USB/serial thermal printers.

### Purpose
Provide businesses with a secure, cloud-based printing workflow that works with any printer, even those behind NAT or lacking network capabilities.

### How It Works
1. At each branch or POS:
   - Install tunnel-agent
   - Install print-agent (local print service using OS drivers)
2. In the cloud:
   - Printer4All accepts print jobs via API, UI, or Email
   - Resolves printer via tunnel ID
   - Sends job securely over Protogate
3. On site:
   - Agent uses OS print APIs (Windows Spooler / CUPS)
   - Printer prints using the vendor driver

### Printer4All Features
- Supports legacy hardware (USB/serial thermal printers)
- Uses native OS drivers
- Supports labels, receipts, PDFs, TXT, images
- Admin UI for controlling printers, templates, policies
- Job logs, status, retries
- Optional queuing for offline printers

### Architecture
Cloud App -> Printer4All API  
           -> Protogate Server  
           -> tunnel-agent (branch)  
           -> print-agent (OS driver)  
           -> Printer (thermal / USB / IP)

### Offering Options
- Self-hosted by companies alongside their Protogate
- Free public demo instance hosted by you, limited but useful for tests

---

## 3 — Printer4All Email Service (Email -> Print)
An optional module allowing users and systems to send an email that gets converted automatically into a print job.

### Purpose
Enable email-to-print workflows for automation, receipts, tickets, reports, labels, and legacy systems that only know how to send emails.

### How It Works
Each printer gets a unique email address, for example:
printerA.123456@print4all.xyz

Printer4All receives emails via:
- A simple SMTP receiver, or
- Integration with 365/Gmail via IMAP/Graph API

The system extracts:
- From / To
- Subject and body
- Attachments (PDF, TXT, PNG, JPG)
- Formatting hints

Then generates a print job and delivers it through Protogate.

---

## Email Formatting Hints

### 1. Subject Tags
Example:
Subject: Order #1234 [P4A a4 onesided bw copies=2]

### 2. Body Block
Example:
[P4A]
paper = A4
sides = duplex
color = bw
copies = 3
[/P4A]

### 3. Email Headers
Example:
X-P4A-Paper: A4  
X-P4A-Sides: 1  
X-P4A-Copies: 2  

### Resolution Order
1. Printer default profile
2. Template (if provided)
3. Subject, body, or header overrides
4. Policy enforcement (admin rules)

Final print job config example:
{
  "paper": "A4",
  "sides": "one-sided",
  "color": "bw",
  "copies": 2
}

---

# Summary

DNS-Tunnel  
A secure, Azure-deployable reverse tunnel infrastructure — the open-source foundation.

Printer4All  
A cloud application running on top of Protogate that enables remote printing in any business scenario.

Printer4All Email Service  
An email-based printing mechanism with admin policies and user override syntax.

Together, these form a coherent platform for secure remote access and universal printing, usable by enterprises and hobbyists, entirely under their own control.
