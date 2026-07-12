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
