#include "scene_event_merge.h"
#include "test_support.h"

#include <string.h>

static Event_Record make_event(double time, uint64_t id, uint32_t type)
{
    return (Event_Record){
        .timestamp_seconds = time,
        .id = id,
        .type = type,
        .value_count = 1,
        .values = {0.5f},
    };
}

TEST(scene_event_merge_keeps_equal_ids_from_independent_lanes)
{
    Event_Timeline manual;
    Event_Timeline semantic;
    event_timeline_init(&manual);
    event_timeline_init(&semantic);
    Event_Record authored = make_event(2.0, 7, EVENT_TYPE_CUSTOM);
    Event_Record inferred = make_event(1.0, 7, EVENT_TYPE_SEMANTIC);
    REQUIRE_TRUE(event_timeline_record(&manual, &authored) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&semantic, &inferred) == EVENT_TIMELINE_OK);

    Scene_Event_Merge merged;
    REQUIRE_TRUE(scene_event_merge_build(&merged, &manual, &semantic) ==
                 EVENT_TIMELINE_OK);
    EXPECT_EQ_SIZE(merged.count, 2);
    EXPECT_TRUE(merged.events[0].id != merged.events[1].id);
    EXPECT_TRUE(merged.events[0].type == EVENT_TYPE_SEMANTIC);
    EXPECT_EQ_U64(merged.events[1].id, 7);
}

TEST(scene_event_merge_keeps_two_full_lanes)
{
    Event_Timeline manual;
    Event_Timeline semantic;
    event_timeline_init(&manual);
    event_timeline_init(&semantic);
    for (size_t i = 0; i < EVENT_TIMELINE_CAPACITY; ++i) {
        Event_Record authored = make_event((double)i, (uint64_t)i + 1,
                                           EVENT_TYPE_CUSTOM);
        Event_Record inferred = make_event((double)i + 0.5, (uint64_t)i + 1,
                                           EVENT_TYPE_SEMANTIC);
        REQUIRE_TRUE(event_timeline_record(&manual, &authored) == EVENT_TIMELINE_OK);
        REQUIRE_TRUE(event_timeline_record(&semantic, &inferred) == EVENT_TIMELINE_OK);
    }

    Scene_Event_Merge merged;
    REQUIRE_TRUE(scene_event_merge_build(&merged, &manual, &semantic) ==
                 EVENT_TIMELINE_OK);
    EXPECT_EQ_SIZE(merged.count, SCENE_EVENT_MERGE_CAPACITY);
    Event_Timeline_View view = scene_event_merge_view(&merged);
    EXPECT_EQ_SIZE(view.count, SCENE_EVENT_MERGE_CAPACITY);
    for (size_t i = 1; i < view.count; ++i) {
        EXPECT_TRUE(view.events[i - 1].timestamp_seconds <=
                    view.events[i].timestamp_seconds);
    }
}

TEST(scene_event_merge_is_deterministic_and_rejects_invalid_sources_atomically)
{
    Event_Timeline manual;
    Event_Timeline semantic;
    event_timeline_init(&manual);
    event_timeline_init(&semantic);
    Event_Record authored = make_event(1.0, UINT64_C(0x8000000000000009),
                                       EVENT_TYPE_CUSTOM);
    Event_Record inferred = make_event(1.0, 9, EVENT_TYPE_SEMANTIC);
    REQUIRE_TRUE(event_timeline_record(&manual, &authored) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&semantic, &inferred) == EVENT_TIMELINE_OK);
    Scene_Event_Merge first;
    Scene_Event_Merge second;
    REQUIRE_TRUE(scene_event_merge_build(&first, &manual, &semantic) ==
                 EVENT_TIMELINE_OK);
    REQUIRE_TRUE(scene_event_merge_build(&second, &manual, &semantic) ==
                 EVENT_TIMELINE_OK);
    EXPECT_TRUE(memcmp(&first, &second, sizeof(first)) == 0);
    EXPECT_TRUE(first.events[0].id != authored.id);

    Scene_Event_Merge sentinel = first;
    semantic.events[0].timestamp_seconds = -1.0;
    EXPECT_TRUE(scene_event_merge_build(&first, &manual, &semantic) ==
                EVENT_TIMELINE_ERROR_MALFORMED);
    EXPECT_TRUE(memcmp(&first, &sentinel, sizeof(first)) == 0);
}
