#!/usr/bin/env bash
# Compile and run the capture-derived TM1650 protocol tests on the host.
# This intentionally avoids ESP-IDF so decoder and bus-state checks stay fast.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
DRIVER_DIR="${REPO_ROOT}/firmware/desk-gateway/components/drivers/yourdesk_v1"
CORE_DIR="${REPO_ROOT}/firmware/desk-gateway/components/desk_core"
BLE_DIR="${REPO_ROOT}/firmware/desk-gateway/components/connectivity/ble"
WEB_DIR="${REPO_ROOT}/firmware/desk-gateway/components/connectivity/web"
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
    "${DRIVER_DIR}/yourdesk_soft_i2c_sm.c" \
    "${DRIVER_DIR}/test/yourdesk_soft_i2c_sm_test.c" \
    -o "${TEST_DIR}/soft-i2c-sm-test"
"${TEST_DIR}/soft-i2c-sm-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/yourdesk_preset_logic.c" \
    "${DRIVER_DIR}/test/yourdesk_preset_logic_test.c" \
    -o "${TEST_DIR}/preset-logic-test"
"${TEST_DIR}/preset-logic-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/tm1650_height_decoder.c" \
    "${DRIVER_DIR}/yourdesk_preset_logic.c" \
    "${DRIVER_DIR}/test/yourdesk_upward_pipeline_test.c" \
    -o "${TEST_DIR}/upward-pipeline-test"
"${TEST_DIR}/upward-pipeline-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/yourdesk_panel_arbiter.c" \
    "${DRIVER_DIR}/test/yourdesk_panel_arbiter_test.c" \
    -o "${TEST_DIR}/panel-arbiter-test"
"${TEST_DIR}/panel-arbiter-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${CORE_DIR}/include" \
    "${CORE_DIR}/desk_control_policy.c" \
    "${CORE_DIR}/test/desk_control_policy_test.c" \
    -o "${TEST_DIR}/control-policy-test"
"${TEST_DIR}/control-policy-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I "${BLE_DIR}/include" \
    "${BLE_DIR}/desk_ble_protocol.c" \
    "${BLE_DIR}/test/desk_ble_protocol_test.c" \
    -o "${TEST_DIR}/ble-protocol-test"
"${TEST_DIR}/ble-protocol-test"

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

node --check "${WEB_DIR}/www/app.js"
node --check "${WEB_DIR}/www/bond-management.js"
node "${WEB_DIR}/test/hold-control.test.js"
node "${WEB_DIR}/test/bond-management.test.js"
