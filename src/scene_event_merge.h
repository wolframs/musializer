#ifndef MUSIALIZER_SCENE_EVENT_MERGE_H_
#define MUSIALIZER_SCENE_EVENT_MERGE_H_

#include "event_timeline.h"

// Manual and semantic lanes are independently bounded. The frame-facing view
// must therefore be able to carry both without dropping the second lane.
#define SCENE_EVENT_MERGE_CAPACITY (EVENT_TIMELINE_CAPACITY*2u)

typedef struct Scene_Event_Merge {
    Event_Record events[SCENE_EVENT_MERGE_CAPACITY];
    size_t count;
} Scene_Event_Merge;

// Build one deterministic, canonically ordered view. Manual IDs are preserved;
// semantic IDs are qualified into a separate display namespace so equal IDs in
// independently-authored lanes remain distinct to scene renderers.
Event_Timeline_Result scene_event_merge_build(
    Scene_Event_Merge *destination,
    const Event_Timeline *manual_events,
    const Event_Timeline *semantic_events);

Event_Timeline_View scene_event_merge_view(const Scene_Event_Merge *merge);

#endif // MUSIALIZER_SCENE_EVENT_MERGE_H_
