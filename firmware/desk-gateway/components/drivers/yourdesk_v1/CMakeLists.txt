idf_component_register(
    SRCS "yourdesk_v1.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_driver esp_driver_i2c driver freertos
)
