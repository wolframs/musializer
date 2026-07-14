#ifndef MUSIALIZER_SCENE_SWITCH_H_
#define MUSIALIZER_SCENE_SWITCH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scene_settings_values.h"

#define SCENE_SWITCH_CAPACITY 256u

typedef struct Scene_Switch_Cue {
    uint64_t id;
    double start_seconds;
    double end_seconds;
    uint32_t scene_index;
    float strength;
    Scene_Settings_Snapshot settings;
} Scene_Switch_Cue;

typedef struct Scene_Switch_Timeline {
    bool enabled;
    size_t active_index;
    size_t count;
    Scene_Switch_Cue cues[SCENE_SWITCH_CAPACITY];
} Scene_Switch_Timeline;

typedef enum Scene_Switch_Result {
    SCENE_SWITCH_OK = 0,
    SCENE_SWITCH_NO_CHANGE,
    SCENE_SWITCH_ERROR_NULL,
    SCENE_SWITCH_ERROR_CAPACITY,
    SCENE_SWITCH_ERROR_DURATION,
    SCENE_SWITCH_ERROR_CUE,
    SCENE_SWITCH_ERROR_ORDER,
    SCENE_SWITCH_ERROR_COVERAGE,
    SCENE_SWITCH_ERROR_DUPLICATE_ID
} Scene_Switch_Result;

void scene_switch_init(Scene_Switch_Timeline *timeline);
void scene_switch_reset(Scene_Switch_Timeline *timeline);
Scene_Switch_Result scene_switch_replace(Scene_Switch_Timeline *timeline,
                                         const Scene_Switch_Cue *cues,
                                         size_t count,
                                         double duration_seconds,
                                         uint32_t scene_count);
Scene_Switch_Result scene_switch_update(Scene_Switch_Timeline *timeline,
                                        double time_seconds,
                                        uint32_t *scene_index);
Scene_Switch_Result scene_switch_cue_at(Scene_Switch_Timeline *timeline,
                                        double time_seconds,
                                        double duration_seconds,
                                        uint32_t scene_index,
                                        uint32_t scene_count,
                                        float strength,
                                        const Scene_Settings_Snapshot *settings);
const char *scene_switch_result_string(Scene_Switch_Result result);

#endif // MUSIALIZER_SCENE_SWITCH_H_
