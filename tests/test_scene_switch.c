#include "scene_switch.h"
#include "scene_settings.h"
#include "test_support.h"

#include <math.h>

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

// --- Editing existing cues -------------------------------------------------
//
// The timeline must stay sorted, contiguous and cover [0, duration] exactly, so
// no edit can simply drop or move a span: each one has to leave a neighbour
// covering the vacated time. Every case below also checks that a rejected edit
// left the timeline untouched.

TEST(scene_switch_remove_hands_the_span_to_the_previous_cue)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 0), cue(2, 4, 7, 1), cue(3, 7, 10, 2)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 3, 10, 10) == SCENE_SWITCH_OK);

    REQUIRE_TRUE(scene_switch_remove(&timeline, 1, 10, 10) == SCENE_SWITCH_OK);
    EXPECT_EQ_SIZE(timeline.count, 2);
    EXPECT_EQ_U64(timeline.cues[0].id, 1);
    EXPECT_NEAR(timeline.cues[0].end_seconds, 7.0, 0.0);
    EXPECT_EQ_U64(timeline.cues[1].id, 3);
    EXPECT_NEAR(timeline.cues[1].start_seconds, 7.0, 0.0);
}

TEST(scene_switch_remove_the_first_cue_pulls_the_next_one_back_to_zero)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 0), cue(2, 4, 10, 1)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 10) == SCENE_SWITCH_OK);

    // Nothing precedes cue 0, so the successor has to absorb the span or the
    // timeline would start at 4 s and fail its own coverage check.
    REQUIRE_TRUE(scene_switch_remove(&timeline, 0, 10, 10) == SCENE_SWITCH_OK);
    EXPECT_EQ_SIZE(timeline.count, 1);
    EXPECT_EQ_U64(timeline.cues[0].id, 2);
    EXPECT_NEAR(timeline.cues[0].start_seconds, 0.0, 0.0);
    EXPECT_NEAR(timeline.cues[0].end_seconds, 10.0, 0.0);
}

TEST(scene_switch_remove_the_only_cue_clears_and_disables_the_plan)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 10, 3)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 1, 10, 10) == SCENE_SWITCH_OK);
    timeline.enabled = true;

    REQUIRE_TRUE(scene_switch_remove(&timeline, 0, 10, 10) == SCENE_SWITCH_OK);
    EXPECT_EQ_SIZE(timeline.count, 0);
    // An enabled plan with no cues would leave the Auto scenes toggle on while
    // nothing ever switches.
    EXPECT_FALSE(timeline.enabled);

    uint32_t scene = 99;
    EXPECT_TRUE(scene_switch_update(&timeline, 1.0, &scene) == SCENE_SWITCH_NO_CHANGE);
    EXPECT_EQ_U64(scene, 99);
}

TEST(scene_switch_remove_rejects_a_cue_that_is_not_there)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 0), cue(2, 4, 10, 1)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 10) == SCENE_SWITCH_OK);

    EXPECT_TRUE(scene_switch_remove(&timeline, 2, 10, 10) == SCENE_SWITCH_ERROR_INDEX);
    EXPECT_TRUE(scene_switch_remove(&timeline, SIZE_MAX, 10, 10) == SCENE_SWITCH_ERROR_INDEX);
    EXPECT_TRUE(scene_switch_remove(NULL, 0, 10, 10) == SCENE_SWITCH_ERROR_NULL);
    EXPECT_EQ_SIZE(timeline.count, 2);
    EXPECT_NEAR(timeline.cues[0].end_seconds, 4.0, 0.0);
}

TEST(scene_switch_retime_moves_the_boundary_shared_by_two_cues)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 0), cue(2, 4, 10, 1)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 10) == SCENE_SWITCH_OK);

    REQUIRE_TRUE(scene_switch_retime(&timeline, 1, 6.5, 10, 10) == SCENE_SWITCH_OK);
    // One boundary, two cues: the predecessor has to end where the successor
    // begins or coverage breaks.
    EXPECT_NEAR(timeline.cues[0].end_seconds, 6.5, 0.0);
    EXPECT_NEAR(timeline.cues[1].start_seconds, 6.5, 0.0);
    EXPECT_EQ_SIZE(timeline.count, 2);
}

TEST(scene_switch_retime_refuses_to_move_the_opening_cue)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 0), cue(2, 4, 10, 1)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 10) == SCENE_SWITCH_OK);

    // Cue 0 starts at 0.0 by construction. Reporting that beats silently
    // succeeding and producing a timeline that no longer starts at the track.
    EXPECT_TRUE(scene_switch_retime(&timeline, 0, 1.0, 10, 10) == SCENE_SWITCH_ERROR_BOUNDARY);
    EXPECT_NEAR(timeline.cues[0].start_seconds, 0.0, 0.0);
}

TEST(scene_switch_retime_leaves_both_neighbours_usable)
{
    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 0), cue(2, 4, 10, 1)};
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 10) == SCENE_SWITCH_OK);

    // Past the successor's end, and back onto the predecessor's start.
    EXPECT_TRUE(scene_switch_retime(&timeline, 1, 10.0, 10, 10) == SCENE_SWITCH_ERROR_BOUNDARY);
    EXPECT_TRUE(scene_switch_retime(&timeline, 1, 0.0, 10, 10) == SCENE_SWITCH_ERROR_BOUNDARY);
    EXPECT_TRUE(scene_switch_retime(&timeline, 1, -1.0, 10, 10) == SCENE_SWITCH_ERROR_BOUNDARY);
    EXPECT_TRUE(scene_switch_retime(&timeline, 1, NAN, 10, 10) == SCENE_SWITCH_ERROR_CUE);
    EXPECT_TRUE(scene_switch_retime(&timeline, 1, INFINITY, 10, 10) == SCENE_SWITCH_ERROR_CUE);
    EXPECT_TRUE(scene_switch_retime(&timeline, 5, 5.0, 10, 10) == SCENE_SWITCH_ERROR_INDEX);

    // Every rejection above left the timeline exactly as it was.
    EXPECT_NEAR(timeline.cues[0].end_seconds, 4.0, 0.0);
    EXPECT_NEAR(timeline.cues[1].start_seconds, 4.0, 0.0);

    // A boundary a hair inside the neighbour is still refused, so an edit can
    // never leave a span too thin for the 0.001 s coverage tolerance to resolve.
    EXPECT_TRUE(scene_switch_retime(&timeline, 1, 10.0 - SCENE_SWITCH_MIN_CUE_SECONDS/2.0,
                                    10, 10) == SCENE_SWITCH_ERROR_BOUNDARY);
}

TEST(scene_switch_retarget_clears_the_tuning_when_no_snapshot_is_given)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    Scene_Settings_Snapshot ascii;
    REQUIRE_TRUE(scene_settings_capture(&settings, 3, &ascii));

    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 4, 3), cue(2, 4, 10, 1)};
    cues[0].settings = ascii;
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 2, 10, 10) == SCENE_SWITCH_OK);
    REQUIRE_TRUE(timeline.cues[0].settings.captured);

    // NULL is the safe default: the scene's own values apply rather than the
    // previous scene's numbers being reinterpreted control for control.
    REQUIRE_TRUE(scene_switch_retarget(&timeline, 0, 4, NULL, 10, 10) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(timeline.cues[0].scene_index, 4);
    EXPECT_FALSE(timeline.cues[0].settings.captured);
    EXPECT_EQ_SIZE(timeline.cues[0].settings.count, 0);
    // Spans are untouched by a retarget.
    EXPECT_NEAR(timeline.cues[0].end_seconds, 4.0, 0.0);
}

TEST(scene_switch_retarget_rejects_a_snapshot_shaped_for_another_scene)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    Scene_Settings_Snapshot ascii;      // scene 3: 6 controls
    Scene_Settings_Snapshot atlas;      // scene 4: 12 controls
    REQUIRE_TRUE(scene_settings_capture(&settings, 3, &ascii));
    REQUIRE_TRUE(scene_settings_capture(&settings, 4, &atlas));

    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 10, 3)};
    cues[0].settings = ascii;
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 1, 10, 10) == SCENE_SWITCH_OK);

    EXPECT_TRUE(scene_switch_retarget(&timeline, 0, 4, &ascii, 10, 10) ==
                SCENE_SWITCH_ERROR_SETTINGS);
    EXPECT_EQ_U64(timeline.cues[0].scene_index, 3);

    REQUIRE_TRUE(scene_switch_retarget(&timeline, 0, 4, &atlas, 10, 10) == SCENE_SWITCH_OK);
    EXPECT_EQ_U64(timeline.cues[0].scene_index, 4);
    EXPECT_TRUE(timeline.cues[0].settings.captured);
    EXPECT_EQ_SIZE(timeline.cues[0].settings.count, 12);

    EXPECT_TRUE(scene_switch_retarget(&timeline, 0, 10, NULL, 10, 10) ==
                SCENE_SWITCH_ERROR_CUE);
    EXPECT_TRUE(scene_switch_retarget(&timeline, 3, 1, NULL, 10, 10) ==
                SCENE_SWITCH_ERROR_INDEX);
    EXPECT_TRUE(scene_switch_retarget(NULL, 0, 1, NULL, 10, 10) ==
                SCENE_SWITCH_ERROR_NULL);
}

TEST(scene_switch_retarget_cannot_catch_a_same_shaped_carry)
{
    // Pins a known limit rather than a desired behaviour. Scene 5 (Spectral
    // Terrarium) and scene 0 (Spectrum) both expose 8 controls and their
    // default values are in range for each other, so the snapshot check cannot
    // tell that these numbers were captured for a different scene: they are
    // accepted and reinterpreted control for control.
    //
    // If a future control-table change makes this pair distinguishable, this
    // test failing is good news -- confirm the new shapes and update
    // scene_switch_retarget's header, which promises callers only that a
    // differently shaped carry is rejected.
    Scene_Settings settings;
    scene_settings_init(&settings);
    Scene_Settings_Snapshot terrarium;
    REQUIRE_TRUE(scene_settings_capture(&settings, 5, &terrarium));
    REQUIRE_TRUE(scene_settings_snapshot_valid(0, &terrarium));

    Scene_Switch_Timeline timeline;
    scene_switch_init(&timeline);
    Scene_Switch_Cue cues[] = {cue(1, 0, 10, 5)};
    cues[0].settings = terrarium;
    REQUIRE_TRUE(scene_switch_replace(&timeline, cues, 1, 10, 10) == SCENE_SWITCH_OK);

    EXPECT_TRUE(scene_switch_retarget(&timeline, 0, 0, &terrarium, 10, 10) ==
                SCENE_SWITCH_OK);
    // Hence the header's instruction: capture from the target scene, or pass
    // NULL. This is why the UI must never reuse the outgoing cue's snapshot.
    EXPECT_EQ_U64(timeline.cues[0].scene_index, 0);
}
