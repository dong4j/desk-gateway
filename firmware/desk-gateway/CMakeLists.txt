# Desk Gateway 主固件：多 Driver 平台 + Web
cmake_minimum_required(VERSION 3.16)

# 嵌套目录需 EXTRA；组件名 = 目录名（wifi / web / yourdesk_v1 …）
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/yourdesk_v1"
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/loctek"
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/jiecang"
    "${CMAKE_CURRENT_LIST_DIR}/components/connectivity/wifi"
    "${CMAKE_CURRENT_LIST_DIR}/components/connectivity/web"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(desk-gateway)
