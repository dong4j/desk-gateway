# Desk Gateway integrations

**Language:** English · [简体中文](./README.zh-CN.md)

Third-party clients that talk to the same LAN REST surface as Web and `scripts/desk-preset.sh`. None of them call a vendor I²C driver. Motion still goes through `desk_core` (STOP, child-lock, source ACL, ToF).

How to move the desk: [`docs/guides/control-methods.md`](../docs/guides/control-methods.md). HTTP contract: [`docs/guides/rest-api.md`](../docs/guides/rest-api.md).

| Directory | What it is | Talks to Desk Gateway with |
|---|---|---|
| [xiaozhi-mcp](./xiaozhi-mcp/README.md) | XiaoZhi cloud MCP endpoint → local REST bridge | Five fixed MCP tools |
| [ulanzi-d200h](./ulanzi-d200h/README.md) | Ulanzi D200H plugin source | Sit / stand / Pomodoro keys |
| [karabiner](./karabiner/README.md) | Karabiner-Elements complex modifications | Keyboard shortcuts and knob jog |
| [goatremote](./goatremote/README.md) | GoatRemote prompt extras | Spoken sit / stand via `desk-preset.sh` |

## Shared rules

- Stay on the LAN. Do not port-forward the gateway.
- Authenticate with `X-Desk-Key` equal to the current Web password.
- Do not invent extra motion APIs. Sit is preset 1, stand is preset 4, highest safe position is `raise-to-max` only when `raise_to_max_supported` is true.
- A tool returning `ok` or `state=started` means the gateway accepted the command. It does not mean the desk has arrived.
- Keep secrets in local `.env` or the host app’s settings. Do not commit keys.

## Not in this tree

Home Assistant / MQTT, Matter, Xiaomi, and Huawei are design docs only. See [`docs/architecture/mqtt-home-assistant.md`](../docs/architecture/mqtt-home-assistant.md) and [`docs/architecture/ecosystem-xiaomi-huawei.md`](../docs/architecture/ecosystem-xiaomi-huawei.md).
