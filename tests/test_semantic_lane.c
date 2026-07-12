#include "semantic_lane.h"
#include "test_support.h"

static Event_Record semantic(double time, uint64_t id,
                             float energy, float tension,
                             float valence, float confidence)
{
    return (Event_Record) {
        .timestamp_seconds = time,
        .id = id,
        .type = EVENT_TYPE_SEMANTIC,
        .value_count = 4,
        .values = {energy, tension, valence, confidence},
    };
}

TEST(semantic_lane_holds_latest_valid_interpretation)
{
    Event_Timeline timeline;
    event_timeline_init(&timeline);
    Event_Record first = semantic(1.0, 7, 0.2f, 0.3f, -0.4f, 0.8f);
    Event_Record second = semantic(3.0, 8, 0.9f, 0.7f, 0.6f, 1.0f);
    REQUIRE_TRUE(event_timeline_record(&timeline, &first) == EVENT_TIMELINE_OK);
    REQUIRE_TRUE(event_timeline_record(&timeline, &second) == EVENT_TIMELINE_OK);
    Semantic_Frame frame = {.available = true};
    EXPECT_FALSE(semantic_lane_sample(event_timeline_view(&timeline), 0.5, &frame));
    EXPECT_FALSE(frame.available);
    REQUIRE_TRUE(semantic_lane_sample(event_timeline_view(&timeline), 2.0, &frame));
    EXPECT_EQ_U64(frame.source_id, 7);
    EXPECT_NEAR(frame.valence, -0.4, 0.000001);
    REQUIRE_TRUE(semantic_lane_sample(event_timeline_view(&timeline), 4.0, &frame));
    EXPECT_EQ_U64(frame.source_id, 8);
    EXPECT_NEAR(frame.energy, 0.9, 0.000001);
}

TEST(semantic_lane_ignores_wrong_shape_and_preserves_output_on_bad_call)
{
    Event_Record wrong = semantic(0.0, 1, 0.5f, 0.5f, 0.0f, 1.0f);
    wrong.value_count = 1;
    Semantic_Frame frame = {.available = true, .source_id = 99};
    EXPECT_FALSE(semantic_lane_sample(
        (Event_Timeline_View){.events = &wrong, .count = 1}, 1.0, &frame));
    EXPECT_FALSE(frame.available);
    frame.source_id = 99;
    EXPECT_FALSE(semantic_lane_sample((Event_Timeline_View){0}, -1.0, &frame));
    EXPECT_EQ_U64(frame.source_id, 99);
}
