idf_component_register(
    SRCS "desk_ble.c" "desk_ble_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES bt desk_core esp_timer freertos
)
