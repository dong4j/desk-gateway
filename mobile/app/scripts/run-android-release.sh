#!/usr/bin/env bash
# Build a local Android Release APK with the JS already inside the package,
# then install it on an authorized phone.
#
# Why this is not run-android-device.sh:
# - Debug Development Build loads JS from Metro and needs `npm start`.
# - Release embeds the bundle so the phone can run after the Mac is unplugged.
# - The generated Gradle release type still uses the debug keystore. That is
#   enough for a personal USB install, not Google Play / EAS, and it does not
#   close V1-12.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ANDROID_DIR="${APP_DIR}/android"
DEFAULT_SDK="${HOME}/Library/Android/sdk"
DEVICE_SERIAL="${1:-}"
APK_PATH="${ANDROID_DIR}/app/build/outputs/apk/release/app-release.apk"

# React Native skips the embed step when this is set. Release must ship JS
# inside the APK; otherwise the installed app still depends on Metro.
unset SKIP_BUNDLING

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

apk_has_js_bundle() {
  local apk="$1"
  local listing

  # Do not pipe unzip into grep -q. grep exits on the first match and closes
  # the pipe; unzip then dies with SIGPIPE 141. With `set -o pipefail` that
  # looks like a missing bundle even when assets/index.android.bundle is there.
  listing="$(unzip -Z -1 "${apk}")" || return 1
  grep -Eq '(^|/)(index\.android\.bundle|main\.jsbundle)$' <<<"${listing}"
}

wait_for_android_device() {
  local timeout_s="${1:-60}"
  local waited=0
  local state=""

  while (( waited < timeout_s )); do
    state="$("${ADB}" -s "${DEVICE_SERIAL}" get-state 2>/dev/null || true)"
    if [[ "${state}" == "device" ]]; then
      return 0
    fi
    sleep 2
    waited=$((waited + 2))
  done
  return 1
}

# Xiaomi/MIUI often dies during `adb install` sync ACK ("failed to read copy
# response: EOF") after the APK bytes already landed. Push to a temp path,
# wait for adbd to come back, then install with `pm` over a new shell.
install_release_apk() {
  local remote_apk="/data/local/tmp/desk-gateway-release.apk"
  local local_size
  local remote_size

  local_size="$(stat -f %z "${APK_PATH}")"

  echo "Pushing ${local_size} bytes to ${DEVICE_SERIAL}:${remote_apk}..."
  set +e
  "${ADB}" -s "${DEVICE_SERIAL}" push "${APK_PATH}" "${remote_apk}"
  set -e

  if ! wait_for_android_device 60; then
    echo "device ${DEVICE_SERIAL} dropped after the APK push (adb EOF / offline)." >&2
    echo "Unlock the phone and keep the screen on. Prefer a USB port on the Mac, not a hub." >&2
    echo "Then: adb kill-server && adb start-server && adb devices -l" >&2
    "${ADB}" devices -l >&2 || true
    exit 1
  fi

  remote_size="$("${ADB}" -s "${DEVICE_SERIAL}" shell stat -c %s "${remote_apk}" 2>/dev/null | tr -d '\r')"
  if [[ "${remote_size}" != "${local_size}" ]]; then
    echo "Retrying APK push; remote size is '${remote_size:-missing}', expected ${local_size}."
    "${ADB}" -s "${DEVICE_SERIAL}" push "${APK_PATH}" "${remote_apk}"
    wait_for_android_device 60 || die "device ${DEVICE_SERIAL} dropped during the retry push."
    remote_size="$("${ADB}" -s "${DEVICE_SERIAL}" shell stat -c %s "${remote_apk}" 2>/dev/null | tr -d '\r')"
  fi
  if [[ "${remote_size}" != "${local_size}" ]]; then
    die "APK on the phone is incomplete (${remote_size:-missing} / ${local_size})."
  fi

  echo "Installing from ${remote_apk}..."
  "${ADB}" -s "${DEVICE_SERIAL}" shell pm install -r "${remote_apk}"
  "${ADB}" -s "${DEVICE_SERIAL}" shell rm -f "${remote_apk}" >/dev/null
}

ANDROID_HOME="$(resolve_sdk)"
export ANDROID_HOME
export ANDROID_SDK_ROOT="${ANDROID_HOME}"
ADB="$(find_adb "${ANDROID_HOME}")"

if [[ ! -x "${ANDROID_DIR}/gradlew" ]]; then
  die "Missing generated Android project. Run: npx expo prebuild --platform android"
fi

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

# Gradle's Expo/RN bundle task does not inherit the interactive shell PATH.
export NODE_BINARY="$(command -v node)"

echo "Using Android SDK ${ANDROID_HOME}"
echo "Building DeskGateway Release APK..."
(
  cd "${ANDROID_DIR}"
  ./gradlew assembleRelease
)

if [[ ! -f "${APK_PATH}" ]]; then
  die "Build succeeded but the APK was not found: ${APK_PATH}"
fi
if ! apk_has_js_bundle "${APK_PATH}"; then
  echo "Release APK is missing index.android.bundle / main.jsbundle: ${APK_PATH}" >&2
  echo "Without that file the phone still needs Metro. Do not install this build." >&2
  exit 1
fi

echo "Installing DeskGateway Release on ${DEVICE_SERIAL}..."
install_release_apk
"${ADB}" -s "${DEVICE_SERIAL}" shell am start -n com.dong4j.deskgateway/.MainActivity >/dev/null

echo "DeskGateway Release is installed and launched. Metro is not required."
echo "APK: ${APK_PATH}"
