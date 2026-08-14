# 通过小智 AI 控制升降桌

> 更新时间：2026-08-15
>
> 小智 ESP32 固件基线：`v2.4.2`
>
> 本地后端基线：`xiaozhi-esp32-server v0.9.6`
>
> 当前状态：接入方案和操作步骤已确定；MCP 桥接尚未创建、部署和实机验收。

本文记录如何让小智 AI 把“升降桌升高到最高”“切换到站立高度”“停止升降桌”等语音
指令转换成受控的 MCP 工具调用，再由局域网桥接程序访问 Desk Gateway REST 接口。

小智固件、本地 Server 和两块硬件的烧录说明见
[小智 AI 固件选择与本地 Server 部署](./10-xiaozhi-ai-firmware-and-local-server.md)。本文只讨论
语音控制升降桌的接入链路。

## 1. 结论

推荐使用下面的链路，不需要修改或重新编译两台小智硬件的固件：

```mermaid
flowchart LR
    Voice["用户语音"] --> Device["小智 AI 硬件<br/>xiaozhi-esp32 v2.4.2"]
    Device --> Server["本地 xiaozhi-esp32-server v0.9.6"]
    Server --> Endpoint["MCP Endpoint Server<br/>端口 8004"]
    Endpoint --> Bridge["desk_mcp.py<br/>MCP 工具桥接"]
    Bridge --> REST["Desk Gateway REST<br/>X-Desk-Key"]
    REST --> Core["desk_core + YourDesk Driver"]
    Core --> Safety["TOF400C 高度闭环<br/>TOF050C 右侧障碍保护"]
    Safety --> Desk["升降桌"]
```

这样设计有三个直接好处：

- 小智硬件继续使用官方固件，JC3636W518C 和 Xmini-C3 不需要分别维护定制代码。
- Desk Gateway 地址和 `X-Desk-Key` 只保存在局域网桥接进程中，不写入小智固件或智能体提示词。
- 最终停止条件由 Desk Gateway 本机执行。桥接程序发出上升指令后即使退出，ESP32 仍会根据
  TOF400C 的安全上限停止。

不推荐让大模型生成任意 URL、HTTP Method 或请求体。大模型只能调用文档中列出的固定工具，
工具内部再映射到固定 REST 路由。

## 2. 版本和协议基线

截至 2026-08-15，本文采用以下版本：

| 组件 | 版本 | 用途 |
| --- | --- | --- |
| `78/xiaozhi-esp32` | `v2.4.2` | 两台小智硬件的设备固件 |
| `xinnan-tech/xiaozhi-esp32-server` | `v0.9.6` | 本地语音、智能体和工具调度后端 |
| `xinnan-tech/mcp-endpoint-server` | 部署时固定实际提交 | 将一个智能体与外部 MCP 工具桥接 |
| `78/mcp-calculator` | 部署时固定实际提交 | 使用其中的 `mcp_pipe.py` 连接 MCP 接入点 |
| Desk Gateway | 当前仓库固件 | 提供认证 REST 和设备侧运动保护 |

小智当前推荐使用 MCP 扩展 IoT 控制。设备和后端通过 `initialize`、`tools/list`、
`tools/call` 完成工具发现和调用。本文接入的是本地 Server 提供的外部 MCP 接入点，不是在
小智 ESP32 上注册一个直接访问 REST 的板级工具。

部署时不要长期跟随 `latest`。首次跑通后记录四个仓库的 commit hash，后续升级先在测试环境
重新执行本文验收清单。

## 3. 语音语义和 REST 映射

### 3.1 工具清单

桥接程序只暴露以下五个工具：

| MCP 工具 | 用户表达示例 | Desk Gateway REST | 含义 |
| --- | --- | --- | --- |
| `desk.get_status` | “桌子现在多高” | `GET /api/v1/desk/status` | 查询当前高度、状态和安全配置 |
| `desk.raise_to_max` | “升降桌升高到最高” | `POST /api/v1/desk/up` | 持续上升，由 ESP32 在最高安全高度停止 |
| `desk.goto_sit` | “切换到坐姿” | `POST /api/v1/desk/preset/1/goto` | 闭环前往档位 1，默认 `560 mm` |
| `desk.goto_stand` | “切换到站姿” | `POST /api/v1/desk/preset/4/goto` | 闭环前往档位 4，默认 `870 mm` |
| `desk.stop` | “停下桌子” | `POST /api/v1/desk/stop` | 立即停止当前运动 |

工具名使用 `module.action` 形式，与小智设备侧 MCP 的命名习惯一致。如果实际使用的 LLM
Provider 不接受工具名中的句点，可统一改成 `desk_get_status`、`desk_raise_to_max` 等下划线
形式；桥接逻辑不变。

### 3.2 “最高”和“站立”不是同一个动作

Desk Gateway 当前默认值为：

```text
最高安全高度：940 mm
档位 1：      560 mm
档位 4：      870 mm
```

这些数值是 TOF400C 的原始距离，不是卷尺测得的桌面离地高度。因此：

- “升到最高”“升到顶”调用 `desk.raise_to_max`，目标是当前 `max_height_mm`。
- “站立模式”“站姿高度”调用 `desk.goto_stand`，目标是当前 `preset4_height_mm`。
- “升高一点”没有固定目标，第一版不自动执行，避免把模糊语义转换成持续上升。
- “降到最低”第一版也不提供独立工具；如果需要最低位置，使用已经配置并验收的
  `desk.goto_sit`。

### 3.3 `raise_to_max` 为什么可以调用持续上升接口

当前 Desk Gateway 默认启用 `CONFIG_DESK_TOF_ENABLE=y`，并在 ESP32 本地执行以下保护：

- 上升开始前检查 TOF400C 高度是否可用。
- 运动过程中持续检查 TOF400C；到达 `max_height_mm` 后直接发送 STOP。
- TOF400C 在运动中不可用时停止。
- 高度低于 `800 mm` 时，TOF050C 右侧距离小于 `80 mm` 会停止；右侧传感器不可用也会
  阻止上升。
- 默认最高安全高度是 `940 mm`，可配置范围是 `560..940 mm`。

上升通用超时仍然是 `0`，这是有意设计：上升停止依赖本机传感器闭环，而不是 MCP、网络
或 Server 延迟。只有在上述 ToF 配置已经烧录并完成真桌验收后，才允许暴露
`desk.raise_to_max`。其他 Driver、SIM 高度或未安装 ToF 的固件不能套用本文映射。

REST 返回 `200` 只表示 ESP32 已接受动作，不表示桌子已经到达目标。桥接工具的返回文案必须
使用“已经开始上升，将由设备在安全上限停止”，不能提前回复“已经到达最高”。

## 4. 接入前检查 Desk Gateway

以下命令在运行 MCP 前完成。不要把真实密钥写进 Shell history、文档或 Git 仓库；下面只用
临时占位值说明格式。

```bash
export DESK_GATEWAY_URL='http://192.168.21.65'
export DESK_GATEWAY_KEY='replace-with-local-desk-key'
```

查询状态：

```bash
curl --fail --silent --show-error \
  --connect-timeout 2 \
  --max-time 5 \
  --header "X-Desk-Key: ${DESK_GATEWAY_KEY}" \
  "${DESK_GATEWAY_URL}/api/v1/desk/status" | jq
```

至少确认：

```json
{
  "status": "idle",
  "height_known": true,
  "tof_height_known": true,
  "child_lock": false,
  "upward_blocked": false,
  "max_height_mm": 940,
  "preset1_height_mm": 560,
  "preset4_height_mm": 870,
  "control_sources": {
    "rest": true
  },
  "driver": "yourdesk_v1"
}
```

以上只是字段示例，实际高度和配置以设备返回值为准。出现下列任一情况时，不启动语音运动
工具：

- `height_known` 或 `tof_height_known` 为 `false`；
- `upward_blocked` 为 `true`；
- `control_sources.rest` 为 `false`；
- Driver 不是已经完成相同安全验收的实现；
- 页面中的最高安全高度尚未通过真桌短行程测试。

先验证 STOP：

```bash
curl --fail --silent --show-error \
  --connect-timeout 2 \
  --max-time 5 \
  --request POST \
  --header "X-Desk-Key: ${DESK_GATEWAY_KEY}" \
  "${DESK_GATEWAY_URL}/api/v1/desk/stop"
```

预期响应：

```json
{"ok":true,"err":"ESP_OK"}
```

## 5. 启用本地 Server 的 MCP 接入点

本节假定已经按照前一篇文档部署好全模块 `xiaozhi-esp32-server`，并且小智硬件可以连接本地
Server 完成一次正常对话。

### 5.1 部署 MCP Endpoint Server

在 Mac 上单独获取源码：

```bash
git clone https://github.com/xinnan-tech/mcp-endpoint-server.git
cd mcp-endpoint-server
git rev-parse HEAD
docker compose up -d
docker compose ps
docker logs -f mcp-endpoint-server
```

日志会给出两类地址：

```text
智控台参数地址： http://<容器地址>:8004/mcp_endpoint/health?key=<key>
工具接入地址：   ws://<容器地址>:8004/mcp_endpoint/mcp/?token=<token>
```

容器日志里的地址可能是 `172.x.x.x`。给智控台和桥接程序使用时，应换成 Mac 的固定局域网
IP，例如：

```text
http://192.168.21.10:8004/mcp_endpoint/health?key=<key>
ws://192.168.21.10:8004/mcp_endpoint/mcp/?token=<token>
```

先在 Mac 上访问 health 地址。预期结果中 `status` 为 `success`：

```bash
curl 'http://192.168.21.10:8004/mcp_endpoint/health?key=replace-with-key'
```

`key` 和 `token` 都是凭据，不要提交到仓库、截图公开或放进智能体提示词。

### 5.2 在智控台启用 MCP 接入点

使用管理员账号打开 `http://<Mac局域网IP>:8002`：

1. 进入“参数字典”→“系统功能配置”。
2. 勾选“MCP 接入点”并保存。
3. 进入“参数字典”→“参数管理”。
4. 搜索 `server.mcp_endpoint`。
5. 填写上一步的 health 地址，即
   `http://<Mac局域网IP>:8004/mcp_endpoint/health?key=<key>`。
6. 保存后进入目标智能体，打开“配置角色”→“编辑功能”。
7. 复制该智能体显示的 MCP 接入点 WebSocket 地址。

每个智能体的 MCP 地址包含独立 token。后续 `MCP_ENDPOINT` 必须使用准备控制升降桌的那个
智能体地址。

## 6. 创建 Desk MCP 桥接

本文复用官方示例仓库的 `mcp_pipe.py`，只增加一个本地 `desk_mcp.py`。以下操作在独立目录
完成，不需要把桥接代码写进小智或 Desk Gateway 固件仓库。

```bash
git clone https://github.com/78/mcp-calculator.git desk-mcp-bridge
cd desk-mcp-bridge
git rev-parse HEAD

python3.10 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

创建 `desk_mcp.py`，内容如下：

```python
"""把小智 MCP 的固定工具映射到局域网 Desk Gateway REST。"""

import json
import os
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from mcp.server.fastmcp import FastMCP


def required_env(name: str) -> str:
    """启动时拒绝缺失配置，避免工具在运动请求中临时猜测地址。"""
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError(f"missing required environment variable: {name}")
    return value


DESK_GATEWAY_URL = required_env("DESK_GATEWAY_URL").rstrip("/")
DESK_GATEWAY_KEY = required_env("DESK_GATEWAY_KEY")
DESK_HTTP_TIMEOUT_SECONDS = float(
    os.environ.get("DESK_HTTP_TIMEOUT_SECONDS", "5")
)

mcp = FastMCP("DeskGateway")


def desk_request(method: str, path: str) -> dict[str, Any]:
    """只访问预先写死的路径；调用方不能传入 URL 或认证头。"""
    request = Request(
        f"{DESK_GATEWAY_URL}{path}",
        method=method,
        headers={
            "Accept": "application/json",
            "X-Desk-Key": DESK_GATEWAY_KEY,
        },
    )
    try:
        with urlopen(request, timeout=DESK_HTTP_TIMEOUT_SECONDS) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Desk Gateway HTTP {exc.code}: {body}") from exc
    except (URLError, TimeoutError) as exc:
        raise RuntimeError(f"Desk Gateway unavailable: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise RuntimeError("Desk Gateway returned invalid JSON") from exc

    if not isinstance(payload, dict):
        raise RuntimeError("Desk Gateway returned a non-object response")
    if method == "POST" and not payload.get("ok", False):
        raise RuntimeError(f"Desk Gateway rejected command: {payload}")
    return payload


def status_preflight() -> dict[str, Any]:
    """运动前检查公共策略；ESP32 仍会在真正执行时再次裁决。"""
    status = desk_request("GET", "/api/v1/desk/status")
    if status.get("child_lock"):
        raise RuntimeError("Desk Gateway child lock is enabled")
    if not status.get("control_sources", {}).get("rest"):
        raise RuntimeError("Desk Gateway REST control source is disabled")
    return status


@mcp.tool(name="desk.get_status")
def get_status() -> dict[str, Any]:
    """查询升降桌状态、高度、安全上限、档位和传感器可用性。"""
    return desk_request("GET", "/api/v1/desk/status")


@mcp.tool(name="desk.raise_to_max")
def raise_to_max() -> dict[str, Any]:
    """仅在用户明确说升到最高或升到顶时调用；站立模式不要调用本工具。"""
    status = status_preflight()
    if status.get("status") != "idle":
        raise RuntimeError(f"Desk is not idle: {status.get('status')}")
    if not status.get("height_known") or not status.get("tof_height_known"):
        raise RuntimeError("Desk height sensor is unavailable")
    if status.get("upward_blocked"):
        raise RuntimeError("Upward motion is blocked by the local safety policy")

    height_mm = int(status["height_mm"])
    max_height_mm = int(status["max_height_mm"])
    if height_mm >= max_height_mm:
        return {
            "ok": True,
            "state": "already_at_max",
            "height_mm": height_mm,
            "max_height_mm": max_height_mm,
        }

    desk_request("POST", "/api/v1/desk/up")
    return {
        "ok": True,
        "state": "started",
        "message": "已经开始上升，将由设备在安全上限自动停止",
        "start_height_mm": height_mm,
        "max_height_mm": max_height_mm,
    }


@mcp.tool(name="desk.goto_sit")
def goto_sit() -> dict[str, Any]:
    """用户明确要求坐姿或档位一时，闭环前往 Desk Gateway 档位 1。"""
    status = status_preflight()
    desk_request("POST", "/api/v1/desk/preset/1/goto")
    return {
        "ok": True,
        "state": "started",
        "target": "sit",
        "target_height_mm": status.get("preset1_height_mm"),
    }


@mcp.tool(name="desk.goto_stand")
def goto_stand() -> dict[str, Any]:
    """用户明确要求站姿或档位四时，闭环前往 Desk Gateway 档位 4。"""
    status = status_preflight()
    desk_request("POST", "/api/v1/desk/preset/4/goto")
    return {
        "ok": True,
        "state": "started",
        "target": "stand",
        "target_height_mm": status.get("preset4_height_mm"),
    }


@mcp.tool(name="desk.stop")
def stop() -> dict[str, Any]:
    """用户要求停止、停下或紧急停止时，立即停止升降桌。"""
    desk_request("POST", "/api/v1/desk/stop")
    return {"ok": True, "state": "stopped"}


if __name__ == "__main__":
    mcp.run(transport="stdio")
```

这个示例没有接收任意 URL、REST 路径、HTTP Method 或 Header 的工具参数。工具参数为空，
大模型只能在五个固定动作中选择。

## 7. 启动桥接并注册工具

在运行桥接的 Shell 中设置环境变量：

```bash
cd desk-mcp-bridge
source .venv/bin/activate

export MCP_ENDPOINT='ws://192.168.21.10:8004/mcp_endpoint/mcp/?token=replace-with-agent-token'
export DESK_GATEWAY_URL='http://192.168.21.65'
export DESK_GATEWAY_KEY='replace-with-local-desk-key'
export DESK_HTTP_TIMEOUT_SECONDS='5'

python mcp_pipe.py desk_mcp.py
```

预期日志至少表明：

- 已连接 MCP Endpoint WebSocket；
- `DeskGateway` MCP Server 初始化完成；
- Server 获取到五个工具；
- 连接断开后会自动重连。

返回智控台，打开目标智能体的“编辑功能”，刷新 MCP 接入状态。工具列表中应出现：

```text
desk.get_status
desk.raise_to_max
desk.goto_sit
desk.goto_stand
desk.stop
```

工具没有出现时先处理连接问题，不要通过修改智能体提示词伪造工具能力。

## 8. 配置智能体行为

在目标智能体的角色或系统提示词中补充以下规则：

```text
你可以通过 DeskGateway 工具控制本地升降桌。

1. 用户明确说“升到最高”“升到顶”时，调用 desk.raise_to_max。
2. 用户说“站立模式”“站姿高度”时，调用 desk.goto_stand。
3. 用户说“坐姿”“坐下高度”时，调用 desk.goto_sit。
4. 用户说“停止”“停下桌子”“紧急停止”时，立即调用 desk.stop。
5. “最高”和“站立模式”是两个不同目标，不得混用。
6. “升高一点”“调整一下”等没有明确目标的指令不要执行，先说明当前只支持固定动作。
7. 工具返回 started 只能回复“已经开始”，不能回复“已经到达”。
8. 工具报错时如实说明原因，不得假装动作已经执行。
9. 不得要求用户提供 Desk Gateway 密钥，也不得生成任意 REST 请求。
```

工具说明负责让模型选择正确函数，提示词负责补充产品语义。不要只依赖中文关键词硬匹配，仍需
用不同说法执行语音验收。

## 9. 一次完整调用过程

用户说：

```text
升降桌升高到最高。
```

预期过程：

1. 小智硬件把语音交给本地 Server 的 ASR 和 LLM。
2. LLM 根据工具描述选择 `desk.raise_to_max`，不传入任何参数。
3. MCP Endpoint 把 `tools/call` 转发给 `desk_mcp.py`。
4. 桥接先读取 `/api/v1/desk/status`。
5. 桥接确认 REST 已启用、童锁关闭、高度有效且上升未被阻止。
6. 桥接向 `/api/v1/desk/up` 发送一次认证 POST。
7. Desk Gateway 再次执行来源权限和本机传感器检查，然后开始上升。
8. 桥接返回 `state=started`，小智回复“已经开始上升，将在安全上限自动停止”。
9. ESP32 在运动过程中持续检查 TOF400C 和 TOF050C。
10. 到达 `max_height_mm`、发现障碍或传感器失效时，ESP32 本地停止。

步骤 9、10 不依赖小智 Server、MCP Endpoint 或桥接进程继续在线。

## 10. 分层验收

不要直接从语音测试开始。按下面顺序逐层验收，某一层失败时停止，不进入下一层。

### 10.1 REST 静态检查

- [ ] `GET /api/v1/desk/status` 使用错误密钥返回 `401`。
- [ ] 正确密钥能读取真实高度和 `max_height_mm`。
- [ ] `height_sim` 为 `false`。
- [ ] `control_sources.rest` 为 `true`。
- [ ] `POST /api/v1/desk/stop` 返回 `ESP_OK`。

### 10.2 Desk Gateway 真桌检查

测试人员必须站在桌旁并随时准备断电或发送 STOP：

- [ ] 从接近最高点的位置开始短行程测试 `/desk/up`。
- [ ] 到达 `max_height_mm` 后设备自动停止。
- [ ] TOF400C 不可用时上升命令被拒绝或运动立即停止。
- [ ] 低于 `800 mm` 时触发右侧障碍条件，设备停止。
- [ ] 档位 1 和档位 4 分别到达当前配置高度并停止。
- [ ] 童锁、REST 来源禁用和原面板优先状态能阻止冲突动作。

### 10.3 MCP 桥接检查

- [ ] health 接口返回 `status=success`。
- [ ] 智控台能看到五个 Desk 工具。
- [ ] `desk.get_status` 返回的数据与直接 REST 一致。
- [ ] 错误 Desk Key 会让工具明确报错，不会返回成功。
- [ ] Desk Gateway 离线时工具在 5 秒内失败。
- [ ] 重启 MCP Endpoint 或桥接后能够重新注册工具。

### 10.4 语音检查

至少测试这些表达：

| 语音 | 预期工具 |
| --- | --- |
| “升降桌升高到最高” | `desk.raise_to_max` |
| “把桌子升到顶” | `desk.raise_to_max` |
| “切换到站立模式” | `desk.goto_stand` |
| “调整到坐姿” | `desk.goto_sit` |
| “停下桌子” | `desk.stop` |
| “桌子现在多高” | `desk.get_status` |
| “升高一点” | 不执行，说明只支持固定动作 |

还要验证：

- [ ] 同一句指令连续说两次不会绕过设备状态检查。
- [ ] 桌子运动时说“停止”能立即生效。
- [ ] 工具返回 `started` 时，小智没有回复“已经到达”。
- [ ] 工具失败时，小智没有回复成功。
- [ ] 关闭桥接、MCP Endpoint 或小智 Server 后，已经开始的上升仍由 ESP32 在安全上限停止。

## 11. 故障排查

### 11.1 智控台没有 Desk 工具

依次检查：

1. `mcp-endpoint-server` 容器是否监听 `8004`。
2. health 地址是否使用 Mac 局域网 IP，而不是容器 `172.x.x.x` 地址。
3. `server.mcp_endpoint` 是否填写 health 地址，而不是工具 WebSocket 地址。
4. `MCP_ENDPOINT` 是否来自同一个目标智能体。
5. `mcp_pipe.py` 是否仍在运行并已连接。
6. macOS 防火墙是否允许 `8004`。

### 11.2 工具存在，但 Desk Gateway 返回 `401`

`DESK_GATEWAY_KEY` 必须与 Desk Gateway 当前 Web 登录密码一致。修改环境变量后重启桥接，
不要把密钥复制到智能体提示词。

### 11.3 `desk.raise_to_max` 报高度不可用

先查看 `/api/v1/desk/status` 中：

```text
height_known
tof_height_known
tof_height_mm
upward_blocked
driver
```

这是设备安全前提失败，不应在桥接中跳过。恢复 ToF 传感器、I2C 和固件配置后重新验收。

### 11.4 小智把“站立”调用成“最高”

检查两个工具的 docstring 和智能体规则是否完整；在真实 Provider 上分别测试“最高”“站立”
的同义表达。如果 Provider 对句点工具名兼容性不好，统一换成下划线命名并重新刷新工具，
不要只改提示词中的名字。

### 11.5 MCP 回复成功但桌子没有运动

检查工具返回的是 REST 原始成功还是桥接自己构造的成功。本文示例只有 REST 返回
`{"ok":true}` 后才返回 `state=started`。同时查看 Desk Gateway 串口日志中的来源授权、童锁、
原面板优先和 ToF 拦截原因。

## 12. 运行和安全要求

- `MCP_ENDPOINT`、`DESK_GATEWAY_KEY` 和模型 API Key 使用环境变量或本机 Secret 管理，不进入
  Git、提示词或日志。
- Desk Gateway REST 只在可信局域网开放，不通过路由器端口映射暴露到公网。
- MCP Endpoint 的 `8004` 只向需要的局域网或反向代理开放，并保护 health key 和 token。
- 桥接进程只允许固定路径，不提供 `request(url, method, body)` 一类通用工具。
- 第一版不提供任意高度参数，避免单位、范围、标定方式和语音识别错误直接驱动桌子。
- `desk.stop` 保持无参数，不能因为状态查询失败而拒绝发送 STOP。
- 自动启动应在手工验收完成后配置；启动配置只引用本地权限受控的环境文件，不把密钥直接
  写进可公开的 service 文件。
- 升级小智固件、Server、MCP Endpoint、桥接依赖或 Desk Gateway 固件后，重新执行第 10 节。

## 13. 不选择设备端直连 REST 的原因

`xiaozhi-esp32 v2.4.2` 支持在各 Board 的 `InitializeTools()` 中注册设备侧 MCP 工具，理论上
也可以让小智 ESP32 自己访问 Desk Gateway。但本项目第一版不采用该方案：

- JC3636W518C 和 Xmini-C3 属于不同 Board，需要维护两份板级接入。
- Desk Gateway 地址、密钥和网络错误处理会进入小智固件配置。
- 每次升级官方固件都要重新合并、编译和烧录。
- 小智硬件和 Desk Gateway 可能不在相同网络条件下，排错边界更复杂。
- 外部桥接可以独立停止、升级和审计，不影响语音终端本身。

如果后续要求断开 Mac 后仍能控制，再单独评估设备端工具；这属于另一套实现和验收范围。

## 14. 参考资料

- [小智 ESP32 v2.4.2](https://github.com/78/xiaozhi-esp32/releases/tag/v2.4.2)
- [小智 MCP IoT Control Usage](https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md)
- [小智 MCP Protocol](https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-protocol.md)
- [xiaozhi-esp32-server v0.9.6](https://github.com/xinnan-tech/xiaozhi-esp32-server/releases/tag/v0.9.6)
- [MCP 接入点部署使用指南](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/mcp-endpoint-enable.md)
- [MCP 接入点使用指南](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/mcp-endpoint-integration.md)
- [MCP Endpoint Server](https://github.com/xinnan-tech/mcp-endpoint-server)
- [小智 MCP 示例](https://github.com/78/mcp-calculator)
- [Desk Gateway 固件说明](../firmware/desk-gateway/README.md)
- [键盘、旋钮与语音控制](./keyboard-voice-control.md)
