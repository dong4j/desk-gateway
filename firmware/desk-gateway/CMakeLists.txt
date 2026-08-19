# Desk Gateway 主固件：多 Driver 平台 + Web
cmake_minimum_required(VERSION 3.16)

# 嵌套目录需 EXTRA；组件名 = 目录名（wifi / web / mxtark …）
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/mxtark"
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/loctek"
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/jiecang"
    "${CMAKE_CURRENT_LIST_DIR}/components/connectivity/wifi"
    "${CMAKE_CURRENT_LIST_DIR}/components/connectivity/web"
    "${CMAKE_CURRENT_LIST_DIR}/components/connectivity/ble"
    "${CMAKE_CURRENT_LIST_DIR}/components/connectivity/desk_mqtt"
    "${CMAKE_CURRENT_LIST_DIR}/components/desk_audio"
    "${CMAKE_CURRENT_LIST_DIR}/components/desk_reminder"
    "${CMAKE_CURRENT_LIST_DIR}/components/peripherals/desk_peripheral_i2c"
    "${CMAKE_CURRENT_LIST_DIR}/components/sensors/desk_tof"
    "${CMAKE_CURRENT_LIST_DIR}/components/display/desk_oled"
    "${CMAKE_CURRENT_LIST_DIR}/components/display/desk_status_led"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(desk-gateway)

# 语音资源独立于应用镜像；完整 idf.py flash 会同时烧录此 SPIFFS 镜像。
spiffs_create_partition_image(audio "${CMAKE_CURRENT_LIST_DIR}/audio_assets"
                              FLASH_IN_PROJECT)
