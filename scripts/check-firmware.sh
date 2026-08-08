#!/usr/bin/env bash
# Desk Gateway 主固件可重复构建检查。
#
# 使用独立临时目录是为了避开本地 build/ 中可能残留的 ESP-IDF Python
# 解释器路径，同时保证检查不会把 CMake 缓存写进源码目录。
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
PROJECT_DIR="${REPO_ROOT}/firmware/desk-gateway"
CHECK_BUILD_DIR="$(mktemp -d /tmp/desk-gateway-check.XXXXXX)"

cleanup() {
    # 仅删除本脚本刚创建且命名受控的临时目录，避免变量异常扩大清理范围。
    case "${CHECK_BUILD_DIR}" in
        /tmp/desk-gateway-check.*|/private/tmp/desk-gateway-check.*)
            rm -rf -- "${CHECK_BUILD_DIR}"
            ;;
    esac
}
trap cleanup EXIT

if command -v idf.py >/dev/null 2>&1; then
    IDF_CMD=(idf.py)
elif [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/tools/idf.py" ]]; then
    # Espressif Install Manager 可把 idf.py 暴露成 zsh function；子 Bash
    # 继承不到 function，但会继承 IDF_PATH 与已激活的 Python 环境。
    IDF_CMD=(python "${IDF_PATH}/tools/idf.py")
else
    echo "error: idf.py not found; activate ESP-IDF first" >&2
    exit 127
fi

echo "Building firmware/desk-gateway in ${CHECK_BUILD_DIR}"
(
    cd "${PROJECT_DIR}"
    "${IDF_CMD[@]}" \
        -B "${CHECK_BUILD_DIR}" \
        -D "SDKCONFIG=${CHECK_BUILD_DIR}/sdkconfig" \
        -D "IDF_TARGET=esp32s3" \
        build
)
