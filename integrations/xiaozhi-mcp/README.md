# XiaoZhi AI MCP bridge

**Language:** English · [简体中文](README.zh-CN.md)

This directory connects a XiaoZhi agent MCP endpoint to Desk Gateway REST. XiaoZhi hardware keeps using the official firmware. Desk Gateway does not need a public URL and does not reimplement motion control.

```text
XiaoZhi hardware → XiaoZhi cloud agent → MCP endpoint → local mcp_pipe.py
                                                        → desk_mcp.py → Desk Gateway REST
```

The bridge exposes exactly five tools:

| MCP tool | REST request | Constraint |
|---|---|---|
| `desk.get_status` | `GET /api/v1/desk/status` | Read only |
| `desk.raise_to_max` | `POST /api/v1/desk/raise-to-max` | Needs real ToF and a bounded driver action |
| `desk.goto_sit` | `POST /api/v1/desk/preset/1/goto` | Fixed sit preset |
| `desk.goto_stand` | `POST /api/v1/desk/preset/4/goto` | Fixed stand preset |
| `desk.stop` | `POST /api/v1/desk/stop` | Not blocked by normal motion pre-checks |

Tools do not accept a URL, HTTP method, header, or arbitrary target height.

Parent index: [`../README.md`](../README.md).

## 1. Desk Gateway prerequisites

The Mac must reach Desk Gateway on the LAN. Before enabling motion tools, confirm:

- `height_sim=false`
- `height_known=true`
- `tof_height_known=true`
- `raise_to_max_supported=true`
- `child_lock=false`
- `upward_blocked=false`
- `control_sources.rest=true`

## 2. Install the official MCP pipe

The bridge reuses the official XiaoZhi sample repo. This tree does not copy the WebSocket protocol:

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

Checked against official sample commit `c537f71d61fd73b47d6c8955b5df6d3721acf4e4` on 2026-08-16. Record the commit you actually use. Re-run this directory’s tests and tool-registration check after upgrading.

Recent `mcp` packages need Python 3.10+. macOS `/usr/bin/python3` may still be 3.9. Do not create the venv with that. The commands above use 3.12 as the example.

## 3. Get the MCP endpoint

In the XiaoZhi console, open the agent used by JC3636W518C → configure role / edit functions, and copy the full MCP WebSocket URL. It contains the agent token. Keep it in local `.env` only. Do not put it in prompts, logs, or git.

## 4. Local config and start

```bash
cp integrations/xiaozhi-mcp/.env.example \
  integrations/xiaozhi-mcp/.env
chmod 600 integrations/xiaozhi-mcp/.env
```

Edit `integrations/xiaozhi-mcp/.env` and fill `MCP_ENDPOINT`, `MCP_PIPE_DIR`, `MCP_PYTHON`, `DESK_GATEWAY_URL`, and `DESK_GATEWAY_KEY`. Values with `&`, `?`, or spaces must keep the single quotes from the template.

`.env` is gitignored at the repo root. Confirm:

```bash
git check-ignore integrations/xiaozhi-mcp/.env
```

Load config and ping Desk Gateway:

```bash
set -a
source integrations/xiaozhi-mcp/.env
set +a

curl --fail --silent --show-error \
  -H "X-Desk-Key: ${DESK_GATEWAY_KEY}" \
  "${DESK_GATEWAY_URL}/api/v1/desk/status" | jq
```

Start without sourcing again; the script reads `.env` in this directory:

```bash
./integrations/xiaozhi-mcp/scripts/run.sh
```

Expect a successful WebSocket connect and MCP server start. Refresh the XiaoZhi agent function list. The five `desk.*` tools above should appear.

## 5. launchd (keep it running)

```bash
cp integrations/xiaozhi-mcp/launchd/com.dong4j.desk-mcp.plist.example \
  ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
```

Replace:

- `__PROJECT_ROOT__`: absolute path to this repo
- `__HOME__`: absolute home directory

`launchd` starts the same `run.sh`, which reads `.env`. Do not duplicate the endpoint or key in the plist.

```bash
plutil -lint ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
launchctl bootstrap gui/$(id -u) \
  ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
launchctl kickstart -k gui/$(id -u)/com.dong4j.desk-mcp
```

Status and logs:

```bash
launchctl print gui/$(id -u)/com.dong4j.desk-mcp
tail -f ~/Library/Logs/desk-mcp.error.log
```

Unload:

```bash
launchctl bootout gui/$(id -u) \
  ~/Library/LaunchAgents/com.dong4j.desk-mcp.plist
```

## 6. Local tests

Mock tests do not connect a real desk and do not move it:

```bash
python3 -m unittest discover \
  -s integrations/xiaozhi-mcp/tests \
  -p 'test_*.py' -v
```

## 7. Layered acceptance

1. Call `desk.get_status` and compare with raw REST.
2. Wrong Desk Key must fail clearly and must not return success.
3. Unplug Desk Gateway; the tool must fail within the configured timeout.
4. Call `desk.stop` and confirm the fixed STOP path.
5. At the desk, near the ceiling, ready to cut power: test `desk.raise_to_max`.
6. Then real phrases: sit, stand, raise to max, stop.

`state=started` means the device accepted the action. Do not announce arrival. Final stop is the gateway’s local ToF safety path. It does not need the MCP socket to stay up.
