# Contributing to Desk Gateway

**Language:** English · [简体中文](./CONTRIBUTING.zh-CN.md)

Thanks for your interest in contributing.

Participation in this project is governed by the
[Code of Conduct](CODE_OF_CONDUCT.md). Potential vulnerabilities must follow
[SECURITY.md](SECURITY.md) and must not be disclosed in a public issue.

Before opening an issue, read [SUPPORT.md](SUPPORT.md) and pick the right
template. To put Web, scripts, phone, Watch, and keyboard on **your** LAN, follow
[local multi-client setup](docs/guides/local-multi-client-setup.en.md).

## How to contribute

1. Open an issue for bugs or proposals when the change is non-trivial.
2. Fork the repo and create a topic branch.
3. Keep diffs focused — avoid unrelated refactors.
4. Run the checks that match the layer you changed (below) before opening a PR.
5. Complete the pull request template, including automated and hardware verification boundaries.
6. Document protocol discoveries in `docs/` (what was verified vs unknown).
   Start from [docs/README.md](docs/README.md); usage changes go in
   `docs/guides/control-methods.md` or `docs/guides/rest-api.md`.
7. Record user-visible changes in [CHANGELOG.md](CHANGELOG.md).

## ESP-IDF (firmware)

This repo is pinned to **ESP-IDF v6.0.2**, target `esp32s3`. Do not mix versions.

Install from Espressif’s docs:
[Standard Setup of Toolchain for Windows / macOS / Linux](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/get-started/index.html).

After install, in the **same shell**:

```bash
export IDF_PATH=/path/to/esp-idf   # v6.0.2 checkout
source "${IDF_PATH}/export.sh"
idf.py --version                   # must print: ESP-IDF v6.0.2
./scripts/check-firmware.sh
```

`./scripts/flash-firmware.sh <port>` prefers `IDF_PATH` / `IDF_PYTHON_ENV_PATH` when
set. If those are empty, it falls back to the maintainer’s local Espressif paths
on this machine. Clones on another computer must set `IDF_PATH`.

Do not commit `build/`, `sdkconfig`, `managed_components/`, or secrets.
Prefer SoftAP provisioning over fragile serial Wi-Fi input.
Never invent unverified key codes (for example original-panel presets 2 / 3).

## Checks by layer

| Layer | Command |
|---|---|
| Firmware host tests + isolated build | `./scripts/check-firmware.sh` (needs IDF v6.0.2) |
| Firmware host tests only | `./scripts/check-height-decoder.sh` |
| Phone app | `cd mobile/app && npm ci && npm run typecheck && npm test` |
| Apple Watch | `cd mobile/watch && swift test` |
| XiaoZhi MCP | `python3 -m unittest discover -s integrations/xiaozhi-mcp/tests -p 'test_*.py' -v` |
| Ulanzi plugin logic | `cd integrations/ulanzi-d200h && npm ci && npm test` |

Compile or unit tests are not a substitute for real-desk motion, BLE radio, or ToF.

## Code style

- Match existing C / CMake / Web style in the tree
- New modules: short file/module comments explaining *why* and constraints
- Identifiers in English; user-facing docs may be Chinese or English
- English `README.md` plus Chinese `README.zh-CN.md` in any directory that has a README

## Pull requests

- Describe **what** and **why**
- Link related issues
- Include a short test plan (build / flash / SoftAP / Web hold up-down)
- Do not claim hardware verification that was not performed

## License

By contributing, you agree that your contributions are licensed under the MIT License (see [LICENSE](LICENSE)).
