idf_component_register(
    SRCS "desk_audio.c" "desk_audio_wav.c" "desk_motion_audio.c"
         "desk_motion_audio_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_core desk_tof esp_driver_i2s esp_timer freertos nvs_flash spiffs
)
