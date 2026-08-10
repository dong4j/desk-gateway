idf_component_register(
    SRCS "yourdesk_v1.c"
         "tm1650_height_decoder.c"
         "yourdesk_preset_logic.c"
         "yourdesk_panel_arbiter.c"
         "yourdesk_panel_proxy.c"
         "yourdesk_soft_i2c_sm.c"
         "yourdesk_soft_i2c_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_driver esp_driver_i2c esp_driver_gpio driver freertos
)

# ISR-reachable switch statements must not emit Flash-resident jump tables.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-jump-tables
    -fno-tree-switch-conversion
)
