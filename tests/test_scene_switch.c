#include "scene_switch.h"
#include "test_support.h"

static Scene_Switch_Cue cue(uint64_t id, double start, double end, uint32_t scene)
{
    return (Scene_Switch_Cue) {
        .id = id, .start_seconds = start, .end_seconds = end,
        .scene_index = scene, .strength = 0.5f,
    };
}

TEST(scene_switch_requires_sorted_contiguous_full_coverage)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue valid[] = {cue(1, 0, 5, 0), cue(2, 5, 10, 1)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, valid, 2, 10, 7) == SCENE_SWITCH_OK);
    EXPECT_EQ_SIZE(timeline.count, 2);

    Scene_Switch_Cue gap[] = {cue(1, 0, 4, 0), cue(2, 5, 10, 1)};
    EXPECT_TRUE(scene_switch_replace(&timeline, gap, 2, 10, 7) ==
                SCENE_SWITCH_ERROR_COVERAGE);
    EXPECT_EQ_SIZE(timeline.count, 2);
    EXPECT_NEAR(timeline.cues[0].end_seconds, 5.0, 0.0);

    Scene_Switch_Cue duplicate[] = {cue(1, 0, 5, 0), cue(1, 5, 10, 1)};
    EXPECT_TRUE(scene_switch_replace(&timeline, duplicate, 2, 10, 7) ==
                SCENE_SWITCH_ERROR_DUPLICATE_ID);

    Scene_Switch_Cue malformed_snapshot[] = {cue(3, 0, 10, 0)};
    malformed_snapshot[0].settings.captured = true;
    EXPECT_TRUE(scene_switch_replace(&timeline, malformed_snapshot, 1, 10, 7) ==
                SCENE_SWITCH_ERROR_CUE);
}

TEST(scene_switch_is_opt_in_and_changes_only_at_section_boundaries)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 5, 2), cue(2, 5, 10, 6)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 7) == SCENE_SWITCH_OK);
    uint32_t scene = 99;
    EXPECT_TRUE(scene_switch_update(&timeline, 1, &scene) == SCENE_SWITCH_NO_CHANGE);
    EXPECT_EQ_U64(scene, 99);

    timeline.enabled = true;
    REQUIRE_TRUE(scene_switch_update(&timeline, 1, &scene) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(scene, 2);
    EXPECT_TRUE(scene_switch_update(&timeline, 4.9, &scene) == SCENE_SWITCH_NO_CHANGE);
    REQUIRE_TRUE(scene_switch_update(&timeline, 5, &scene) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(scene, 6);
}

TEST(scene_switch_seek_and_reset_rebind_deterministically)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {
        cue(1, 0, 3, 0), cue(2, 3, 7, 1), cue(3, 7, 10, 2),
    };
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 3, 10, 7) == SCENE_SWITCH_OK);
    timeline.enabled = true;
    uint32_t scene = 0;
    REQUIRE_TRUE(scene_switch_update(&timeline, 8, &scene) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(scene, 2);
    REQUIRE_TRUE(scene_switch_update(&timeline, 1, &scene) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(scene, 0);
    scene_switch_reset(&timeline);
    REQUIRE_TRUE(scene_switch_update(&timeline, 1, &scene) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(scene, 0);
}

TEST(scene_switch_cue_at_splits_and_replaces_with_parameter_snapshots)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Settings_Snapshot pulse = {
        .captured = true, .count = 5, .values = {0.8f, 30.0f, 0.5f, 1.0f, 1.2f},
    };
    Scene_Settings_Snapshot atlas = {
        .captured = true, .count = 8,
        .values = {1.2f, 2.4f, 1.0f, 1.3f, 1.0f, 20.0f, 0.8f, 1.0f},
    };
    REQUIRE_TRUE(scene_switch_cue_at(
        &timeline, 0.0, 10.0, 1, 7, 1.0f, &pulse) == SCENE_SWITCH_OK);
    EXPECT_TRUE(timeline.enabled);
    EXPECT_EQ_SIZE(timeline.count, 1);
    EXPECT_NEAR(timeline.cues[0].settings.values[1], 30.0f, 0.0f);

    REQUIRE_TRUE(scene_switch_cue_at(
        &timeline, 4.0, 10.0, 4, 7, 0.75f, &atlas) == SCENE_SWITCH_OK);
    EXPECT_EQ_SIZE(timeline.count, 2);
    EXPECT_NEAR(timeline.cues[0].end_seconds, 4.0, 0.0);
    EXPECT_NEAR(timeline.cues[1].start_seconds, 4.0, 0.0);
    EXPECT_EQ_U64(timeline.cues[1].scene_index, 4);
    EXPECT_NEAR(timeline.cues[1].settings.values[1], 2.4f, 0.0f);

    atlas.values[1] = 3.1f;
    REQUIRE_TRUE(scene_switch_cue_at(
        &timeline, 4.0, 10.0, 4, 7, 0.9f, &atlas) == SCENE_SWITCH_OK);
    EXPECT_EQ_SIZE(timeline.count, 2);
    EXPECT_NEAR(timeline.cues[1].settings.values[1], 3.1f, 0.0f);
}
