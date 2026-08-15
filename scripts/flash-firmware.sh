#!/usr/bin/env bash
# Desk Gateway 主固件完整烧录脚本。
#
# 脚本固定使用项目要求的 ESP-IDF v6.0.2，并执行 full flash，确保
# bootloader、分区表、应用固件和 audio.bin 一并写入，同时保留 NVS。
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
PROJECT_DIR="${REPO_ROOT}/firmware/desk-gateway"
SDKCONFIG_FILE="${PROJECT_DIR}/sdkconfig"
PARTITION_FILE="${PROJECT_DIR}/partitions.csv"
IDF_ROOT="/Users/dong4j/.espressif/v6.0.2/esp-idf"
IDF_VENV="/Users/dong4j/.espressif/tools/python/v6.0.2/venv"

# 参数保持简单且显式，避免多串口环境下自动选择错误设备。
usage() {
    cat <<'EOF'
Usage: ./scripts/flash-firmware.sh <serial-port> [--monitor]

Examples:
  ./scripts/flash-firmware.sh /dev/cu.usbmodem1101
  ./scripts/flash-firmware.sh /dev/cu.usbmodem1101 --monitor
EOF
}

# 统一错误出口，确保失败发生在任何烧录动作之前。
die() {
    echo "error: $*" >&2
    exit 1
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if (( $# < 1 || $# > 2 )); then
    usage >&2
    exit 64
fi

SERIAL_PORT="$1"
MONITOR=false
if (( $# == 2 )); then
    [[ "$2" == "--monitor" ]] || die "unknown option '$2'"
    MONITOR=true
fi

[[ -c "${SERIAL_PORT}" ]] || die "serial port is not a character device: ${SERIAL_PORT}"
[[ -f "${IDF_ROOT}/export.sh" ]] || die "ESP-IDF v6.0.2 not found at ${IDF_ROOT}"
[[ -d "${IDF_VENV}" ]] || die "ESP-IDF Python venv not found at ${IDF_VENV}"

export IDF_PYTHON_ENV_PATH="${IDF_VENV}"
export IDF_PYTHON_CHECK_CONSTRAINTS=no
# shellcheck disable=SC1091 -- 固定绝对路径由上面的存在性检查保护。
source "${IDF_ROOT}/export.sh" >/dev/null

IDF_VERSION="$(idf.py --version)"
[[ "${IDF_VERSION}" == "ESP-IDF v6.0.2" ]] || \
    die "expected ESP-IDF v6.0.2, got '${IDF_VERSION}'"

cd "${PROJECT_DIR}"

# 首次使用时从受版本控制的 defaults 初始化配置；已有配置不自动覆盖，
# 避免清除开发者通过 menuconfig 保存的其他本地选项。
if [[ ! -f "${SDKCONFIG_FILE}" ]]; then
    idf.py set-target esp32s3
fi

# audio.bin 只有在自定义分区表和足够长的 SPIFFS 文件名配置同时生效时
# 才能正确生成并加入 flash 目标，因此在连接设备前完成门禁检查。
grep -qx 'CONFIG_PARTITION_TABLE_CUSTOM=y' "${SDKCONFIG_FILE}" || \
    die "sdkconfig does not enable the custom partition table; run 'idf.py set-target esp32s3'"
grep -qx 'CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"' "${SDKCONFIG_FILE}" || \
    die "sdkconfig does not use partitions.csv; run 'idf.py set-target esp32s3'"
grep -qx 'CONFIG_SPIFFS_OBJ_NAME_LEN=64' "${SDKCONFIG_FILE}" || \
    die "sdkconfig does not set CONFIG_SPIFFS_OBJ_NAME_LEN=64; run 'idf.py set-target esp32s3'"
grep -Eq '^audio,[[:space:]]+data,[[:space:]]+spiffs,[[:space:]]+0x310000,[[:space:]]+0x400000,' \
    "${PARTITION_FILE}" || die "audio SPIFFS partition is missing or has an unexpected layout"

echo "Using ${IDF_VERSION}"
echo "Flashing Desk Gateway to ${SERIAL_PORT} (NVS will be preserved)"
idf.py -p "${SERIAL_PORT}" flash

if [[ "${MONITOR}" == true ]]; then
    idf.py -p "${SERIAL_PORT}" monitor
fi
