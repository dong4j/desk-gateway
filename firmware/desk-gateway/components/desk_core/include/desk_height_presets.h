/**
 * @file desk_height_presets.h
 * @brief 网关自定义高度档位的固定容量数据模型。
 *
 * 模型不依赖 ESP-IDF，便于在主机测试中验证名称、ID 和容量边界；NVS
 * 持久化与实际运动仍由 desk_core 负责。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_HEIGHT_PRESET_CUSTOM_CAPACITY 16
#define DESK_HEIGHT_PRESET_NAME_MAX_BYTES 48
#define DESK_HEIGHT_PRESET_NAME_BUFFER_LENGTH 49
#define DESK_HEIGHT_PRESET_ID_BUFFER_LENGTH 20

typedef struct {
    uint8_t in_use;
    uint8_t reserved[3];
    uint32_t numeric_id;
    int32_t height_mm;
    char name[DESK_HEIGHT_PRESET_NAME_BUFFER_LENGTH];
} desk_height_preset_record_t;

typedef struct {
    uint32_t next_id;
    desk_height_preset_record_t records[DESK_HEIGHT_PRESET_CUSTOM_CAPACITY];
} desk_height_preset_registry_t;

void desk_height_preset_registry_init(desk_height_preset_registry_t *registry);
bool desk_height_preset_registry_valid(
    const desk_height_preset_registry_t *registry, int minimum_height_mm,
    int maximum_height_mm);
bool desk_height_preset_name_valid(const char *name);
bool desk_height_preset_format_id(uint32_t numeric_id, char *out,
                                  size_t out_size);
bool desk_height_preset_parse_id(const char *id, uint32_t *out_numeric_id);
desk_height_preset_record_t *desk_height_preset_find(
    desk_height_preset_registry_t *registry, const char *id);
const desk_height_preset_record_t *desk_height_preset_find_const(
    const desk_height_preset_registry_t *registry, const char *id);
bool desk_height_preset_create(desk_height_preset_registry_t *registry,
                               const char *name, int height_mm,
                               int minimum_height_mm, int maximum_height_mm,
                               char *out_id, size_t out_id_size);
bool desk_height_preset_update(desk_height_preset_registry_t *registry,
                               const char *id, const char *name, int height_mm,
                               int minimum_height_mm, int maximum_height_mm);
bool desk_height_preset_delete(desk_height_preset_registry_t *registry,
                               const char *id);
size_t desk_height_preset_count(
    const desk_height_preset_registry_t *registry);

#ifdef __cplusplus
}
#endif
