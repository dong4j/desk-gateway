idf_component_register(
    SRCS "yourdesk_v1.c" "tm1650_height_decoder.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_driver esp_driver_i2c esp_driver_gpio driver freertos
)
