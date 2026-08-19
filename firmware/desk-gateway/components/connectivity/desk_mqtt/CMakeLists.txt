# 目录必须叫 desk_mqtt：本地组件若命名 mqtt，会与 espressif/mqtt 撞名并自链接。
idf_component_register(
    SRCS "desk_mqtt.c" "desk_mqtt_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_core wifi nvs_flash esp_event esp_app_format esp_system
             esp-tls mqtt
)
