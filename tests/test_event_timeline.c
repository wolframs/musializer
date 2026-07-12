#include "event_timeline.h"
#include "test_support.h"

#include <math.h>

static Event_Record make_event(double time, uint64_t id, uint32_t type, float value)
{
    return (Event_Record) {
        .timestamp_seconds = time,
        .id = id,
        .type = type,
        .value_count = 1,
        .values = { value },
    };
}

TEST(event_timeline_rejects_malformed_records)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    Event_Record event = make_event(1.0, 1, EVENT_TYPE_CUE, 0.5f);

    event.timestamp_seconds = NAN;
    EXPECT_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_ERROR_MALFORMED);
    event = make_event(-1.0, 1, EVENT_TYPE_CUE, 0.5f);
    EXPECT_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_ERROR_MALFORMED);
    event = make_event(1.0, 0, EVENT_TYPE_CUE, 0.5f);
    EXPECT_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_ERROR_MALFORMED);
    event = make_event(1.0, 1, 0, 0.5f);
    EXPECT_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_ERROR_MALFORMED);
    event = make_event(1.0, 1, EVENT_TYPE_CUE, INFINITY);
    EXPECT_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_ERROR_MALFORMED);
    event = make_event(1.0, 1, EVENT_TYPE_CUE, 0.5f);
    event.value_count = EVENT_VALUE_CAPACITY + 1;
    EXPECT_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_ERROR_MALFORMED);
    EXPECT_EQ_SIZE(timeline.count, 0);
}

TEST(event_timeline_records_in_canonical_order)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    Event_Record events[] = {
        make_event(2.0, 40, EVENT_TYPE_CUSTOM, 4.0f),
        make_event(1.0, 30, EVENT_TYPE_CUE, 3.0f),
        make_event(1.0, 20, EVENT_TYPE_LYRIC, 2.0f),
        make_event(1.0, 10, EVENT_TYPE_LYRIC, 1.0f),
    };
    for (size_t i = 0; i < sizeof(events)/sizeof(events[0]); ++i) {
        EXPECT_TRUE(event_timeline_record(&timeline, &events[i]) == EVENT_TIMELINE_OK);
    }
    EXPECT_TRUE(event_timeline_validate(&timeline) == EVENT_TIMELINE_OK);
    EXPECT_EQ_U64(timeline.events[0].id, 10);
    EXPECT_EQ_U64(timeline.events[1].id, 20);
    EXPECT_EQ_U64(timeline.events[2].id, 30);
    EXPECT_EQ_U64(timeline.events[3].id, 40);

    Event_Record duplicate = make_event(4.0, 20, EVENT_TYPE_SEMANTIC, 9.0f);
    EXPECT_TRUE(event_timeline_record(&timeline, &duplicate) == EVENT_TIMELINE_ERROR_DUPLICATE_ID);
}

TEST(event_timeline_reports_overflow_without_mutation)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    for (size_t i = 0; i < EVENT_TIMELINE_CAPACITY; ++i) {
        Event_Record event = make_event((double)i/10.0, (uint64_t)i + 1,
                                        EVENT_TYPE_CUSTOM, (float)i);
        REQUIRE_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_OK);
    }
    uint64_t revision = timeline.revision;
    Event_Record extra = make_event(999.0, EVENT_TIMELINE_CAPACITY + 1,
                                    EVENT_TYPE_CUSTOM, 1.0f);
    EXPECT_TRUE(event_timeline_record(&timeline, &extra) == EVENT_TIMELINE_ERROR_OVERFLOW);
    EXPECT_EQ_SIZE(timeline.count, EVENT_TIMELINE_CAPACITY);
    EXPECT_EQ_U64(timeline.revision, revision);
}

TEST(event_timeline_seek_and_replay_are_inclusive_and_repeatable)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    Event_Record a = make_event(0.5, 1, EVENT_TYPE_LYRIC, 1.0f);
    Event_Record b = make_event(1.0, 2, EVENT_TYPE_CUE, 2.0f);
    Event_Record c = make_event(1.0, 3, EVENT_TYPE_CUSTOM, 3.0f);
    Event_Record d = make_event(2.0, 4, EVENT_TYPE_SEMANTIC, 4.0f);
    REQUIRE_TRUE(event_timeline_record(&timeline, &d) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&timeline, &b) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&timeline, &a) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&timeline, &c) == EVENT_TIMELINE_OK);

    Event_Timeline_Cursor cursor;
    REQUIRE_TRUE(event_timeline_cursor_begin(&timeline, &cursor) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_cursor_seek(&cursor, 1.0) == EVENT_TIMELINE_OK);
    const Event_Record *event = NULL;
    REQUIRE_TRUE(event_timeline_cursor_next_until(&cursor, 1.0, &event) == EVENT_TIMELINE_OK);
    EXPECT_EQ_U64(event->id, 2);
    REQUIRE_TRUE(event_timeline_cursor_next_until(&cursor, 1.0, &event) == EVENT_TIMELINE_OK);
    EXPECT_EQ_U64(event->id, 3);
    EXPECT_TRUE(event_timeline_cursor_next_until(&cursor, 1.0, &event) == EVENT_TIMELINE_DONE);
    REQUIRE_TRUE(event_timeline_cursor_seek(&cursor, 0.0) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_cursor_next_until(&cursor, 0.5, &event) == EVENT_TIMELINE_OK);
    EXPECT_EQ_U64(event->id, 1);
}

TEST(event_timeline_replay_detects_recording_and_seek_rebinds)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    Event_Record a = make_event(1.0, 1, EVENT_TYPE_CUE, 1.0f);
    REQUIRE_TRUE(event_timeline_record(&timeline, &a) == EVENT_TIMELINE_OK);
    Event_Timeline_Cursor cursor;
    REQUIRE_TRUE(event_timeline_cursor_begin(&timeline, &cursor) == EVENT_TIMELINE_OK);

    Event_Record b = make_event(2.0, 2, EVENT_TYPE_CUE, 2.0f);
    REQUIRE_TRUE(event_timeline_record(&timeline, &b) == EVENT_TIMELINE_OK);
    const Event_Record *event = NULL;
    EXPECT_TRUE(event_timeline_cursor_next_until(&cursor, 2.0, &event) ==
                EVENT_TIMELINE_ERROR_STALE_CURSOR);
    REQUIRE_TRUE(event_timeline_cursor_seek(&cursor, 2.0) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_cursor_next_until(&cursor, 2.0, &event) == EVENT_TIMELINE_OK);
    EXPECT_EQ_U64(event->id, 2);
}

TEST(event_timeline_validation_catches_corrupt_order_and_count)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    timeline.count = 2;
    timeline.events[0] = make_event(2.0, 1, EVENT_TYPE_CUE, 1.0f);
    timeline.events[1] = make_event(1.0, 2, EVENT_TYPE_CUE, 2.0f);
    EXPECT_TRUE(event_timeline_validate(&timeline) == EVENT_TIMELINE_ERROR_ORDER);
    timeline.count = EVENT_TIMELINE_CAPACITY + 1;
    EXPECT_TRUE(event_timeline_validate(&timeline) == EVENT_TIMELINE_ERROR_OVERFLOW);
}

TEST(event_timeline_view_is_bounded_and_read_only)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    Event_Record event = make_event(3.0, 77, EVENT_TYPE_SEMANTIC, 0.75f);
    REQUIRE_TRUE(event_timeline_record(&timeline, &event) == EVENT_TIMELINE_OK);
    Event_Timeline_View view = event_timeline_view(&timeline);
    EXPECT_EQ_SIZE(view.count, 1);
    EXPECT_EQ_U64(view.events[0].id, 77);
    timeline.count = EVENT_TIMELINE_CAPACITY + 1;
    view = event_timeline_view(&timeline);
    EXPECT_EQ_SIZE(view.count, 0);
    EXPECT_TRUE(view.events == NULL);
}

TEST(event_timeline_replace_invalidates_same_count_different_values)
{
    Event_Timeline destination;
    Event_Timeline source;
    event_timeline_init(&destination);
    event_timeline_init(&source);
    Event_Record old_event = make_event(1.0, 7, EVENT_TYPE_SEMANTIC, 0.2f);
    Event_Record new_event = make_event(1.0, 7, EVENT_TYPE_SEMANTIC, 0.9f);
    REQUIRE_TRUE(event_timeline_record(&destination, &old_event) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&source, &new_event) == EVENT_TIMELINE_OK);
    uint64_t previous_revision = destination.revision;
    REQUIRE_TRUE(event_timeline_replace(&destination, &source) == EVENT_TIMELINE_OK);
    EXPECT_TRUE(destination.revision != previous_revision);
    EXPECT_EQ_SIZE(destination.count, 1);
    EXPECT_NEAR(destination.events[0].values[0], 0.9, 0.000001);
}
