# Desk Gateway 集成

**语言：** [English](README.md) · 简体中文

这里放走同一套局域网 REST 的第三方入口，和 Web、`scripts/desk-preset.sh` 共用控制面。它们都不直接调厂商 I²C Driver。运动仍然经过 `desk_core`（STOP、童锁、来源权限、ToF）。

怎么控桌见 [`docs/guides/control-methods.md`](../docs/guides/control-methods.md)。HTTP 契约见 [`docs/guides/rest-api.md`](../docs/guides/rest-api.md)。克隆后要改的 IP、密码和仓库路径见 [`docs/guides/local-multi-client-setup.md`](../docs/guides/local-multi-client-setup.md)。

| 目录 | 做什么 | 怎么接到 Desk Gateway |
|---|---|---|
| [xiaozhi-mcp](xiaozhi-mcp/README.zh-CN.md) | 小智云 MCP Endpoint 到本机 REST 桥接 | 五个固定 MCP 工具 |
| [ulanzi-d200h](ulanzi-d200h/README.zh-CN.md) | Ulanzi D200H 插件源码 | 请坐 / 站立 / 番茄时刻 |
| [karabiner](karabiner/README.zh-CN.md) | Karabiner-Elements Complex Modifications | 键盘快捷键和旋钮 jog |
| [goatremote](goatremote/README.zh-CN.md) | GoatRemote 提示词补充 | 语音坐姿 / 站姿，经 `desk-preset.sh` |

## 共用规则

- 只走局域网，不要把网关做公网端口映射。
- 鉴权用 `X-Desk-Key`，值等于当前 Web 密码。
- 不要自造运动接口。坐姿是档位 1，站姿是档位 4，最高安全位只有在 `raise_to_max_supported` 为真时才用 `raise-to-max`。
- 工具返回 `ok` 或 `state=started` 只表示网关接受了命令，不表示桌子已经到位。
- 密钥放在本地 `.env` 或宿主应用设置里，不要提交进仓库。

## 不在本目录

Home Assistant / MQTT、Matter、米家、华为目前只有方案文档。见 [`docs/future/mqtt-home-assistant.md`](../docs/future/mqtt-home-assistant.md) 和 [`docs/future/ecosystem-xiaomi-huawei.md`](../docs/future/ecosystem-xiaomi-huawei.md)。
