#include "event_timeline.h"

#include <math.h>
#include <string.h>

static int event_compare(const Event_Record *left, const Event_Record *right)
{
    if (left->timestamp_seconds < right->timestamp_seconds) return -1;
    if (left->timestamp_seconds > right->timestamp_seconds) return 1;
    if (left->type < right->type) return -1;
    if (left->type > right->type) return 1;
    if (left->id < right->id) return -1;
    if (left->id > right->id) return 1;
    return 0;
}

void event_timeline_init(Event_Timeline *timeline)
{
    if (timeline == NULL) return;
    memset(timeline, 0, sizeof(*timeline));
    timeline->revision = 1;
}

void event_timeline_clear(Event_Timeline *timeline)
{
    if (timeline == NULL) return;
    timeline->count = 0;
    timeline->revision += 1;
    if (timeline->revision == 0) timeline->revision = 1;
}

bool event_record_is_valid(const Event_Record *event)
{
    if (event == NULL || !isfinite(event->timestamp_seconds) ||
        event->timestamp_seconds < 0.0 || event->id == 0 ||
        event->type < EVENT_TYPE_LYRIC || event->type >= EVENT_TYPE_COUNT ||
        event->value_count == 0 || event->value_count > EVENT_VALUE_CAPACITY) {
        return false;
    }
    for (size_t i = 0; i < event->value_count; ++i) {
        if (!isfinite(event->values[i])) return false;
    }
    return true;
}

Event_Timeline_Result event_timeline_validate(const Event_Timeline *timeline)
{
    if (timeline == NULL) return EVENT_TIMELINE_ERROR_NULL;
    if (timeline->count > EVENT_TIMELINE_CAPACITY) return EVENT_TIMELINE_ERROR_OVERFLOW;
    for (size_t i = 0; i < timeline->count; ++i) {
        if (!event_record_is_valid(&timeline->events[i])) return EVENT_TIMELINE_ERROR_MALFORMED;
        if (i > 0 && event_compare(&timeline->events[i - 1], &timeline->events[i]) >= 0) {
            if (timeline->events[i - 1].id == timeline->events[i].id) {
                return EVENT_TIMELINE_ERROR_DUPLICATE_ID;
            }
            return EVENT_TIMELINE_ERROR_ORDER;
        }
        for (size_t previous = 0; previous < i; ++previous) {
            if (timeline->events[previous].id == timeline->events[i].id) {
                return EVENT_TIMELINE_ERROR_DUPLICATE_ID;
            }
        }
    }
    return EVENT_TIMELINE_OK;
}

Event_Timeline_Result event_timeline_record(Event_Timeline *timeline,
                                            const Event_Record *event)
{
    if (timeline == NULL || event == NULL) return EVENT_TIMELINE_ERROR_NULL;
    if (!event_record_is_valid(event)) return EVENT_TIMELINE_ERROR_MALFORMED;
    if (timeline->count > EVENT_TIMELINE_CAPACITY) return EVENT_TIMELINE_ERROR_OVERFLOW;

    size_t low = 0;
    size_t high = timeline->count;
    while (low < high) {
        size_t middle = low + (high - low)/2;
        if (event_compare(&timeline->events[middle], event) < 0) low = middle + 1;
        else high = middle;
    }
    for (size_t i = 0; i < timeline->count; ++i) {
        if (timeline->events[i].id == event->id) return EVENT_TIMELINE_ERROR_DUPLICATE_ID;
    }
    if (timeline->count == EVENT_TIMELINE_CAPACITY) return EVENT_TIMELINE_ERROR_OVERFLOW;

    memmove(&timeline->events[low + 1], &timeline->events[low],
            (timeline->count - low)*sizeof(timeline->events[0]));
    timeline->events[low] = *event;
    timeline->count += 1;
    timeline->revision += 1;
    if (timeline->revision == 0) timeline->revision = 1;
    return EVENT_TIMELINE_OK;
}

Event_Timeline_Result event_timeline_replace(Event_Timeline *destination,
                                             const Event_Timeline *source)
{
    if (destination == NULL || source == NULL) return EVENT_TIMELINE_ERROR_NULL;
    Event_Timeline_Result valid = event_timeline_validate(source);
    if (valid != EVENT_TIMELINE_OK) return valid;
    uint64_t revision = destination->revision + 1;
    if (revision == 0) revision = 1;
    if (destination != source) memcpy(destination, source, sizeof(*destination));
    destination->revision = revision;
    return EVENT_TIMELINE_OK;
}

Event_Timeline_View event_timeline_view(const Event_Timeline *timeline)
{
    if (timeline == NULL || timeline->count > EVENT_TIMELINE_CAPACITY) {
        return (Event_Timeline_View) {0};
    }
    return (Event_Timeline_View) { .events = timeline->events, .count = timeline->count };
}

Event_Timeline_Result event_timeline_cursor_begin(const Event_Timeline *timeline,
                                                  Event_Timeline_Cursor *cursor)
{
    if (timeline == NULL || cursor == NULL) return EVENT_TIMELINE_ERROR_NULL;
    Event_Timeline_Result valid = event_timeline_validate(timeline);
    if (valid != EVENT_TIMELINE_OK) return valid;
    *cursor = (Event_Timeline_Cursor) {
        .timeline = timeline,
        .revision = timeline->revision,
        .position_seconds = 0.0,
    };
    return EVENT_TIMELINE_OK;
}

Event_Timeline_Result event_timeline_cursor_seek(Event_Timeline_Cursor *cursor,
                                                 double timestamp_seconds)
{
    if (cursor == NULL || cursor->timeline == NULL) return EVENT_TIMELINE_ERROR_NULL;
    if (!isfinite(timestamp_seconds) || timestamp_seconds < 0.0) {
        return EVENT_TIMELINE_ERROR_MALFORMED;
    }
    Event_Timeline_Result valid = event_timeline_validate(cursor->timeline);
    if (valid != EVENT_TIMELINE_OK) return valid;

    size_t low = 0;
    size_t high = cursor->timeline->count;
    while (low < high) {
        size_t middle = low + (high - low)/2;
        if (cursor->timeline->events[middle].timestamp_seconds < timestamp_seconds) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    cursor->index = low;
    cursor->revision = cursor->timeline->revision;
    cursor->position_seconds = timestamp_seconds;
    return EVENT_TIMELINE_OK;
}

Event_Timeline_Result event_timeline_cursor_next_until(Event_Timeline_Cursor *cursor,
                                                       double inclusive_end_seconds,
                                                       const Event_Record **event)
{
    if (event != NULL) *event = NULL;
    if (cursor == NULL || cursor->timeline == NULL || event == NULL) {
        return EVENT_TIMELINE_ERROR_NULL;
    }
    if (!isfinite(inclusive_end_seconds) || inclusive_end_seconds < cursor->position_seconds) {
        return EVENT_TIMELINE_ERROR_MALFORMED;
    }
    if (cursor->revision != cursor->timeline->revision) {
        return EVENT_TIMELINE_ERROR_STALE_CURSOR;
    }
    if (cursor->index >= cursor->timeline->count ||
        cursor->timeline->events[cursor->index].timestamp_seconds > inclusive_end_seconds) {
        cursor->position_seconds = inclusive_end_seconds;
        return EVENT_TIMELINE_DONE;
    }
    *event = &cursor->timeline->events[cursor->index++];
    cursor->position_seconds = (*event)->timestamp_seconds;
    return EVENT_TIMELINE_OK;
}

const char *event_timeline_result_string(Event_Timeline_Result result)
{
    switch (result) {
    case EVENT_TIMELINE_OK: return "ok";
    case EVENT_TIMELINE_DONE: return "done";
    case EVENT_TIMELINE_ERROR_NULL: return "null argument";
    case EVENT_TIMELINE_ERROR_MALFORMED: return "malformed event or time";
    case EVENT_TIMELINE_ERROR_DUPLICATE_ID: return "duplicate event id";
    case EVENT_TIMELINE_ERROR_OVERFLOW: return "timeline capacity exceeded";
    case EVENT_TIMELINE_ERROR_ORDER: return "events are not canonically ordered";
    case EVENT_TIMELINE_ERROR_STALE_CURSOR: return "timeline changed during replay";
    }
    return "unknown event timeline result";
}
