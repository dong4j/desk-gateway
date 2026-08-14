idf_component_register(
    SRCS "desk_reminder.c" "desk_reminder_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_audio esp_timer freertos nvs_flash
)
