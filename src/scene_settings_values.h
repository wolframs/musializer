#ifndef MUSIALIZER_SCENE_SETTINGS_VALUES_H_
#define MUSIALIZER_SCENE_SETTINGS_VALUES_H_

#include <stdbool.h>
#include <stddef.h>

enum {
    SCENE_SETTINGS_SCENE_COUNT = 9,
    SCENE_SETTINGS_MAX_CONTROLS = 12,
    SCENE_SETTINGS_PRESETS_PER_SCENE = 8,
};

typedef struct Scene_Settings_Snapshot {
    bool captured;
    size_t count;
    float values[SCENE_SETTINGS_MAX_CONTROLS];
} Scene_Settings_Snapshot;

#endif // MUSIALIZER_SCENE_SETTINGS_VALUES_H_
