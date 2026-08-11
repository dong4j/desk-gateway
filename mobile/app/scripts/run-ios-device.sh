#!/usr/bin/env bash
# Build the Expo development client with the iOS 26 SDK, then install it on an
# iOS 27 device with Xcode 27. Expo SDK 57 has not yet adopted the mandatory
# iOS 27 UIScene lifecycle, so building directly with the iOS 27 SDK crashes
# before React Native starts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
IOS_DIR="${APP_DIR}/ios"
XCODE_26_APP="${DESK_XCODE_26_APP:-/Applications/Xcode.app}"
XCODE_27_APP="${DESK_XCODE_27_APP:-/Applications/Xcode-beta.app}"
DERIVED_DATA="${TMPDIR:-/tmp}/desk-gateway-xcode26"
DEVICE_ID="${1:-}"

require_xcode() {
  local app_path="$1"
  local expected_version="$2"

  if [[ ! -x "${app_path}/Contents/Developer/usr/bin/xcodebuild" ]]; then
    echo "Missing ${expected_version}: ${app_path}" >&2
    exit 1
  fi
}

detect_ios_device() {
  local devices_json
  devices_json="$(mktemp "${TMPDIR:-/tmp}/desk-gateway-devices.XXXXXX.json")"

  DEVELOPER_DIR="${XCODE_27_APP}/Contents/Developer" \
    xcrun devicectl list devices --json-output "${devices_json}" >/dev/null

  # Node.js is already an Expo prerequisite. Use it here so the script does not
  # add jq or another machine-local dependency just to select a connected iPhone.
  node -e '
    const data = require(process.argv[1]);
    const device = data.result.devices.find((candidate) =>
      candidate.properties?.hardware?.platform === "iOS" &&
      candidate.connectionProperties?.tunnelState === "connected"
    );
    if (!device) process.exit(1);
    process.stdout.write(device.identifier);
  ' "${devices_json}"

  rm -f "${devices_json}"
}

require_xcode "${XCODE_26_APP}" "Xcode 26"
require_xcode "${XCODE_27_APP}" "Xcode 27"

if [[ ! -d "${IOS_DIR}/DeskGateway.xcworkspace" ]]; then
  echo "Missing generated iOS workspace. Run: npx expo prebuild --platform ios" >&2
  exit 1
fi

if [[ -z "${DEVICE_ID}" ]]; then
  if ! DEVICE_ID="$(detect_ios_device)"; then
    echo "No connected and unlocked iPhone was found." >&2
    exit 1
  fi
fi

echo "Synchronizing CocoaPods for installed native modules..."
(
  cd "${IOS_DIR}"
  DEVELOPER_DIR="${XCODE_26_APP}/Contents/Developer" pod install
)

echo "Building DeskGateway with Xcode 26 / iOS 26 SDK..."
DEVELOPER_DIR="${XCODE_26_APP}/Contents/Developer" \
  xcodebuild \
    -workspace "${IOS_DIR}/DeskGateway.xcworkspace" \
    -scheme DeskGateway \
    -configuration Debug \
    -destination 'generic/platform=iOS' \
    -derivedDataPath "${DERIVED_DATA}" \
    -allowProvisioningUpdates \
    COCOAPODS_PARALLEL_CODE_SIGN=true \
    COMPILER_INDEX_STORE_ENABLE=NO \
    -quiet \
    build

APP_PATH="${DERIVED_DATA}/Build/Products/Debug-iphoneos/DeskGateway.app"
if [[ ! -d "${APP_PATH}" ]]; then
  echo "Build succeeded but the app bundle was not found: ${APP_PATH}" >&2
  exit 1
fi

echo "Installing DeskGateway with Xcode 27 device support..."
DEVELOPER_DIR="${XCODE_27_APP}/Contents/Developer" \
  xcrun devicectl device install app --device "${DEVICE_ID}" "${APP_PATH}"

DEVELOPER_DIR="${XCODE_27_APP}/Contents/Developer" \
  xcrun devicectl device process launch \
    --device "${DEVICE_ID}" \
    --terminate-existing \
    com.dong4j.deskgateway

echo "DeskGateway is installed and launched. Keep 'npm start' running for Metro."
