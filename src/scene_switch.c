#include "scene_switch.h"

#include <math.h>
#include <string.h>

void scene_switch_init(Scene_Switch_Timeline *timeline)
{
    if (timeline == NULL) return;
    memset(timeline, 0, sizeof(*timeline));
    timeline->active_index = SIZE_MAX;
}

void scene_switch_reset(Scene_Switch_Timeline *timeline)
{
    if (timeline != NULL) timeline->active_index = SIZE_MAX;
}

Scene_Switch_Result scene_switch_replace(Scene_Switch_Timeline *timeline,
                                         const Scene_Switch_Cue *cues,
                                         size_t count,
                                         double duration_seconds,
                                         uint32_t scene_count)
{
    if (timeline == NULL || (count > 0 && cues == NULL)) return SCENE_SWITCH_ERROR_NULL;
    if (count == 0 || count > SCENE_SWITCH_CAPACITY) return SCENE_SWITCH_ERROR_CAPACITY;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0 || scene_count == 0) {
        return SCENE_SWITCH_ERROR_DURATION;
    }
    double cursor = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const Scene_Switch_Cue *cue = &cues[i];
        if (cue->id == 0 || !isfinite(cue->start_seconds) ||
            !isfinite(cue->end_seconds) || cue->start_seconds < 0.0 ||
            cue->end_seconds <= cue->start_seconds ||
            cue->end_seconds > duration_seconds || cue->scene_index >= scene_count ||
            !isfinite(cue->strength) || cue->strength < 0.0f || cue->strength > 1.0f) {
            return SCENE_SWITCH_ERROR_CUE;
        }
        if (i > 0 && cue->start_seconds < cues[i - 1].start_seconds) {
            return SCENE_SWITCH_ERROR_ORDER;
        }
        if (fabs(cue->start_seconds - cursor) > 0.001) return SCENE_SWITCH_ERROR_COVERAGE;
        cursor = cue->end_seconds;
        for (size_t previous = 0; previous < i; ++previous) {
            if (cues[previous].id == cue->id) return SCENE_SWITCH_ERROR_DUPLICATE_ID;
        }
    }
    if (fabs(cursor - duration_seconds) > 0.001) return SCENE_SWITCH_ERROR_COVERAGE;

    bool enabled = timeline->enabled;
    memcpy(timeline->cues, cues, count*sizeof(cues[0]));
    if (count < SCENE_SWITCH_CAPACITY) {
        memset(&timeline->cues[count], 0,
               (SCENE_SWITCH_CAPACITY - count)*sizeof(timeline->cues[0]));
    }
    timeline->count = count;
    timeline->enabled = enabled;
    timeline->active_index = SIZE_MAX;
    return SCENE_SWITCH_OK;
}

Scene_Switch_Result scene_switch_update(Scene_Switch_Timeline *timeline,
                                        double time_seconds,
                                        uint32_t *scene_index)
{
    if (timeline == NULL || scene_index == NULL) return SCENE_SWITCH_ERROR_NULL;
    if (!isfinite(time_seconds) || time_seconds < 0.0) return SCENE_SWITCH_ERROR_CUE;
    if (!timeline->enabled || timeline->count == 0) {
        timeline->active_index = SIZE_MAX;
        return SCENE_SWITCH_NO_CHANGE;
    }
    size_t selected = SIZE_MAX;
    for (size_t i = 0; i < timeline->count; ++i) {
        if (timeline->cues[i].start_seconds <= time_seconds &&
            time_seconds < timeline->cues[i].end_seconds) {
            selected = i;
            break;
        }
    }
    if (selected == SIZE_MAX || selected == timeline->active_index) {
        return SCENE_SWITCH_NO_CHANGE;
    }
    timeline->active_index = selected;
    *scene_index = timeline->cues[selected].scene_index;
    return SCENE_SWITCH_OK;
}

const char *scene_switch_result_string(Scene_Switch_Result result)
{
    static const char *const names[] = {
        "ok", "no change", "null input", "capacity violation", "invalid duration",
        "invalid cue", "cues are unsorted", "timeline coverage gap", "duplicate id",
    };
    if ((unsigned)result >= sizeof(names)/sizeof(names[0])) return "unknown scene switch error";
    return names[result];
}
