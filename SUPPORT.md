# Support

Desk Gateway is an early-stage, community-maintained hardware project. Support
is best-effort; there is no guaranteed response time or private one-to-one
support channel.

**中文说明：** 本项目当前为早期硬件项目，支持由社区尽力提供，不承诺响应时限。提交问题前请先查看文档和已有 Issue，并按下面的边界选择渠道。

## Before asking for help

1. Read the [README](./README.md),
   [documentation index](./docs/README.md),
   [control methods](./docs/guides/control-methods.md),
   [bring-up checklist](./docs/bringup-checklist.md), and relevant protocol
   notes in [`docs/`](./docs/).
2. Search existing issues for the same hardware, firmware version, or symptom.
3. Run `./scripts/check-firmware.sh` for firmware build failures.
4. Remove Wi-Fi passwords, tokens, MAC addresses, and other private data from
   logs or captures before sharing them.

## Choose the right channel

| Need | Channel |
|------|---------|
| Documented behavior fails or a reproducible defect exists | Open a **Bug report** using the issue form |
| A capability, desk driver, integration, or documentation workflow is missing | Open a **Feature request** using the issue form |
| General setup or usage question | Check documentation and existing issues first; if the documented path fails, file a Bug report with complete environment details |
| Potential vulnerability or unsafe unauthorized control | Follow [SECURITY.md](./SECURITY.md); do **not** publish details in an issue |
| Community conduct incident | Follow [CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md) |

## Hardware and protocol evidence

For desk movement, driver, or reverse-engineered protocol reports, include:

- Desk controller / handset model and ESP32 board
- Firmware commit or tag and relevant configuration changes
- Wiring and power arrangement, without exposing private network credentials
- Minimal reproduction steps and the observed result
- Sanitized serial logs, logic-analyzer captures, or photos when available
- Which behavior is verified on hardware and which remains an assumption

Stay near the desk during testing and keep a physical stop or power-disconnect
path available. Do not rely on firmware timeout as the only safety measure.
