idf_component_register(
    SRCS "desk_audio.c" "desk_audio_wav.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2s esp_timer freertos nvs_flash spiffs
)
