# Desk Gateway Phase 1：ESP32 模拟原厂面板（I²C Slave @ 0x24）
# 目标板：YD-ESP32-S3 N16R8；协议契约见 docs/3-protocol-reverse-notes.md §18
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(phase1-panel-slave)
