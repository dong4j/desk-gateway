idf_component_register(
    SRCS "desk_core.c" "desk_control_policy.c" "desk_height_presets.c"
         "desk_motion_watch.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_driver nvs_flash esp_timer
)
