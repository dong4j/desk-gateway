idf_component_register(
    SRCS "desk_core.c" "desk_control_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_driver nvs_flash esp_timer
)
