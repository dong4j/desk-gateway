#!/bin/zsh
# 启动官方 mcp_pipe.py 与本仓库的 DeskGateway MCP Server。
# 凭据优先从环境变量读取；launchd 场景可改用 macOS Keychain service。

set -euo pipefail

task_script_dir="${0:A:h}"
task_integration_dir="${task_script_dir:h}"

if [[ -z "${MCP_ENDPOINT:-}" && -n "${MCP_ENDPOINT_KEYCHAIN_SERVICE:-}" ]]; then
  MCP_ENDPOINT="$(security find-generic-password \
    -s "${MCP_ENDPOINT_KEYCHAIN_SERVICE}" -w)"
  export MCP_ENDPOINT
fi

if [[ -z "${DESK_GATEWAY_KEY:-}" && -n "${DESK_GATEWAY_KEYCHAIN_SERVICE:-}" ]]; then
  DESK_GATEWAY_KEY="$(security find-generic-password \
    -s "${DESK_GATEWAY_KEYCHAIN_SERVICE}" -w)"
  export DESK_GATEWAY_KEY
fi

: "${MCP_ENDPOINT:?MCP_ENDPOINT or MCP_ENDPOINT_KEYCHAIN_SERVICE is required}"
: "${DESK_GATEWAY_URL:?DESK_GATEWAY_URL is required}"
: "${DESK_GATEWAY_KEY:?DESK_GATEWAY_KEY or DESK_GATEWAY_KEYCHAIN_SERVICE is required}"
: "${MCP_PIPE_DIR:?MCP_PIPE_DIR must point to the official 78/mcp-calculator checkout}"

if [[ "${MCP_ENDPOINT}" != ws://* && "${MCP_ENDPOINT}" != wss://* ]]; then
  print -u2 "MCP_ENDPOINT must use ws:// or wss://"
  exit 2
fi

task_python="${MCP_PYTHON:-${MCP_PIPE_DIR}/.venv/bin/python}"
task_pipe="${MCP_PIPE_DIR}/mcp_pipe.py"
task_server="${task_integration_dir}/desk_mcp.py"

if [[ ! -x "${task_python}" ]]; then
  print -u2 "MCP Python is not executable: ${task_python}"
  exit 2
fi
if ! "${task_python}" -c \
  'import sys; raise SystemExit(sys.version_info < (3, 10))'; then
  print -u2 "MCP Python 3.10 or newer is required: ${task_python}"
  exit 2
fi
if [[ ! -f "${task_pipe}" ]]; then
  print -u2 "Official mcp_pipe.py not found: ${task_pipe}"
  exit 2
fi

exec env PYTHONUNBUFFERED=1 \
  "${task_python}" "${task_pipe}" "${task_server}"
