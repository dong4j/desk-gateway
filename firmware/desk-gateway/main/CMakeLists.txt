idf_component_register(
    SRCS "app_main.c" "console_cmd.c"
    INCLUDE_DIRS "."
    REQUIRES ble desk_audio desk_core desk_oled desk_peripheral_i2c desk_reminder desk_tof mxtark wifi web nvs_flash freertos
)
