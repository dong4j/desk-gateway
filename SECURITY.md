# Security Policy

## Supported versions

This project is early-stage. Security fixes are applied on a best-effort basis to the default branch.

## Reporting a vulnerability

Please **do not** open a public issue or attach exploit details for
vulnerabilities that could put users at risk. Relevant examples include:

- Authentication bypass or unauthorized desk control on the LAN Web UI
- Exposed credentials, secrets, or private network data
- Protocol behavior that can cause uncontrolled or unexpectedly prolonged motion
- A bypass of stop, timeout, or other motion-safety controls

Private vulnerability reporting is not currently enabled for this repository.
Use a private contact method published on the
[maintainer's GitHub profile](https://github.com/dong4j) for an initial report.
If no private contact method is available, open the `Private contact request`
issue form **without technical details**, logs, captures, or proof-of-concept
material. A private channel can then be agreed upon.

Include:

- Affected commit / tag if known
- Minimal reproduction steps and required hardware
- Impact (e.g. unauthenticated control on LAN)
- Whether the issue has been verified on real hardware or only in simulation
- Suggested remediation or disclosure constraints, if any

This is an early-stage project, so reports are handled on a best-effort basis
without a guaranteed response or remediation time. Please allow the maintainer
to investigate and coordinate disclosure before publishing details.

## Hardening expectations (users)

- Change the default Web password (`desk-gateway`) after first login
- Keep the device on a trusted LAN; **do not** port-forward or expose HTTP to the Internet
- Stay near the desk when commanding motion; rely on the motion timeout as a backstop, not the only safety measure
- Treat reverse‑engineered protocols as incomplete; unexpected firmware on the desk host may behave differently
