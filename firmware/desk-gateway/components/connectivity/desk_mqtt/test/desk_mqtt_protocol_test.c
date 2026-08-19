/**
 * @file desk_mqtt_protocol_test.c
 * @brief MQTT v1 Topic / Payload / Discovery 宿主机回归
 */
#include "desk_mqtt_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_device_id_and_topics(void)
{
    const uint8_t mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    char id[DESK_MQTT_DEVICE_ID_BUFFER];
    char buf[DESK_MQTT_TOPIC_BUFFER];
    char client[DESK_MQTT_CLIENT_ID_BUFFER];

    assert(desk_mqtt_device_id_from_mac(mac, id, sizeof(id)));
    assert(strcmp(id, "aabbccddeeff") == 0);
    assert(desk_mqtt_format_client_id(id, client, sizeof(client)));
    assert(strcmp(client, "desk-gateway-aabbccddeeff") == 0);
    assert(desk_mqtt_topic_availability(id, buf, sizeof(buf)));
    assert(strcmp(buf, "desk-gateway/aabbccddeeff/availability") == 0);
    assert(desk_mqtt_topic_state(id, buf, sizeof(buf)));
    assert(strcmp(buf, "desk-gateway/aabbccddeeff/state") == 0);
    assert(desk_mqtt_topic_command(id, buf, sizeof(buf)));
    assert(strcmp(buf, "desk-gateway/aabbccddeeff/command") == 0);
    assert(desk_mqtt_topic_result(id, buf, sizeof(buf)));
    assert(strcmp(buf, "desk-gateway/aabbccddeeff/result") == 0);
    assert(desk_mqtt_topic_component_discovery(
        "homeassistant", DESK_MQTT_DISCOVERY_COVER, id, buf, sizeof(buf)));
    assert(strcmp(buf, "homeassistant/cover/aabbccddeeff/config") == 0);
    assert(desk_mqtt_topic_component_discovery(
        "homeassistant", DESK_MQTT_DISCOVERY_HEIGHT, id, buf, sizeof(buf)));
    assert(strcmp(buf, "homeassistant/sensor/aabbccddeeff_height/config") == 0);
    assert(desk_mqtt_topic_component_discovery(
        "homeassistant", DESK_MQTT_DISCOVERY_CHILD_LOCK, id, buf,
        sizeof(buf)));
    assert(strcmp(buf,
                  "homeassistant/binary_sensor/aabbccddeeff_child_lock/config") ==
           0);
    assert(desk_mqtt_is_command_topic(id, buf, strlen(buf)) == false);
    const char *cmd = "desk-gateway/aabbccddeeff/command";
    assert(desk_mqtt_is_command_topic(id, cmd, strlen(cmd)));
    assert(!desk_mqtt_is_command_topic(id, "desk-gateway/#", 15));
    assert(!desk_mqtt_is_command_topic(id, "desk-gateway/aabbccddeeff/command/x",
                                       35));
    assert(desk_mqtt_is_ha_status_topic("homeassistant/status", 20));
    assert(!desk_mqtt_is_ha_status_topic("homeassistant/status/x", 22));
}

static void test_command_parser_rejects_unsafe_payloads(void)
{
    desk_mqtt_action_t action = DESK_MQTT_ACTION_NONE;
    assert(desk_mqtt_parse_command("SIT", 3, false, &action));
    assert(action == DESK_MQTT_ACTION_SIT);
    assert(desk_mqtt_parse_command("STAND", 5, false, &action));
    assert(action == DESK_MQTT_ACTION_STAND);
    assert(desk_mqtt_parse_command("STOP", 4, false, &action));
    assert(action == DESK_MQTT_ACTION_STOP);
    assert(!desk_mqtt_parse_command("STOP", 4, true, &action));
    assert(!desk_mqtt_parse_command("stop", 4, false, &action));
    assert(!desk_mqtt_parse_command(" STOP", 5, false, &action));
    assert(!desk_mqtt_parse_command("STOP\n", 5, false, &action));
    assert(!desk_mqtt_parse_command("UP", 2, false, &action));
    assert(!desk_mqtt_parse_command("", 0, false, &action));
    assert(!desk_mqtt_parse_command("STANDING", 8, false, &action));
}

static void test_cover_position_and_state(void)
{
    assert(desk_mqtt_cover_position(true, 550, 550, 870) == 0);
    assert(desk_mqtt_cover_position(true, 870, 550, 870) == 100);
    assert(desk_mqtt_cover_position(true, 710, 550, 870) == 50);
    assert(desk_mqtt_cover_position(false, 710, 550, 870) == -1);
    assert(desk_mqtt_cover_position(true, 710, 870, 550) == -1);
    assert(strcmp(desk_mqtt_cover_state(DESK_MQTT_STATUS_MOVING_UP, true, 600,
                                        false, 0),
                  "opening") == 0);
    assert(strcmp(desk_mqtt_cover_state(DESK_MQTT_STATUS_MOVING_DOWN, true, 600,
                                        false, 0),
                  "closing") == 0);
    assert(strcmp(desk_mqtt_cover_state(DESK_MQTT_STATUS_IDLE, true, 600, true,
                                        590),
                  "stopped") == 0);
    assert(strcmp(desk_mqtt_cover_state(DESK_MQTT_STATUS_GOTO_PRESET, true, 600,
                                        false, 0),
                  "stopped") == 0);
    assert(strcmp(desk_mqtt_cover_state(DESK_MQTT_STATUS_GOTO_PRESET, true, 620,
                                        true, 600),
                  "opening") == 0);
    assert(strcmp(desk_mqtt_cover_state(DESK_MQTT_STATUS_GOTO_PRESET, true, 580,
                                        true, 600),
                  "closing") == 0);
}

static void test_config_validation(void)
{
    desk_mqtt_config_t config;
    const char *reason = NULL;
    desk_mqtt_config_init_defaults(&config);
    assert(!config.client_enabled);
    assert(config.port == DESK_MQTT_DEFAULT_PORT);
    assert(strcmp(config.discovery_prefix, DESK_MQTT_DEFAULT_PREFIX) == 0);
    assert(!desk_mqtt_config_valid(&config, &reason));
    assert(reason != NULL);

    strncpy(config.host, "192.168.1.10", sizeof(config.host) - 1);
    assert(desk_mqtt_config_valid(&config, &reason));

    strncpy(config.host, "mqtt://evil", sizeof(config.host) - 1);
    assert(!desk_mqtt_config_valid(&config, &reason));
    strncpy(config.host, "user@host", sizeof(config.host) - 1);
    assert(!desk_mqtt_config_valid(&config, &reason));
    strncpy(config.host, "192.168.1.10", sizeof(config.host) - 1);
    strncpy(config.discovery_prefix, "home assistant",
            sizeof(config.discovery_prefix) - 1);
    assert(!desk_mqtt_config_valid(&config, &reason));
}

static void test_state_and_result_json(void)
{
    char json[DESK_MQTT_STATE_JSON_MAX];
    desk_mqtt_state_input_t input = {
        .status = DESK_MQTT_STATUS_IDLE,
        .height_known = true,
        .height_mm = 820,
        .have_previous_height = false,
        .preset1_height_mm = 550,
        .preset4_height_mm = 870,
        .max_height_mm = 940,
        .child_lock = false,
        .upward_blocked = false,
        .mqtt_control_enabled = true,
        .driver = "mxtark",
        .firmware_version = "0.1.0",
    };
    size_t n = desk_mqtt_format_state(&input, json, sizeof(json));
    assert(n > 0);
    assert(strstr(json, "\"version\":1") != NULL);
    assert(strstr(json, "\"height_mm\":820") != NULL);
    assert(strstr(json, "\"cover_state\":\"stopped\"") != NULL);
    assert(strstr(json, "\"mqtt_control_enabled\":true") != NULL);

    input.height_known = false;
    n = desk_mqtt_format_state(&input, json, sizeof(json));
    assert(n > 0);
    assert(strstr(json, "\"height_mm\":null") != NULL);
    assert(strstr(json, "\"position\":null") != NULL);

    char result[DESK_MQTT_RESULT_JSON_MAX];
    n = desk_mqtt_format_result(43, DESK_MQTT_ACTION_STAND, false,
                                "ESP_ERR_NOT_ALLOWED", "child_lock", result,
                                sizeof(result));
    assert(n > 0);
    assert(strstr(result, "\"ok\":false") != NULL);
    assert(strstr(result, "\"reason\":\"child_lock\"") != NULL);
}

static void test_discovery_unique_ids(void)
{
    char json[DESK_MQTT_DISCOVERY_JSON_MAX];
    size_t n = desk_mqtt_format_component_discovery(
        DESK_MQTT_DISCOVERY_COVER, "aabbccddeeff", "0.1.0", json, sizeof(json));
    assert(n > 0);
    assert(strstr(json, "desk_gateway_aabbccddeeff_cover") != NULL);
    assert(strstr(json, "\"payload_open\":\"STAND\"") != NULL);
    assert(strstr(json, "\"payload_close\":\"SIT\"") != NULL);
    assert(strstr(json, "\"payload_stop\":\"STOP\"") != NULL);
    assert(strstr(json, "desk-gateway/aabbccddeeff/command") != NULL);
    assert(strstr(json, "set_position_topic") == NULL);
    assert(strstr(json, "position_topic") == NULL);

    n = desk_mqtt_format_component_discovery(DESK_MQTT_DISCOVERY_HEIGHT,
                                             "aabbccddeeff", "0.1.0", json,
                                             sizeof(json));
    assert(n > 0);
    assert(strstr(json, "desk_gateway_aabbccddeeff_height") != NULL);
    assert(strstr(json, "\"device_class\":\"distance\"") != NULL);

    n = desk_mqtt_format_component_discovery(DESK_MQTT_DISCOVERY_CHILD_LOCK,
                                             "aabbccddeeff", "0.1.0", json,
                                             sizeof(json));
    assert(n > 0);
    assert(strstr(json, "desk_gateway_aabbccddeeff_child_lock") != NULL);
    assert(strstr(json, "payload_on\":\"ON\"") != NULL);
}

static void test_command_queue_stop_priority(void)
{
    desk_mqtt_cmd_queue_t queue;
    desk_mqtt_action_t action = DESK_MQTT_ACTION_NONE;
    desk_mqtt_cmd_queue_init(&queue);

    assert(desk_mqtt_cmd_queue_push(&queue, DESK_MQTT_ACTION_SIT) ==
           DESK_MQTT_QUEUE_OK);
    assert(desk_mqtt_cmd_queue_push(&queue, DESK_MQTT_ACTION_STAND) ==
           DESK_MQTT_QUEUE_OK);
    assert(desk_mqtt_cmd_queue_push(&queue, DESK_MQTT_ACTION_STOP) ==
           DESK_MQTT_QUEUE_OK);
    assert(desk_mqtt_cmd_queue_pop(&queue, &action));
    assert(action == DESK_MQTT_ACTION_STOP);
    assert(!desk_mqtt_cmd_queue_pop(&queue, &action));

    for (int i = 0; i < DESK_MQTT_QUEUE_CAPACITY; i++) {
        assert(desk_mqtt_cmd_queue_push(&queue, DESK_MQTT_ACTION_SIT) ==
               DESK_MQTT_QUEUE_OK);
    }
    assert(desk_mqtt_cmd_queue_push(&queue, DESK_MQTT_ACTION_STAND) ==
           DESK_MQTT_QUEUE_FULL);
    assert(desk_mqtt_cmd_queue_push(&queue, DESK_MQTT_ACTION_STOP) ==
           DESK_MQTT_QUEUE_OK);
    assert(desk_mqtt_cmd_queue_pop(&queue, &action));
    assert(action == DESK_MQTT_ACTION_STOP);
}

static void test_payload_fragment_reassembly(void)
{
    desk_mqtt_payload_accum_t accum;
    desk_mqtt_payload_accum_reset(&accum);
    assert(desk_mqtt_payload_accum_feed(&accum, 0, 5, "ST", 2));
    assert(!desk_mqtt_payload_accum_ready(&accum));
    assert(desk_mqtt_payload_accum_feed(&accum, 2, 5, "AND", 3));
    assert(desk_mqtt_payload_accum_ready(&accum));
    assert(memcmp(accum.buf, "STAND", 5) == 0);

    desk_mqtt_payload_accum_reset(&accum);
    assert(!desk_mqtt_payload_accum_feed(&accum, 0, 64, "x", 1));
    assert(!desk_mqtt_payload_accum_ready(&accum));
    assert(accum.overflow);

    desk_mqtt_payload_accum_reset(&accum);
    assert(desk_mqtt_payload_accum_feed(&accum, 0, 4, "SIT", 3));
    assert(!desk_mqtt_payload_accum_feed(&accum, 1, 4, "X", 1));
    assert(!desk_mqtt_payload_accum_ready(&accum));
}

int main(void)
{
    test_device_id_and_topics();
    test_command_parser_rejects_unsafe_payloads();
    test_cover_position_and_state();
    test_config_validation();
    test_state_and_result_json();
    test_discovery_unique_ids();
    test_command_queue_stop_priority();
    test_payload_fragment_reassembly();
    puts("desk mqtt protocol vectors: OK");
    return 0;
}
