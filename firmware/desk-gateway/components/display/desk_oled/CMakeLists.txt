idf_component_register(
    SRCS "desk_oled.c" "desk_oled_pages.c"
    INCLUDE_DIRS "include"
    REQUIRES ble desk_core desk_tof wifi esp_driver_i2c freertos
)
