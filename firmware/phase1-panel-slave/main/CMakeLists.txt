# Phase 1 主组件：I²C Slave 键应答 + 串口命令切 DR
idf_component_register(
    SRCS
        "app_main.c"
        "desk_dr.c"
        "i2c_panel_slave.c"
        "console_cmd.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_driver_i2c
        freertos
        esp_timer
        nvs_flash
)
