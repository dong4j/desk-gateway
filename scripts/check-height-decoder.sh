#!/usr/bin/env bash
# Compile and run the capture-derived TM1650 protocol tests on the host.
# This intentionally avoids ESP-IDF so decoder and bus-state checks stay fast.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
DRIVER_DIR="${REPO_ROOT}/firmware/desk-gateway/components/drivers/mxtark"
CORE_DIR="${REPO_ROOT}/firmware/desk-gateway/components/desk_core"
BLE_DIR="${REPO_ROOT}/firmware/desk-gateway/components/connectivity/ble"
WEB_DIR="${REPO_ROOT}/firmware/desk-gateway/components/connectivity/web"
MQTT_DIR="${REPO_ROOT}/firmware/desk-gateway/components/connectivity/desk_mqtt"
TOF_DIR="${REPO_ROOT}/firmware/desk-gateway/components/sensors/desk_tof"
OLED_DIR="${REPO_ROOT}/firmware/desk-gateway/components/display/desk_oled"
STATUS_LED_DIR="${REPO_ROOT}/firmware/desk-gateway/components/display/desk_status_led"
AUDIO_DIR="${REPO_ROOT}/firmware/desk-gateway/components/desk_audio"
REMINDER_DIR="${REPO_ROOT}/firmware/desk-gateway/components/desk_reminder"
TEST_DIR="$(mktemp -d /tmp/desk-gateway-height-tests.XXXXXX)"

cleanup() {
    # TEST_DIR is a mktemp-created directory with a controlled prefix.
    case "${TEST_DIR}" in
        /tmp/desk-gateway-height-tests.*|/private/tmp/desk-gateway-height-tests.*)
            rm -rf -- "${TEST_DIR}"
            ;;
    esac
}
trap cleanup EXIT

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/tm1650_height_decoder.c" \
    "${DRIVER_DIR}/test/tm1650_height_decoder_test.c" \
    -o "${TEST_DIR}/height-decoder-test"
"${TEST_DIR}/height-decoder-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/mxtark_soft_i2c_sm.c" \
    "${DRIVER_DIR}/test/mxtark_soft_i2c_sm_test.c" \
    -o "${TEST_DIR}/soft-i2c-sm-test"
"${TEST_DIR}/soft-i2c-sm-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/mxtark_preset_logic.c" \
    "${DRIVER_DIR}/test/mxtark_preset_logic_test.c" \
    -o "${TEST_DIR}/preset-logic-test"
"${TEST_DIR}/preset-logic-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/tm1650_height_decoder.c" \
    "${DRIVER_DIR}/mxtark_preset_logic.c" \
    "${DRIVER_DIR}/test/mxtark_upward_pipeline_test.c" \
    -o "${TEST_DIR}/upward-pipeline-test"
"${TEST_DIR}/upward-pipeline-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/mxtark_panel_arbiter.c" \
    "${DRIVER_DIR}/test/mxtark_panel_arbiter_test.c" \
    -o "${TEST_DIR}/panel-arbiter-test"
"${TEST_DIR}/panel-arbiter-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/mxtark_panel_display.c" \
    "${DRIVER_DIR}/test/mxtark_panel_display_test.c" \
    -o "${TEST_DIR}/panel-display-test"
"${TEST_DIR}/panel-display-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${CORE_DIR}/include" \
    "${CORE_DIR}/desk_control_policy.c" \
    "${CORE_DIR}/test/desk_control_policy_test.c" \
    -o "${TEST_DIR}/control-policy-test"
"${TEST_DIR}/control-policy-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${CORE_DIR}/include" \
    "${CORE_DIR}/desk_auto_lock.c" \
    "${CORE_DIR}/test/desk_auto_lock_test.c" \
    -o "${TEST_DIR}/auto-lock-test"
"${TEST_DIR}/auto-lock-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${CORE_DIR}/include" \
    "${CORE_DIR}/desk_height_presets.c" \
    "${CORE_DIR}/test/desk_height_presets_test.c" \
    -o "${TEST_DIR}/height-presets-test"
"${TEST_DIR}/height-presets-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${CORE_DIR}/include" \
    "${CORE_DIR}/desk_motion_watch.c" \
    "${CORE_DIR}/test/desk_motion_watch_test.c" \
    -o "${TEST_DIR}/motion-watch-test"
"${TEST_DIR}/motion-watch-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${BLE_DIR}/include" \
    "${BLE_DIR}/desk_ble_protocol.c" \
    "${BLE_DIR}/test/desk_ble_protocol_test.c" \
    -o "${TEST_DIR}/ble-protocol-test"
"${TEST_DIR}/ble-protocol-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${MQTT_DIR}/include" \
    "${MQTT_DIR}/desk_mqtt_protocol.c" \
    "${MQTT_DIR}/test/desk_mqtt_protocol_test.c" \
    -o "${TEST_DIR}/mqtt-protocol-test"
"${TEST_DIR}/mqtt-protocol-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${BLE_DIR}/include" \
    "${BLE_DIR}/desk_ble_session.c" \
    "${BLE_DIR}/test/desk_ble_session_test.c" \
    -o "${TEST_DIR}/ble-session-test"
"${TEST_DIR}/ble-session-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${BLE_DIR}/include" \
    "${BLE_DIR}/desk_ble_bond_registry.c" \
    "${BLE_DIR}/test/desk_ble_bond_registry_test.c" \
    -o "${TEST_DIR}/ble-bond-registry-test"
"${TEST_DIR}/ble-bond-registry-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${BLE_DIR}/include" \
    -I "${WEB_DIR}/include" \
    "${WEB_DIR}/desk_web_ble_api.c" \
    "${WEB_DIR}/test/desk_web_ble_api_test.c" \
    -o "${TEST_DIR}/web-ble-api-test"
"${TEST_DIR}/web-ble-api-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${TOF_DIR}/include" \
    "${TOF_DIR}/desk_tof_filter.c" \
    "${TOF_DIR}/test/desk_tof_filter_test.c" \
    -o "${TEST_DIR}/tof-filter-test"
"${TEST_DIR}/tof-filter-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${TOF_DIR}/include" \
    "${TOF_DIR}/desk_tof_snapshot_logic.c" \
    "${TOF_DIR}/test/desk_tof_snapshot_logic_test.c" \
    -o "${TEST_DIR}/tof-snapshot-logic-test"
"${TEST_DIR}/tof-snapshot-logic-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${OLED_DIR}/include" \
    "${OLED_DIR}/desk_oled_pages.c" \
    "${OLED_DIR}/test/desk_oled_pages_test.c" \
    -o "${TEST_DIR}/oled-pages-test"
"${TEST_DIR}/oled-pages-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${STATUS_LED_DIR}/include" \
    "${STATUS_LED_DIR}/desk_status_led_logic.c" \
    "${STATUS_LED_DIR}/test/desk_status_led_logic_test.c" \
    -o "${TEST_DIR}/status-led-logic-test"
"${TEST_DIR}/status-led-logic-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${AUDIO_DIR}/include" \
    "${AUDIO_DIR}/desk_audio_wav.c" \
    "${AUDIO_DIR}/test/desk_audio_wav_test.c" \
    -o "${TEST_DIR}/audio-wav-test"
"${TEST_DIR}/audio-wav-test" \
    "${AUDIO_DIR}/../../audio_assets/zh-CN-default/focus_done.wav" \
    "${AUDIO_DIR}/../../audio_assets/zh-CN-default/break_done.wav" \
    "${AUDIO_DIR}/../../audio_assets/zh-CN-default/snooze_done.wav" \
    "${AUDIO_DIR}/../../audio_assets/zh-CN-default/attention_chime.wav"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${REMINDER_DIR}/include" \
    "${REMINDER_DIR}/desk_reminder_logic.c" \
    "${REMINDER_DIR}/test/desk_reminder_logic_test.c" \
    -o "${TEST_DIR}/reminder-logic-test"
"${TEST_DIR}/reminder-logic-test"

"${SCRIPT_DIR}/check-audio-assets.sh"

# Web 测试用 node:assert，需要 Node 16+。espressif/idf 容器没有 node。
if ! command -v node >/dev/null 2>&1; then
    echo "error: node is required for Web static checks" >&2
    exit 127
fi

node --check "${WEB_DIR}/www/app.js"
node --check "${WEB_DIR}/www/bond-management.js"
node --check "${WEB_DIR}/www/height-presets.js"
node --check "${WEB_DIR}/www/reminder-control.js"
node "${WEB_DIR}/test/hold-control.test.js"
node "${WEB_DIR}/test/bond-management.test.js"
node "${WEB_DIR}/test/height-presets.test.js"
node "${WEB_DIR}/test/source-toggle.test.js"
node "${WEB_DIR}/test/reminder-control.test.js"
node "${WEB_DIR}/test/web-ui-structure.test.js"
