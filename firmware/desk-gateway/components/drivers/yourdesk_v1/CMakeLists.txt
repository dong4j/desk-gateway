set(yourdesk_v1_sources "yourdesk_v1.c")
set(yourdesk_v1_requires
    desk_driver esp_driver_i2c esp_driver_gpio driver freertos)

# 产品固件只编译硬件 I²C @0x24 控制路径。以下源文件属于显式开启的
# 协议实验，避免默认构建即使不初始化也把软件 I²C 状态机带入固件。
if(CONFIG_DESK_YOURDESK_SOFT_I2C_MULTI_ADDRESS OR
   CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL)
    list(APPEND yourdesk_v1_sources
         "tm1650_height_decoder.c"
         "yourdesk_preset_logic.c")
endif()

if(CONFIG_DESK_YOURDESK_SOFT_I2C_MULTI_ADDRESS)
    list(APPEND yourdesk_v1_sources
         "yourdesk_soft_i2c_sm.c"
         "yourdesk_soft_i2c_esp.c")
endif()

if(CONFIG_DESK_YOURDESK_PANEL_PROXY)
    list(APPEND yourdesk_v1_sources
         "yourdesk_panel_arbiter.c"
         "yourdesk_panel_display.c"
         "yourdesk_panel_proxy.c")
endif()

idf_component_register(
    SRCS ${yourdesk_v1_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${yourdesk_v1_requires}
    PRIV_REQUIRES desk_tof
)

# ISR-reachable switch statements must not emit Flash-resident jump tables.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-jump-tables
    -fno-tree-switch-conversion
)
