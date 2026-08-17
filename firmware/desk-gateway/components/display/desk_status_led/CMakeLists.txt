idf_component_register(
    SRCS "desk_status_led.c" "desk_status_led_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_core wifi esp_driver_gpio freertos
)
