idf_component_register(
    SRCS "desk_tof.c" "desk_tof_filter.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c freertos
)
