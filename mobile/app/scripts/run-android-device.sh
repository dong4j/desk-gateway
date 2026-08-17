#!/usr/bin/env bash
# Build and install the Expo Android Development Build on a connected phone.
#
# Why a wrapper: `expo run:android --device` still assumes ANDROID_HOME, an
# authorized `adb` device, and USB Metro reverse. Fail those checks before
# paying for a Gradle compile. BLE uses a native module, so this path must
# never fall back to Expo Go.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEFAULT_SDK="${HOME}/Library/Android/sdk"
METRO_PORT="${DESK_METRO_PORT:-8081}"
DEVICE_SERIAL="${1:-}"

die() {
  echo "error: $*" >&2
  exit 1
}

resolve_sdk() {
  if [[ -n "${ANDROID_HOME:-}" ]]; then
    printf '%s\n' "${ANDROID_HOME}"
    return
  fi
  if [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then
    printf '%s\n' "${ANDROID_SDK_ROOT}"
    return
  fi
  if [[ -d "${DEFAULT_SDK}" ]]; then
    printf '%s\n' "${DEFAULT_SDK}"
    return
  fi
  die "Android SDK not found. Install Android Studio and set ANDROID_HOME (expected ${DEFAULT_SDK})."
}

find_adb() {
  local sdk="$1"
  if command -v adb >/dev/null 2>&1; then
    command -v adb
    return
  fi
  if [[ -x "${sdk}/platform-tools/adb" ]]; then
    printf '%s\n' "${sdk}/platform-tools/adb"
    return
  fi
  die "adb not found. Install Android SDK Platform-Tools and add ${sdk}/platform-tools to PATH."
}

# Pick the first USB/Wi-Fi device whose adb state is "device".
# unauthorized / offline phones are not silently selected.
detect_android_device() {
  local adb_bin="$1"
  local serial=""
  local state=""
  while read -r serial state _; do
    [[ "${serial}" == "List" || -z "${serial}" ]] && continue
    if [[ "${state}" == "device" ]]; then
      printf '%s\n' "${serial}"
      return 0
    fi
  done < <("${adb_bin}" devices)
  return 1
}

ANDROID_HOME="$(resolve_sdk)"
export ANDROID_HOME
export ANDROID_SDK_ROOT="${ANDROID_HOME}"
ADB="$(find_adb "${ANDROID_HOME}")"

if [[ -z "${DEVICE_SERIAL}" ]]; then
  if ! DEVICE_SERIAL="$(detect_android_device "${ADB}")"; then
    echo "No authorized Android device was found. Connect a phone, enable USB debugging, and confirm the RSA prompt." >&2
    "${ADB}" devices -l >&2 || true
    exit 1
  fi
fi

DEVICE_STATE="$("${ADB}" -s "${DEVICE_SERIAL}" get-state 2>/dev/null || true)"
if [[ "${DEVICE_STATE}" != "device" ]]; then
  die "device ${DEVICE_SERIAL} is not ready (adb state=${DEVICE_STATE:-missing}). Unlock the phone and allow USB debugging."
fi

echo "Using Android SDK ${ANDROID_HOME}"
echo "Installing DeskGateway on ${DEVICE_SERIAL}..."

(
  cd "${APP_DIR}"
  npx expo run:android --device "${DEVICE_SERIAL}" --no-bundler
)

# USB installs often cannot reach Metro on the Mac until this reverse is set.
# Harmless on wireless debugging if the phone can already route to the host.
echo "Forwarding Metro tcp:${METRO_PORT} to the device..."
"${ADB}" -s "${DEVICE_SERIAL}" reverse "tcp:${METRO_PORT}" "tcp:${METRO_PORT}" >/dev/null || true

echo "DeskGateway is installed. Keep 'npm start' running for Metro."
echo "If the app cannot load the bundle over USB, re-run: ${ADB} -s ${DEVICE_SERIAL} reverse tcp:${METRO_PORT} tcp:${METRO_PORT}"
