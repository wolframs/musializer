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
    SCENE_SWITCH_ERROR_DUPLICATE_ID,
    // Appended, never reordered: scene_switch_result_string indexes by value.
    SCENE_SWITCH_ERROR_INDEX,
    SCENE_SWITCH_ERROR_BOUNDARY,
    SCENE_SWITCH_ERROR_SETTINGS
} Scene_Switch_Result;

// Shortest span an editing operation will leave behind. The coverage checks in
// scene_switch_replace tolerate 0.001 s of drift, so a cue thinner than that
// cannot be positioned meaningfully even though `end > start` would pass.
// Deliberately enforced only by the editing entry points below: applying it to
// scene_switch_replace would reject already-saved projects.
#define SCENE_SWITCH_MIN_CUE_SECONDS 0.001

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
// Editing operations. Each stages a private copy, mutates it, and re-publishes
// through scene_switch_replace, so the sorted/contiguous/full-coverage checks
// run again on the result and a rejected edit leaves the timeline untouched.

// Drops a cue and hands its span to a neighbour, because the timeline has to
// stay contiguous over [0, duration]: the previous cue absorbs it, or the next
// one does when index is 0. Removing the only cue clears the plan and disables
// it -- a timeline with zero cues cannot be published.
Scene_Switch_Result scene_switch_remove(Scene_Switch_Timeline *timeline,
                                        size_t index,
                                        double duration_seconds,
                                        uint32_t scene_count);

// Moves the boundary between cue `index - 1` and cue `index`, shortening one and
// lengthening the other. Cue 0 begins at 0.0 by construction, so retiming it is
// SCENE_SWITCH_ERROR_BOUNDARY rather than a silent no-op.
Scene_Switch_Result scene_switch_retime(Scene_Switch_Timeline *timeline,
                                        size_t index,
                                        double start_seconds,
                                        double duration_seconds,
                                        uint32_t scene_count);

// Points a cue at a different scene. `settings` must be a snapshot captured
// from the NEW scene, or NULL to clear the cue's tuning so the scene's own
// values apply. NULL is the safe default and the old cue's snapshot must never
// be carried across: a snapshot is a bare value array whose meaning comes
// entirely from the scene it was captured for.
//
// This checks the snapshot against the target scene's control table, which
// rejects a carry between differently shaped scenes. It is NOT a complete
// guard: scenes 0, 1, 5, 6 and 9 all expose 8 controls, so a stale snapshot
// with in-range values passes validation and is reinterpreted control for
// control. Capturing from the target scene is the caller's responsibility.
Scene_Switch_Result scene_switch_retarget(Scene_Switch_Timeline *timeline,
                                          size_t index,
                                          uint32_t scene_index,
                                          const Scene_Settings_Snapshot *settings,
                                          double duration_seconds,
                                          uint32_t scene_count);

Scene_Switch_Result scene_switch_cue_at(Scene_Switch_Timeline *timeline,
                                        double time_seconds,
                                        double duration_seconds,
                                        uint32_t scene_index,
                                        uint32_t scene_count,
                                        float strength,
                                        const Scene_Settings_Snapshot *settings);
const char *scene_switch_result_string(Scene_Switch_Result result);

#endif // MUSIALIZER_SCENE_SWITCH_H_
