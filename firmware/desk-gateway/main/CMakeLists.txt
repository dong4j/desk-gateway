idf_component_register(
    SRCS "app_main.c" "console_cmd.c"
    INCLUDE_DIRS "."
    REQUIRES ble desk_core desk_tof yourdesk_v1 wifi web nvs_flash freertos
)
