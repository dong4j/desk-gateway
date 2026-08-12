idf_component_register(
    SRCS "desk_ble.c" "desk_ble_protocol.c" "desk_ble_session.c"
    INCLUDE_DIRS "include"
    REQUIRES bt desk_core esp_app_format esp_system esp_timer freertos
)
