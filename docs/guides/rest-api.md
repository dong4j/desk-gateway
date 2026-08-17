# Desk Gateway REST API

| 项 | 内容 |
|---|---|
| 日期 | 2026-08-17 |
| 基线 | 当前主固件 `desk_web.c` 已注册路由 |
| 使用场景 | 脚本、Karabiner、GoatRemote、小智 MCP、Ulanzi、手机 Wi-Fi 回退 |

本文冻结局域网 HTTP 接口。所有运动命令最终进入 `desk_core`，受童锁、来源权限和 ToF 上升策略约束。不要把本接口暴露到公网。

浏览器控制台走 `Authorization: Bearer <login token>`。脚本和第三方集成用 `X-Desk-Key`，值等于当前 Web 登录密码。两个头对已认证接口等价。

## 约定

- 基础地址：`http://<设备IP>`，端口 `80`。mDNS 名为 `desk-gateway.local`，以设备实际解析为准。
- `Content-Type`：JSON 请求体使用 `application/json`。
- 运动类 POST 成功时返回 `{"ok":true,"err":"ESP_OK"}`。
- 未认证返回 `401` `{"error":"unauthorized"}`。
- 童锁或来源关闭导致拒绝时返回 `403`，并带 `"reason":"child_lock"` 或 `"source_disabled"`。
- 参数不合法或当前状态不允许时返回 `400` 或 `409`。
- STOP 不受普通运动权限阻塞。

先确认设备活着：

```bash
curl -s -H "X-Desk-Key: $DESK_KEY" "http://$DESK_IP/api/v1/desk/status"
```

`height_known` 和 `tof_height_known` 应为 `true`，`child_lock` 应为 `false`，`control_sources.rest` 应为 `true`，再发运动命令。

## 鉴权与配网

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/v1/setup/wifi` | SoftAP 配网，写入 STA SSID/密码 |
| POST | `/api/v1/auth/login` | `{"password":"..."}`，返回 `{"token":"..."}` |
| POST | `/api/v1/auth/password` | 已登录后修改 Web 密码 |

```bash
curl -s -X POST "http://$DESK_IP/api/v1/auth/login" \
  -H "Content-Type: application/json" \
  -d '{"password":"desk-gateway"}'
```

后续请求任选一种：

```text
X-Desk-Key: <当前 Web 密码>
Authorization: Bearer <login token>
```

## 状态

`GET /api/v1/desk/status` 需要鉴权。Web 约每 250 ms 轮询一次。常用字段：

| 字段 | 含义 |
|---|---|
| `status` | `idle` / `moving_up` / `moving_down` / `goto_preset` / `error` |
| `height_mm` | 产品高度；未知时为 `null` |
| `height_known` / `tof_height_known` | ToF 高度是否有效 |
| `right_gap_mm` / `right_gap_known` | TOF050C 右侧间距 |
| `child_lock` / `child_lock_reason` | 全局童锁 |
| `upward_blocked` | 当前是否禁止上升 |
| `raise_to_max_supported` | 能否使用有界最高位 |
| `min_height_mm` / `max_height_mm` | 默认 `550` / `940` |
| `preset1_height_mm` / `preset4_height_mm` | 默认 `550` / `870` |
| `control_sources.rest\|bluetooth\|panel` | 来源开关 |
| `driver` | 当前 Driver，产品固件为 `mxtark` |
| `build_id` / `git_version` | 确认烧录镜像 |
| `reminder` / `audio` | 番茄时钟与语音快照 |

`height_sim` 在产品固件中应为 `false`。高度未知时不要发档位或 `raise-to-max`。

## 运动

| 方法 | 路径 | 语义 |
|---|---|---|
| POST | `/api/v1/desk/up` | Hold 上升，需续期；Web/App 长按用这条 |
| POST | `/api/v1/desk/down` | Hold 下降 |
| POST | `/api/v1/desk/jog/up` | 约 500 ms 短租约上升，旋钮/Crown 用这条 |
| POST | `/api/v1/desk/jog/down` | 短租约下降 |
| POST | `/api/v1/desk/stop` | 立即停止 |
| POST | `/api/v1/desk/preset/1/goto` | 闭环到档位 1 |
| POST | `/api/v1/desk/preset/4/goto` | 闭环到档位 4 |
| POST | `/api/v1/desk/preset/{n}/save` | 保存档位；仅 Driver 支持时成功 |
| POST | `/api/v1/desk/raise-to-max` | 升到最高安全高度；不支持时拒绝，不会改成长按上升 |
| POST | `/api/v1/desk/controller/reset` | 8 秒控制盒重置序列；STOP 可提前打断 |

`up`/`down` 不是单击走到底。客户端必须在按住期间重复调用，松手发 `stop`。Jog 由固件租约兜底，停转后不必再发 stop，但显式 stop 仍然有效。

```bash
# 档位 4（站姿）
curl -s -X POST -H "X-Desk-Key: $DESK_KEY" \
  "http://$DESK_IP/api/v1/desk/preset/4/goto"

# 有界最高位（小智「最高」）
curl -s -X POST -H "X-Desk-Key: $DESK_KEY" \
  "http://$DESK_IP/api/v1/desk/raise-to-max"

# 立即停止
curl -s -X POST -H "X-Desk-Key: $DESK_KEY" \
  "http://$DESK_IP/api/v1/desk/stop"
```

仓库脚本 `scripts/desk-preset.sh` 封装了 `1`、`4`、`up`、`down`、`stop`。其中 `up`/`down` 走 jog，不是 hold。

## 高度与权限

| 方法 | 路径 | 请求体 |
|---|---|---|
| POST | `/api/v1/desk/child-lock` | `{"enabled": true\|false}` |
| POST | `/api/v1/desk/access` | `{"source":"rest\|bluetooth\|panel","enabled":true\|false}` |
| POST | `/api/v1/desk/min-height` | `{"min_height_mm": 550}` |
| POST | `/api/v1/desk/max-height` | `{"max_height_mm": 940}` |
| POST | `/api/v1/desk/presets` | `{"preset1_height_mm":550,"preset4_height_mm":870}` |
| GET | `/api/v1/desk/height-presets` | 自定义档位列表 |
| POST | `/api/v1/desk/height-presets` | 新建自定义档位 |
| POST | `/api/v1/desk/height-presets/{id}` | 修改 |
| POST | `/api/v1/desk/height-presets/{id}/goto` | 前往该档位 |
| DELETE | `/api/v1/desk/height-presets/{id}` | 删除 |
| POST | `/api/v1/desk/auto-child-lock` | `{"enabled":bool,"device_id":"..."}` |
| POST | `/api/v1/desk/presence` | `{"device_id":"..."}`，离开超时心跳 |

最低高度只约束档位写入，不在下降到机械最低位时主动 STOP。自定义档位最多 16 个，高度未知时拒绝执行。

## 蓝牙配对管理

这些接口走 REST，不经过 BLE Command Characteristic。手机设置页和 Web Bond 卡片使用同一组路径。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/bluetooth/bonds` | 匿名 Bond 列表、容量、配对窗口 |
| POST | `/api/v1/bluetooth/pairing-window` | 打开约 120 秒配对窗口 |
| DELETE | `/api/v1/bluetooth/pairing-window` | 提前关闭 |
| POST | `/api/v1/bluetooth/bonds/{id}` | 设置别名 |
| DELETE | `/api/v1/bluetooth/bonds/{id}` | 删除单个 Bond |
| DELETE | `/api/v1/bluetooth/bonds` | 全删 |

Bond 满额不会自动淘汰旧设备。删除会先停止该设备发起的运动。协议细节见 [三客户端与 Bond 管理](../architecture/ble-multi-client-bond-management.md)。

## 番茄时钟与系统

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/v1/reminder/action` | `{"action":"start_focus\|start_break\|pause\|resume\|skip\|stop\|snooze"}` |
| POST | `/api/v1/reminder/config` | 时长、`audio_enabled`、`volume_percent` |
| POST | `/api/v1/audio/action` | `{"action":"test_audio","prompt_id":"focus_done"}` 或 `{"action":"stop_audio"}` |
| POST | `/api/v1/system/restart` | 先 STOP，再重启 ESP32 |

试听只接受登记过的 `prompt_id`：`focus_done`、`break_done`、`snooze_done`、`attention_chime`。客户端字符串不会被当成文件路径。

非法提醒动作返回 `409`，不会被静默改成另一个动作。

## 小智与第三方约束

小智 MCP 只应调用 `status`、`raise-to-max`、`preset/1/goto`、`preset/4/goto`、`stop`。不要给模型任意 URL、HTTP Method 或目标毫米数。

旋钮和 Watch Crown 只用 jog。Web 和手机长按只用 hold。把两条语义接反，会出现点一下就持续升降，或按住却只动一下。
