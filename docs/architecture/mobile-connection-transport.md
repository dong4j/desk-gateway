# 移动端 BLE / Wi-Fi 双通道方案

## 目标

Desk Gateway App 只负责呈现 UI 和发出统一控制意图，不让页面感知 BLE GATT 或
局域网 REST 的协议差异。默认优先使用 BLE；BLE 不可用、连接失败或已连接后断开时，
自动尝试同一局域网内的 REST 接口。

```text
Home / Settings / Hold Controller
                │
          DeskClient 契约
                │
       DeskConnectionManager
          ┌─────┴─────┐
          │           │
      BLE GATT     Wi-Fi REST
          │           │
          └─────┬─────┘
                │
        ESP32 desk_core
```

## 连接模式

设置页提供三种模式：

| 模式 | 行为 |
|---|---|
| 自动 | 先连接 BLE；5 秒内失败则连接 REST；已连接 BLE 断开后也尝试 REST |
| 仅 BLE | 只使用 BLE，不回退 REST |
| 仅 Wi-Fi | 直接使用 REST，不扫描 BLE |

REST 默认地址是 `desk-gateway.local`。固件通过 mDNS 公布
`http://desk-gateway.local/`；若路由器、手机或网络环境无法解析 `.local`，可以在设置页
填写 ESP32 当前 IP，例如 `192.168.21.65`。REST 密码通过现有 `X-Desk-Key` 请求头发送。

连接方式和 REST 地址、密码保存在 App 本地。它们是手机侧连接参数，不会修改 ESP32 的
Wi-Fi 凭证或登录密码。

## 统一状态与配置

BLE State / Config Characteristic 和 `GET /api/v1/desk/status` 都映射为同一个
`DeskClientSnapshot`：

- 当前通道、连接阶段和设备信息；
- 高度、运动状态、童锁和向上阻止状态；
- REST / Bluetooth / Panel 来源权限；
- 最高安全高度、档位 1 和档位 4；
- 固件构建信息。

因此 Home 和 Settings 不需要为两种连接方式维护两套业务状态。REST 空闲时每秒同步一次，
运动时每 250ms 同步一次，以兼顾实时性和 ESP32 负载。

## 安全规则

1. BLE 与 REST 的命令最终都进入 `desk_core`，童锁、来源权限、安全高度和超时仍由固件
   统一裁决，App 不是安全边界。
2. 通道切换只建立新的状态会话，绝不重放之前的 HOLD、档位或其他运动命令。
3. BLE 断开时 App 会立即取消本地 HOLD 续期；固件侧 BLE 租约/断连停止仍是最终保护。
4. REST 长按继续复用同一 `DeskHoldController`：持续发送 HOLD，松手立即发送 STOP。
5. REST 连续三次轮询失败后进入断开状态，不把旧高度伪装成实时状态。

## 原生平台要求

移动端使用局域网 HTTP，需要以下原生配置：

- iOS：Local Network 用途说明、Bonjour `_http._tcp`、允许本地网络 HTTP；
- Android：`ACCESS_NETWORK_STATE`，并允许本地明文 HTTP；
- Expo：使用 `expo-build-properties` 生成 Android cleartext 配置。

这些配置位于 `mobile/app/app.json`，修改后不能只启动 Metro，必须重新构建并安装
Development Build。

## 真机验收

1. 烧录包含 mDNS 的最新固件，确认日志出现
   `mDNS ready: http://desk-gateway.local/`。
2. 手机与 ESP32 连接同一局域网，在 Safari 打开
   `http://desk-gateway.local/`；失败时先用 ESP32 日志中的 IP 验证。
3. App 设置为“自动”，确认 BLE 可用时首页显示当前连接为 `BLE`。
4. 关闭手机蓝牙或让设备超出 BLE 范围，确认 App 自动显示 `Wi-Fi REST`，且不会自行恢复
   之前的运动。
5. 分别通过 Wi-Fi 验证 STOP、按住升降、档位、童锁、最高安全高度和档位配置同步。
6. 关闭 ESP32 的 REST 来源权限，确认 Wi-Fi 控制被固件拒绝；重新允许后恢复。

自动化测试覆盖 REST 认证/接口映射和 BLE 失败后的 Wi-Fi 回退；断连时序、局域网权限弹窗
及真实升降仍必须以真机验收为准。
