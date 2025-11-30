# BugBash-Aware Access Layer (Concept Summary)

## Overview
The idea is to allow developers to expose their local or prototype environments securely during a bug-bashing session without manually managing accounts, groups, or RBAC. Access to the system is automatically inferred from a Microsoft Teams meeting or channel, with per-user temporary roles and complete lifecycle governance.

---

## Core Concept
During a bug-bashing session:

1. A Teams meeting or channel is used as the source of identity.
2. A “BugBash Session” is created and bound to that meeting/channel.
3. Participants are automatically recognized through Entra ID (AAD) authentication.
4. Each participant chooses their temporary role (Contributor or ReadOnly).
5. A short-lived access token is issued for the duration of the session.
6. Protogate exposes the developer's local environment securely to authenticated participants.
7. All permissions expire when the session ends.
8. Telemetry and user activity are captured for review and debugging.

---

## Flow

### 1. Session Creation
- Developer starts a bug-bash mode from a Teams app or CLI.
- The system creates a BugBash Session with:
  - `SessionId`
  - Meeting/channel binding
  - Duration and allowed roles
  - Associated Protogate endpoint for the environment

### 2. Participant Access
- Users click the bug-bash link in the Teams meeting.
- They authenticate via AAD automatically (Teams → browser SSO).
- System verifies they are part of the meeting/channel.
- On first access, they choose:
  - Contributor (write access)
  - ReadOnly (view-only)
- A signed, short-lived BugBash token is issued.

### 3. Application Integration
- Apps integrate a BugBash SDK (NuGet package).
- SDK validates the BugBash token and exposes:
  ```
  BugBashContext {
      SessionId
      UserId
      Role (Contributor | ReadOnly)
  }
  ```
- Developers apply authorization policies:
  - Contributors can mutate state
  - ReadOnly users can only GET/HEAD
- No local user accounts or custom RBAC needed.

### 4. Expiry and Policies
- Session auto-expires after defined duration.
- Tokens expire quickly and cannot be reused.
- Developers may set:
  - Allowed HTTP methods per role
  - Safety banners or UI indicators
  - Rate limits
  - Optional data isolation requirements

---

## Telemetry and Governance
- Every request includes session and role metadata.
- Telemetry (Application Insights / Azure Monitor):
  - `bugbash.session_id`
  - `bugbash.user_id`
  - `bugbash.role`
  - Request paths, status codes, timings
- Perfect for bug-bash debriefs, issue tracing, and activity accountability.

---

## SDK Support (for .NET / Aspire)
- Simple AppHost configuration:
  ```
  builder.AddBugBashAuth("bugbash", options => {
      options.Authority = "https://bugbash.mycorp.com";
      options.Audience  = "my-app-service";
  });
  ```
- ASP.NET Core integration:
  ```
  app.UseBugBashAuth();
  ```
- Authorization policies:
  ```
  [Authorize(Policy = "BugBashContributor")]
  ```

The developer never interacts directly with Teams, Graph, or Protogate.  
Everything is abstracted through the BugBash platform layer.

---

## Summary
This system provides:
- Temporary access control via Teams meeting identity
- Optional self-serve role selection (Contributor vs ReadOnly)
- Automatic expiry and strong governance
- Integrated telemetry for analysis and debug tracing
- Easy integration for .NET and Aspire applications via SDK

Ideal for demos, prototypes, bug-bashes, PR reviews, or any collaborative session requiring secure, time-bound access to local developer environments.
