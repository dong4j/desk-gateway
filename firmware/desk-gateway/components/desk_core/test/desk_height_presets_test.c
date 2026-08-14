/** @file desk_height_presets_test.c @brief 自定义高度档位模型的主机测试。 */
#include "desk_height_presets.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    desk_height_preset_registry_t registry;
    desk_height_preset_registry_init(&registry);
    assert(desk_height_preset_registry_valid(&registry, 560, 940));
    assert(desk_height_preset_name_valid("午休"));
    assert(!desk_height_preset_name_valid("   "));
    assert(!desk_height_preset_name_valid("坏\n名称"));

    char id[DESK_HEIGHT_PRESET_ID_BUFFER_LENGTH];
    assert(desk_height_preset_create(&registry, "午休", 720, 560, 940,
                                     id, sizeof(id)));
    assert(strcmp(id, "custom_00000001") == 0);
    assert(desk_height_preset_count(&registry) == 1);
    assert(desk_height_preset_update(&registry, id, "阅读", 760,
                                     560, 940));
    const desk_height_preset_record_t *record =
        desk_height_preset_find_const(&registry, id);
    assert(record && record->height_mm == 760);
    assert(strcmp(record->name, "阅读") == 0);
    assert(desk_height_preset_delete(&registry, id));
    assert(desk_height_preset_count(&registry) == 0);

    for (size_t i = 0; i < DESK_HEIGHT_PRESET_CUSTOM_CAPACITY; ++i) {
        assert(desk_height_preset_create(&registry, "档位", 700, 560, 940,
                                         id, sizeof(id)));
    }
    assert(!desk_height_preset_create(&registry, "超出容量", 700, 560, 940,
                                      id, sizeof(id)));
    assert(desk_height_preset_registry_valid(&registry, 560, 940));
    puts("desk_height_presets_test: OK");
    return 0;
}
