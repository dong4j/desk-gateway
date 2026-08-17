# Contributing to Desk Gateway

Thanks for your interest in contributing.

**中文说明：** 欢迎提 Issue / PR。提交前请先阅读 [SUPPORT.md](./SUPPORT.md)
并选择正确模板；改固件前请运行 `./scripts/check-firmware.sh`；协议相关改动请附上抓包或复现步骤。

Participation in this project is governed by the
[Code of Conduct](./CODE_OF_CONDUCT.md). Potential vulnerabilities must follow
[SECURITY.md](./SECURITY.md) and must not be disclosed in a public issue.

## How to contribute

1. Open an issue for bugs or proposals when the change is non-trivial.
2. Fork the repo and create a topic branch.
3. Keep diffs focused — avoid unrelated refactors.
4. For firmware changes, activate ESP-IDF and run `./scripts/check-firmware.sh` before opening a PR.
5. Complete the pull request template, including automated and hardware verification boundaries.
6. Document protocol discoveries in `docs/` (what was verified vs unknown).
   Start from [docs/README.md](./docs/README.md); usage changes go in
   `docs/guides/control-methods.md` or `docs/guides/rest-api.md`.

## Development notes

- Main firmware: `firmware/desk-gateway/`
- Repeatable compile check: `./scripts/check-firmware.sh` (ESP-IDF 6.0.2 in CI)
- Do not commit `build/`, `sdkconfig`, `managed_components/`, or secrets
- Prefer SoftAP provisioning over fragile serial input for Wi‑Fi setup
- Never invent unverified key codes (e.g. unknown presets)

## Code style

- Match existing C / CMake / Web style in the tree
- New modules: short file/module comments explaining *why* and constraints
- Identifiers in English; user-facing docs may be Chinese or English

## Pull requests

- Describe **what** and **why**
- Link related issues
- Include a short test plan (build / flash / SoftAP / Web hold up-down)

## License

By contributing, you agree that your contributions are licensed under the MIT License (see [LICENSE](./LICENSE)).
