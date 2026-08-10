#!/usr/bin/env bash
# Compile and run the capture-derived TM1650 height decoder vectors on the host.
# This intentionally avoids ESP-IDF so protocol checks stay fast and portable.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
DRIVER_DIR="${REPO_ROOT}/firmware/desk-gateway/components/drivers/yourdesk_v1"
TEST_BIN="$(mktemp /tmp/desk-gateway-height-test.XXXXXX)"

cleanup() {
    # TEST_BIN is a single mktemp-created file with a controlled prefix.
    case "${TEST_BIN}" in
        /tmp/desk-gateway-height-test.*|/private/tmp/desk-gateway-height-test.*)
            rm -f -- "${TEST_BIN}"
            ;;
    esac
}
trap cleanup EXIT

cc -std=c11 -Wall -Wextra -Werror \
    -I "${DRIVER_DIR}" \
    "${DRIVER_DIR}/tm1650_height_decoder.c" \
    "${DRIVER_DIR}/test/tm1650_height_decoder_test.c" \
    -o "${TEST_BIN}"
"${TEST_BIN}"
