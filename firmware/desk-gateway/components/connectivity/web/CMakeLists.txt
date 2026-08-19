idf_component_register(
    SRCS "desk_web.c" "desk_web_ble_api.c"
    INCLUDE_DIRS "include"
    # IDF 6：勿再 REQUIRES json；由 idf_component.yml 的 espressif/cjson 提供
    REQUIRES ble desk_audio desk_core desk_mqtt desk_reminder desk_tof wifi esp_app_format esp_http_server esp_system nvs_flash
    EMBED_FILES
        "www/login.html"
        "www/index.html"
        "www/hold-control.js"
        "www/bond-management.js"
        "www/height-presets.js"
        "www/reminder-control.js"
        "www/app.js"
        "www/style.css"
        "www/favicon.png"
        "www/desk-workstation.webp"
        "www/setup.html"
)
