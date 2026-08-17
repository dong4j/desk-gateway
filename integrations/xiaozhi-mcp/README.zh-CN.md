# 小智 AI MCP 桥接

**语言：** [English](README.md) · 简体中文

本目录把小智智能体提供的 MCP Endpoint 连接到 Desk Gateway REST。小智硬件继续使用官方
固件，Desk Gateway 不需要公网地址，也不需要重新实现固件运动控制。

```text
小智硬件 → 小智云智能体 → MCP Endpoint → 本机 mcp_pipe.py
                                      → desk_mcp.py → Desk Gateway REST
```

桥接只暴露五个固定工具：

| MCP 工具 | REST 请求 | 约束 |
|---|---|---|
| `desk.get_status` | `GET /api/v1/desk/status` | 只读 |
| `desk.raise_to_max` | `POST /api/v1/desk/raise-to-max` | 必须具有真实 ToF 和有界 Driver 能力 |
| `desk.goto_sit` | `POST /api/v1/desk/preset/1/goto` | 固定坐姿档位 |
| `desk.goto_stand` | `POST /api/v1/desk/preset/4/goto` | 固定站姿档位 |
| `desk.stop` | `POST /api/v1/desk/stop` | 不受运动前置检查阻塞 |

工具不接受 URL、HTTP Method、Header 或任意目标高度参数。

## 1. Desk Gateway 前置条件

Mac 必须能通过局域网访问 Desk Gateway。在启用运动工具前确认：

- `height_sim=false`
- `height_known=true`
- `tof_height_known=true`
- `raise_to_max_supported=true`
- `child_lock=false`
- `upward_blocked=false`
- `control_sources.rest=true`

## 2. 安装官方 MCP Pipe

桥接复用小智官方示例仓库，不在本仓库复制 WebSocket 协议实现：

```bash
git clone https://github.com/78/mcp-calculator.git /path/to/mcp-calculator
cd /path/to/mcp-calculator
git rev-parse HEAD

python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
python -m pip install \
  -r /path/to/desk-gateway/integrations/xiaozhi-mcp/requirements.txt
```

本实现于 2026-08-16 核对过官方示例提交
`c537f71d61fd73b47d6c8955b5df6d3721acf4e4`。安装时应记录实际使用的提交，升级后重新执行
本目录测试和工具注册检查。

最新版 `mcp` 包要求 Python 3.10 以上。本机系统自带的 `/usr/bin/python3` 可能仍是 3.9，
不要用它创建运行环境；本文以 Python 3.12 为例。

## 3. 获取 MCP Endpoint

登录小智控制台，在 JC3636W518C 当前使用的目标智能体中打开“配置角色 / 编辑功能”，复制
完整 MCP 接入点 WebSocket 地址。该地址包含智能体 token，只保存在本地 `.env`，不要写入
提示词、日志或仓库。

## 4. 创建本地配置并启动

复制模板：

```bash
cp integrations/xiaozhi-mcp/.env.example \
  integrations/xiaozhi-mcp/.env
chmod 600 integrations/xiaozhi-mcp/.env
```

编辑 `integrations/xiaozhi-mcp/.env`，填写完整的 `MCP_ENDPOINT`、`MCP_PIPE_DIR`、
`MCP_PYTHON`、`DESK_GATEWAY_URL` 和 `DESK_GATEWAY_KEY`。包含 `&`、`?` 或空格的值必须保留
模板中的单引号。

`.env` 已由仓库根目录的 `.gitignore` 排除。可以用下面的命令确认它不会被提交：

```bash
git check-ignore integrations/xiaozhi-mcp/.env
```

加载配置并检查 Desk Gateway：

```bash
set -a
source integrations/xiaozhi-mcp/.env
set +a

curl --fail --silent --show-error \
  -H "X-Desk-Key: ${DESK_GATEWAY_KEY}" \
  "${DESK_GATEWAY_URL}/api/v1/desk/status" | jq
```

启动时不需要再次 `source`，脚本会自动读取同目录的 `.env`：

```bash
./integrations/xiaozhi-mcp/scripts/run.sh
```

预期日志包含 WebSocket 连接成功和 MCP Server 启动。返回小智智能体刷新功能列表，应能看到
本页开头列出的五个 `desk.*` 工具。

## 5. 配置 launchd 常驻

复制模板：

```bash
cp integrations/xiaozhi-mcp/launchd/com.dong4j.desk-mcp.plist.example \
  ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
```

编辑副本并替换：

- `__PROJECT_ROOT__`：本仓库绝对路径；
- `__HOME__`：当前用户主目录绝对路径。

`launchd` 启动同一个 `run.sh`，会自动读取 `.env`，plist 中不再重复保存 Endpoint 或 Key。

校验并启动：

```bash
plutil -lint ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
launchctl bootstrap gui/$(id -u) \
  ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
launchctl kickstart -k gui/$(id -u)/com.dong4j.desk-mcp
```

查看状态和日志：

```bash
launchctl print gui/$(id -u)/com.dong4j.desk-mcp
tail -f ~/Library/Logs/desk-mcp.error.log
```

卸载：

```bash
launchctl bootout gui/$(id -u) \
  ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
```

## 6. 本地测试

Mock 测试不会连接真实升降桌，也不会执行真实运动：

```bash
python3 -m unittest discover \
  -s integrations/xiaozhi-mcp/tests \
  -p 'test_*.py' -v
```

## 7. 分层验收

1. 先调用 `desk.get_status`，核对数据与直接 REST 一致。
2. 使用错误 Desk Key，确认工具明确失败且不返回成功。
3. Desk Gateway 断网，确认工具在配置的超时时间内失败。
4. 调用 `desk.stop`，确认固定 STOP 路径可用。
5. 在桌旁、接近最高位并随时准备断电时，测试 `desk.raise_to_max`。
6. 最后测试“坐姿”“站姿”“升到最高”“停止”等真实语音表达。

工具返回 `state=started` 只表示设备已接受动作，不能向用户播报“已经到达”。最终停止由
Desk Gateway 的本地 ToF 安全链路完成，不依赖 MCP 连接继续在线。
