#ifndef MUSIALIZER_EVENT_TIMELINE_H_
#define MUSIALIZER_EVENT_TIMELINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Deliberately fixed: project loading and live recording can never grow the
// realtime state without a caller-visible overflow result.
#define EVENT_TIMELINE_CAPACITY 1024u
#define EVENT_VALUE_CAPACITY 4u

typedef enum Event_Type {
    EVENT_TYPE_LYRIC = 1,
    EVENT_TYPE_SEMANTIC = 2,
    EVENT_TYPE_CUE = 3,
    EVENT_TYPE_CUSTOM = 4,
    EVENT_TYPE_COUNT
} Event_Type;

typedef struct Event_Record {
    double timestamp_seconds;
    uint64_t id;
    uint32_t type;
    uint8_t value_count;
    uint8_t reserved[3];
    float values[EVENT_VALUE_CAPACITY];
} Event_Record;

typedef struct Event_Timeline {
    Event_Record events[EVENT_TIMELINE_CAPACITY];
    size_t count;
    uint64_t revision;
} Event_Timeline;

// A non-owning immutable view suitable for passing through Scene_Frame. The
// producer must keep the backing timeline alive for the duration of the frame.
typedef struct Event_Timeline_View {
    const Event_Record *events;
    size_t count;
} Event_Timeline_View;

typedef enum Event_Timeline_Result {
    EVENT_TIMELINE_OK = 0,
    EVENT_TIMELINE_DONE,
    EVENT_TIMELINE_ERROR_NULL,
    EVENT_TIMELINE_ERROR_MALFORMED,
    EVENT_TIMELINE_ERROR_DUPLICATE_ID,
    EVENT_TIMELINE_ERROR_OVERFLOW,
    EVENT_TIMELINE_ERROR_ORDER,
    EVENT_TIMELINE_ERROR_STALE_CURSOR
} Event_Timeline_Result;

typedef struct Event_Timeline_Cursor {
    const Event_Timeline *timeline;
    size_t index;
    uint64_t revision;
    double position_seconds;
} Event_Timeline_Cursor;

void event_timeline_init(Event_Timeline *timeline);
void event_timeline_clear(Event_Timeline *timeline);
bool event_record_is_valid(const Event_Record *event);
Event_Timeline_Result event_timeline_validate(const Event_Timeline *timeline);

// Recording inserts by the canonical (timestamp, type, id) key, so replay is
// deterministic regardless of the order in which producers submit events.
Event_Timeline_Result event_timeline_record(Event_Timeline *timeline,
                                            const Event_Record *event);
// Atomically replace validated contents while advancing the destination's
// revision even when source and destination contain the same number of events.
Event_Timeline_Result event_timeline_replace(Event_Timeline *destination,
                                             const Event_Timeline *source);
Event_Timeline_View event_timeline_view(const Event_Timeline *timeline);

// Replay cursors are invalidated by subsequent recording. seek() deliberately
// rebinds a stale cursor to the timeline's newest revision.
Event_Timeline_Result event_timeline_cursor_begin(const Event_Timeline *timeline,
                                                  Event_Timeline_Cursor *cursor);
Event_Timeline_Result event_timeline_cursor_seek(Event_Timeline_Cursor *cursor,
                                                 double timestamp_seconds);
Event_Timeline_Result event_timeline_cursor_next_until(Event_Timeline_Cursor *cursor,
                                                       double inclusive_end_seconds,
                                                       const Event_Record **event);

const char *event_timeline_result_string(Event_Timeline_Result result);

#endif // MUSIALIZER_EVENT_TIMELINE_H_
