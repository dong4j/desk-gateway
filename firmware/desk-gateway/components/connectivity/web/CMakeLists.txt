idf_component_register(
    SRCS "desk_web.c"
    INCLUDE_DIRS "include"
    # IDF 6：勿再 REQUIRES json；由 idf_component.yml 的 espressif/cjson 提供
    REQUIRES desk_core wifi esp_app_format esp_http_server esp_system nvs_flash
    EMBED_FILES
        "www/login.html"
        "www/index.html"
        "www/app.js"
        "www/style.css"
        "www/desk-workstation.webp"
        "www/setup.html"
)
