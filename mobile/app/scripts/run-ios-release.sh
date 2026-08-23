#!/usr/bin/env bash
# Build a local iOS Release app with the JS already inside main.jsbundle, then
# install it on an iOS 27 phone.
#
# Why this is not run-ios-device.sh:
# - Debug Development Build loads JS from Metro and needs `npm start`.
# - Release embeds the bundle so the phone can run after the Mac is unplugged.
# - Signing is still Apple Development for a registered personal device.
#   This is not TestFlight / App Store Distribution, and it does not close V1-12.
#
# Why two Xcodes: Expo SDK 57 has not adopted the iOS 27 UIScene lifecycle.
# Building with the iOS 27 SDK can install the icon, then exit before React Native
# starts. Compile with Xcode 26 / iOS 26 SDK; install with Xcode 27 device support.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
IOS_DIR="${APP_DIR}/ios"
XCODE_26_APP="${DESK_XCODE_26_APP:-/Applications/Xcode.app}"
XCODE_27_APP="${DESK_XCODE_27_APP:-/Applications/Xcode-beta.app}"
# Keep Release artifacts away from the Debug Derived Data used by ios:device.
DERIVED_DATA="${TMPDIR:-/tmp}/desk-gateway-xcode26-release"
DEVICE_ID="${1:-}"

# The React Native Xcode packager skips embed when this is set. Release must emit
# main.jsbundle; otherwise the installed app still depends on Metro.
unset SKIP_BUNDLING

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

# xcodebuild's "Bundle React Native code and images" phase does not inherit the
# interactive shell PATH. Point it at the same node that runs Expo.
export NODE_BINARY="$(command -v node)"

echo "Synchronizing CocoaPods for installed native modules..."
(
  cd "${IOS_DIR}"
  DEVELOPER_DIR="${XCODE_26_APP}/Contents/Developer" pod install
)

echo "Building DeskGateway Release with Xcode 26 / iOS 26 SDK..."
# Leave the log visible: the JS embed step is the usual Release-only failure.
DEVELOPER_DIR="${XCODE_26_APP}/Contents/Developer" \
  xcodebuild \
    -workspace "${IOS_DIR}/DeskGateway.xcworkspace" \
    -scheme DeskGateway \
    -configuration Release \
    -destination 'generic/platform=iOS' \
    -derivedDataPath "${DERIVED_DATA}" \
    -allowProvisioningUpdates \
    COCOAPODS_PARALLEL_CODE_SIGN=true \
    COMPILER_INDEX_STORE_ENABLE=NO \
    build

APP_PATH="${DERIVED_DATA}/Build/Products/Release-iphoneos/DeskGateway.app"
BUNDLE_PATH="${APP_PATH}/main.jsbundle"
if [[ ! -d "${APP_PATH}" ]]; then
  echo "Build succeeded but the app bundle was not found: ${APP_PATH}" >&2
  exit 1
fi
if [[ ! -f "${BUNDLE_PATH}" ]]; then
  echo "Release app is missing main.jsbundle at ${BUNDLE_PATH}." >&2
  echo "Without that file the phone still needs Metro. Do not install this build." >&2
  exit 1
fi

echo "Installing DeskGateway Release with Xcode 27 device support..."
DEVELOPER_DIR="${XCODE_27_APP}/Contents/Developer" \
  xcrun devicectl device install app --device "${DEVICE_ID}" "${APP_PATH}"

DEVELOPER_DIR="${XCODE_27_APP}/Contents/Developer" \
  xcrun devicectl device process launch \
    --device "${DEVICE_ID}" \
    --terminate-existing \
    com.dong4j.deskgateway

echo "DeskGateway Release is installed and launched. Metro is not required."
