/**
 * @file desk_height_presets.c
 * @brief 自定义高度档位的校验和固定容量 CRUD。
 */
#include "desk_height_presets.h"

#include <stdio.h>
#include <string.h>

#define CUSTOM_ID_PREFIX "custom_"

static bool height_valid(int height_mm, int minimum_height_mm,
                         int maximum_height_mm)
{
    return height_mm >= minimum_height_mm && height_mm <= maximum_height_mm;
}

void desk_height_preset_registry_init(desk_height_preset_registry_t *registry)
{
    if (!registry) {
        return;
    }
    memset(registry, 0, sizeof(*registry));
    registry->next_id = 1;
}

bool desk_height_preset_name_valid(const char *name)
{
    if (!name) {
        return false;
    }
    size_t length = strlen(name);
    if (length == 0 || length > DESK_HEIGHT_PRESET_NAME_MAX_BYTES) {
        return false;
    }
    bool has_visible_byte = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)name[i];
        if (byte < 0x20 || byte == 0x7f) {
            return false;
        }
        if (byte > 0x20) {
            has_visible_byte = true;
        }
    }
    return has_visible_byte;
}

bool desk_height_preset_format_id(uint32_t numeric_id, char *out,
                                  size_t out_size)
{
    if (numeric_id == 0 || !out || out_size == 0) {
        return false;
    }
    int length = snprintf(out, out_size, CUSTOM_ID_PREFIX "%08lx",
                          (unsigned long)numeric_id);
    return length > 0 && (size_t)length < out_size;
}

bool desk_height_preset_parse_id(const char *id, uint32_t *out_numeric_id)
{
    if (!id || !out_numeric_id ||
        strncmp(id, CUSTOM_ID_PREFIX, sizeof(CUSTOM_ID_PREFIX) - 1) != 0) {
        return false;
    }
    const char *digits = id + sizeof(CUSTOM_ID_PREFIX) - 1;
    if (strlen(digits) != 8) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        char character = digits[i];
        uint8_t nibble;
        if (character >= '0' && character <= '9') {
            nibble = (uint8_t)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = (uint8_t)(character - 'a' + 10);
        } else {
            return false;
        }
        value = (value << 4) | nibble;
    }
    if (value == 0) {
        return false;
    }
    *out_numeric_id = value;
    return true;
}

desk_height_preset_record_t *desk_height_preset_find(
    desk_height_preset_registry_t *registry, const char *id)
{
    uint32_t numeric_id = 0;
    if (!registry || !desk_height_preset_parse_id(id, &numeric_id)) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_HEIGHT_PRESET_CUSTOM_CAPACITY; ++i) {
        desk_height_preset_record_t *record = &registry->records[i];
        if (record->in_use && record->numeric_id == numeric_id) {
            return record;
        }
    }
    return NULL;
}

const desk_height_preset_record_t *desk_height_preset_find_const(
    const desk_height_preset_registry_t *registry, const char *id)
{
    return desk_height_preset_find((desk_height_preset_registry_t *)registry,
                                   id);
}

bool desk_height_preset_registry_valid(
    const desk_height_preset_registry_t *registry, int minimum_height_mm,
    int maximum_height_mm)
{
    if (!registry || registry->next_id == 0) {
        return false;
    }
    uint32_t greatest_id = 0;
    for (size_t i = 0; i < DESK_HEIGHT_PRESET_CUSTOM_CAPACITY; ++i) {
        const desk_height_preset_record_t *left = &registry->records[i];
        if (left->in_use > 1) {
            return false;
        }
        if (!left->in_use) {
            continue;
        }
        if (left->numeric_id == 0 ||
            !desk_height_preset_name_valid(left->name) ||
            !height_valid(left->height_mm, minimum_height_mm,
                          maximum_height_mm)) {
            return false;
        }
        if (left->numeric_id > greatest_id) {
            greatest_id = left->numeric_id;
        }
        for (size_t j = i + 1; j < DESK_HEIGHT_PRESET_CUSTOM_CAPACITY; ++j) {
            const desk_height_preset_record_t *right = &registry->records[j];
            if (right->in_use && right->numeric_id == left->numeric_id) {
                return false;
            }
        }
    }
    return registry->next_id > greatest_id;
}

bool desk_height_preset_create(desk_height_preset_registry_t *registry,
                               const char *name, int height_mm,
                               int minimum_height_mm, int maximum_height_mm,
                               char *out_id, size_t out_id_size)
{
    if (!registry || !desk_height_preset_name_valid(name) ||
        !height_valid(height_mm, minimum_height_mm, maximum_height_mm) ||
        registry->next_id == 0 || registry->next_id == UINT32_MAX) {
        return false;
    }
    for (size_t i = 0; i < DESK_HEIGHT_PRESET_CUSTOM_CAPACITY; ++i) {
        desk_height_preset_record_t *record = &registry->records[i];
        if (record->in_use) {
            continue;
        }
        uint32_t numeric_id = registry->next_id;
        if (!desk_height_preset_format_id(numeric_id, out_id, out_id_size)) {
            return false;
        }
        memset(record, 0, sizeof(*record));
        record->in_use = 1;
        record->numeric_id = numeric_id;
        record->height_mm = height_mm;
        memcpy(record->name, name, strlen(name) + 1);
        registry->next_id += 1;
        return true;
    }
    return false;
}

bool desk_height_preset_update(desk_height_preset_registry_t *registry,
                               const char *id, const char *name, int height_mm,
                               int minimum_height_mm, int maximum_height_mm)
{
    desk_height_preset_record_t *record = desk_height_preset_find(registry, id);
    if (!record || !desk_height_preset_name_valid(name) ||
        !height_valid(height_mm, minimum_height_mm, maximum_height_mm)) {
        return false;
    }
    record->height_mm = height_mm;
    memset(record->name, 0, sizeof(record->name));
    memcpy(record->name, name, strlen(name) + 1);
    return true;
}

bool desk_height_preset_delete(desk_height_preset_registry_t *registry,
                               const char *id)
{
    desk_height_preset_record_t *record = desk_height_preset_find(registry, id);
    if (!record) {
        return false;
    }
    memset(record, 0, sizeof(*record));
    return true;
}

size_t desk_height_preset_count(
    const desk_height_preset_registry_t *registry)
{
    if (!registry) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < DESK_HEIGHT_PRESET_CUSTOM_CAPACITY; ++i) {
        if (registry->records[i].in_use) {
            count += 1;
        }
    }
    return count;
}
