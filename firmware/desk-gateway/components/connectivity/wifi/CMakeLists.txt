idf_component_register(
    SRCS "desk_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash mdns
)
