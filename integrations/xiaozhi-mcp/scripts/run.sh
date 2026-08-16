#!/bin/zsh
# 启动官方 mcp_pipe.py 与本仓库的 DeskGateway MCP Server。
# 默认读取集成目录下的 .env；环境变量和 macOS Keychain 仍可用于高级场景。

set -euo pipefail

task_script_dir="${0:A:h}"
task_integration_dir="${task_script_dir:h}"
task_config_file="${DESK_MCP_CONFIG:-${task_integration_dir}/.env}"

# .env 是用户本机的可信 Shell 配置。set -a 保证其中的变量会传给
# mcp_pipe.py 及其启动的 desk_mcp.py 子进程。
if [[ -f "${task_config_file}" ]]; then
  set -a
  source "${task_config_file}"
  set +a
fi

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

: "${MCP_ENDPOINT:?set MCP_ENDPOINT in ${task_config_file}}"
: "${DESK_GATEWAY_URL:?set DESK_GATEWAY_URL in ${task_config_file}}"
: "${DESK_GATEWAY_KEY:?set DESK_GATEWAY_KEY in ${task_config_file}}"
: "${MCP_PIPE_DIR:?set MCP_PIPE_DIR in ${task_config_file}}"

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
