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
        if (cue->settings.count > SCENE_SETTINGS_MAX_CONTROLS ||
            (cue->settings.captured && cue->settings.count == 0) ||
            (!cue->settings.captured && cue->settings.count != 0)) {
            return SCENE_SWITCH_ERROR_CUE;
        }
        for (size_t setting = 0; setting < cue->settings.count; ++setting) {
            if (!isfinite(cue->settings.values[setting])) return SCENE_SWITCH_ERROR_CUE;
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

static uint64_t scene_switch_next_id(const Scene_Switch_Timeline *timeline)
{
    uint64_t next_id = 1;
    for (size_t index = 0; index < timeline->count; ++index) {
        if (timeline->cues[index].id >= next_id) {
            if (timeline->cues[index].id == UINT64_MAX) return 0;
            next_id = timeline->cues[index].id + 1;
        }
    }
    return next_id;
}

Scene_Switch_Result scene_switch_cue_at(Scene_Switch_Timeline *timeline,
                                        double time_seconds,
                                        double duration_seconds,
                                        uint32_t scene_index,
                                        uint32_t scene_count,
                                        float strength,
                                        const Scene_Settings_Snapshot *settings)
{
    if (timeline == NULL || settings == NULL) return SCENE_SWITCH_ERROR_NULL;
    if (!isfinite(time_seconds) || !isfinite(duration_seconds) ||
        duration_seconds <= 0.0 || time_seconds < 0.0 ||
        time_seconds >= duration_seconds || scene_index >= scene_count ||
        !isfinite(strength) || strength < 0.0f || strength > 1.0f ||
        !settings->captured || settings->count == 0 ||
        settings->count > SCENE_SETTINGS_MAX_CONTROLS) return SCENE_SWITCH_ERROR_CUE;
    for (size_t index = 0; index < settings->count; ++index) {
        if (!isfinite(settings->values[index])) return SCENE_SWITCH_ERROR_CUE;
    }

    Scene_Switch_Cue staged[SCENE_SWITCH_CAPACITY];
    size_t count = timeline->count;
    if (count == 0) {
        uint64_t id = scene_switch_next_id(timeline);
        if (id == 0) return SCENE_SWITCH_ERROR_DUPLICATE_ID;
        staged[0] = (Scene_Switch_Cue) {
            .id = id,
            .start_seconds = 0.0,
            .end_seconds = duration_seconds,
            .scene_index = scene_index,
            .strength = strength,
            .settings = *settings,
        };
        count = 1;
    } else {
        memcpy(staged, timeline->cues, count*sizeof(staged[0]));
        size_t selected = SIZE_MAX;
        for (size_t index = 0; index < count; ++index) {
            if (staged[index].start_seconds <= time_seconds &&
                time_seconds < staged[index].end_seconds) {
                selected = index;
                break;
            }
        }
        if (selected == SIZE_MAX) return SCENE_SWITCH_ERROR_COVERAGE;
        if (fabs(staged[selected].start_seconds - time_seconds) <= 0.001) {
            staged[selected].scene_index = scene_index;
            staged[selected].strength = strength;
            staged[selected].settings = *settings;
        } else {
            if (count >= SCENE_SWITCH_CAPACITY) return SCENE_SWITCH_ERROR_CAPACITY;
            uint64_t id = scene_switch_next_id(timeline);
            if (id == 0) return SCENE_SWITCH_ERROR_DUPLICATE_ID;
            memmove(&staged[selected + 2], &staged[selected + 1],
                    (count - selected - 1)*sizeof(staged[0]));
            Scene_Switch_Cue next = staged[selected];
            staged[selected].end_seconds = time_seconds;
            next.id = id;
            next.start_seconds = time_seconds;
            next.scene_index = scene_index;
            next.strength = strength;
            next.settings = *settings;
            staged[selected + 1] = next;
            count++;
        }
    }
    Scene_Switch_Result result = scene_switch_replace(
        timeline, staged, count, duration_seconds, scene_count);
    if (result == SCENE_SWITCH_OK) timeline->enabled = true;
    return result;
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
