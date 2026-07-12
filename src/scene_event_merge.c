#include "scene_event_merge.h"

#include <stdlib.h>
#include <string.h>

static int event_compare(const void *left_pointer, const void *right_pointer)
{
    const Event_Record *left = left_pointer;
    const Event_Record *right = right_pointer;
    if (left->timestamp_seconds < right->timestamp_seconds) return -1;
    if (left->timestamp_seconds > right->timestamp_seconds) return 1;
    if (left->type < right->type) return -1;
    if (left->type > right->type) return 1;
    if (left->id < right->id) return -1;
    if (left->id > right->id) return 1;
    return 0;
}

static bool id_is_used(const Scene_Event_Merge *merge, uint64_t id)
{
    for (size_t i = 0; i < merge->count; ++i) {
        if (merge->events[i].id == id) return true;
    }
    return false;
}

static uint64_t qualify_semantic_id(const Scene_Event_Merge *merge, uint64_t id)
{
    // XOR is a one-to-one lane transform. The bounded probe only handles an
    // authored ID already occupying the transformed value (or a prior probe).
    uint64_t qualified = id ^ UINT64_C(0x8000000000000000);
    if (qualified == 0) qualified = UINT64_C(0x8000000000000001);
    while (id_is_used(merge, qualified)) {
        qualified += UINT64_C(0x9E3779B97F4A7C15);
        if (qualified == 0) qualified = 1;
    }
    return qualified;
}

Event_Timeline_Result scene_event_merge_build(
    Scene_Event_Merge *destination,
    const Event_Timeline *manual_events,
    const Event_Timeline *semantic_events)
{
    if (destination == NULL || manual_events == NULL || semantic_events == NULL) {
        return EVENT_TIMELINE_ERROR_NULL;
    }
    Event_Timeline_Result manual_valid = event_timeline_validate(manual_events);
    if (manual_valid != EVENT_TIMELINE_OK) return manual_valid;
    Event_Timeline_Result semantic_valid = event_timeline_validate(semantic_events);
    if (semantic_valid != EVENT_TIMELINE_OK) return semantic_valid;
    if (manual_events->count + semantic_events->count >
        SCENE_EVENT_MERGE_CAPACITY) return EVENT_TIMELINE_ERROR_OVERFLOW;

    Scene_Event_Merge result = {0};
    if (manual_events->count > 0) {
        memcpy(result.events, manual_events->events,
               manual_events->count*sizeof(result.events[0]));
        result.count = manual_events->count;
    }
    for (size_t i = 0; i < semantic_events->count; ++i) {
        Event_Record event = semantic_events->events[i];
        event.id = qualify_semantic_id(&result, event.id);
        result.events[result.count++] = event;
    }
    qsort(result.events, result.count, sizeof(result.events[0]), event_compare);
    *destination = result;
    return EVENT_TIMELINE_OK;
}

Event_Timeline_View scene_event_merge_view(const Scene_Event_Merge *merge)
{
    if (merge == NULL || merge->count > SCENE_EVENT_MERGE_CAPACITY) {
        return (Event_Timeline_View){0};
    }
    return (Event_Timeline_View){.events = merge->events, .count = merge->count};
}
