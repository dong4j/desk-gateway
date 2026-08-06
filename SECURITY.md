# Security Policy

## Supported versions

This project is early-stage. Security fixes are applied on a best-effort basis to the default branch.

## Reporting a vulnerability

Please **do not** open a public issue for vulnerabilities that could put users at risk (e.g. auth bypass on the LAN Web UI).

Prefer contacting the maintainer privately (GitHub Security Advisory if enabled, or the maintainer’s contact on their GitHub profile).

Include:

- Affected commit / tag if known
- Reproduction steps
- Impact (e.g. unauthenticated control on LAN)

## Hardening expectations (users)

- Change the default Web password (`desk-gateway`) after first login
- Keep the device on a trusted LAN; **do not** port-forward or expose HTTP to the Internet
- Stay near the desk when commanding motion; rely on the motion timeout as a backstop, not the only safety measure
- Treat reverse‑engineered protocols as incomplete; unexpected firmware on the desk host may behave differently
