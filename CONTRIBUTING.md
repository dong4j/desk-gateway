# Contributing to Desk Gateway

Thanks for your interest in contributing.

**中文说明：** 欢迎提 Issue / PR。改固件前请先在本地 `idf.py build`；协议相关改动请附上抓包或复现步骤。行为准则以互相尊重为准。

## How to contribute

1. Open an issue for bugs or proposals when the change is non-trivial.
2. Fork the repo and create a topic branch.
3. Keep diffs focused — avoid unrelated refactors.
4. For firmware changes, activate ESP-IDF and run `./scripts/check-firmware.sh` before opening a PR.
5. Document protocol discoveries in `docs/` (what was verified vs unknown).

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
