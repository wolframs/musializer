#include "route_editor_state.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

// Headless tests cannot include scene.h (raylib); these mirror the Scene_Id
// ordering that scene_settings.c's descriptor tables are built around.
enum { SCENE_SONG_ATLAS = 4, SCENE_LOOM = 8, COUNT_SCENES = 10 };

enum { TRACK_A = 0, TRACK_B = 1 };

static size_t setting_index_for(size_t scene_index, const char *key)
{
    size_t owner_scene = 0;
    size_t setting_index = 0;
    const Scene_Setting_Descriptor *descriptor =
        scene_settings_descriptor_by_key(key, &owner_scene, &setting_index);
    if (descriptor == NULL || owner_scene != scene_index) {
        TEST_FAIL("descriptor lookup failed for %s", key);
    }
    return setting_index;
}

static Musi_Parameter_Mapping committed_band_route(void)
{
    Musi_Parameter_Mapping route = {0};
    strncpy(route.parameter, "settings.loom.weight",
            sizeof(route.parameter) - 1U);
    route.source = MUSI_ANALYSIS_BAND;
    route.band_index = 2;
    route.input_min = 0.0;
    route.input_max = 1.0;
    route.output_min = 0.4;
    route.output_max = 2.2;
    route.interpolation = MUSI_INTERPOLATION_SMOOTHSTEP;
    route.clamp = true;
    return route;
}

TEST(route_editor_open_seeds_full_range_draft)
{
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_FALSE(route_editor_is_open(&state));

    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, NULL));
    EXPECT_TRUE(route_editor_is_open(&state));
    EXPECT_TRUE(route_editor_targets(&state, TRACK_A, SCENE_LOOM, weight));
    EXPECT_FALSE(route_editor_targets(&state, TRACK_B, SCENE_LOOM, weight));
    EXPECT_FALSE(route_editor_matches_context(&state, TRACK_A, SCENE_SONG_ATLAS));

    EXPECT_TRUE(strcmp(state.draft.parameter, "settings.loom.weight") == 0);
    EXPECT_TRUE(state.draft.source == MUSI_ANALYSIS_RMS);
    EXPECT_EQ_SIZE((size_t)state.draft.band_index, 0);
    EXPECT_NEAR(state.draft.input_min, 0.0, 1e-9);
    EXPECT_NEAR(state.draft.input_max, 1.0, 1e-9);
    // Fresh drafts span the descriptor range so the first Apply is visible.
    EXPECT_NEAR(state.draft.output_min, 0.40, 1e-6);
    EXPECT_NEAR(state.draft.output_max, 2.50, 1e-6);
    EXPECT_TRUE(state.draft.interpolation == MUSI_INTERPOLATION_LINEAR);
    EXPECT_TRUE(state.draft.clamp);
    // Untouched fresh drafts do not guard and are already valid.
    EXPECT_FALSE(route_editor_dirty(&state));
    EXPECT_TRUE(route_editor_can_apply(&state));
}

TEST(route_editor_open_rejects_bad_targets)
{
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_FALSE(route_editor_open(&state, TRACK_A, COUNT_SCENES, 0, NULL));
    EXPECT_FALSE(route_editor_open(&state, TRACK_A, SCENE_LOOM,
                                   SCENE_SETTINGS_MAX_CONTROLS, NULL));

    // An existing route must belong to exactly the opened setting.
    Musi_Parameter_Mapping route = committed_band_route();
    size_t density = setting_index_for(SCENE_LOOM, "settings.loom.density");
    EXPECT_FALSE(route_editor_open(&state, TRACK_A, SCENE_LOOM, density, &route));
    Musi_Parameter_Mapping poisoned = committed_band_route();
    poisoned.input_max = poisoned.input_min;
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    EXPECT_FALSE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight,
                                   &poisoned));
    EXPECT_FALSE(route_editor_is_open(&state));
}

TEST(route_editor_open_with_existing_route_starts_clean)
{
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    Musi_Parameter_Mapping route = committed_band_route();
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, &route));
    EXPECT_TRUE(state.has_committed);
    EXPECT_FALSE(route_editor_dirty(&state));

    // Editing dirties; restoring the committed value cleans again.
    EXPECT_TRUE(route_editor_set_curve(&state, MUSI_INTERPOLATION_LINEAR));
    EXPECT_TRUE(route_editor_dirty(&state));
    EXPECT_TRUE(route_editor_set_curve(&state, MUSI_INTERPOLATION_SMOOTHSTEP));
    EXPECT_FALSE(route_editor_dirty(&state));
    // No-change writes never dirty.
    EXPECT_TRUE(route_editor_set_clamp(&state, true));
    EXPECT_TRUE(route_editor_set_source(&state, MUSI_ANALYSIS_BAND));
    EXPECT_FALSE(route_editor_dirty(&state));
}

TEST(route_editor_setters_keep_draft_valid)
{
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, NULL));

    // The input window keeps a minimum width while endpoints drag past
    // each other, and stays inside 0..1.
    EXPECT_TRUE(route_editor_set_input_max(&state, 0.30));
    EXPECT_TRUE(route_editor_set_input_min(&state, 0.90));
    EXPECT_NEAR(state.draft.input_min, 0.30 - ROUTE_EDITOR_INPUT_GAP, 1e-9);
    EXPECT_TRUE(route_editor_set_input_min(&state, -4.0));
    EXPECT_NEAR(state.draft.input_min, 0.0, 1e-9);
    EXPECT_TRUE(route_editor_set_input_max(&state, 7.0));
    EXPECT_NEAR(state.draft.input_max, 1.0, 1e-9);
    EXPECT_FALSE(route_editor_set_input_min(&state, NAN));
    EXPECT_FALSE(route_editor_set_input_max(&state, INFINITY));

    // Output endpoints clamp to the descriptor range; inversion is legal.
    EXPECT_TRUE(route_editor_set_output_low(&state, 99.0));
    EXPECT_NEAR(state.draft.output_min, 2.50, 1e-6);
    EXPECT_TRUE(route_editor_set_output_high(&state, -99.0));
    EXPECT_NEAR(state.draft.output_max, 0.40, 1e-6);
    EXPECT_TRUE(route_editor_can_apply(&state));
    EXPECT_TRUE(route_editor_swap_output(&state));
    EXPECT_NEAR(state.draft.output_min, 0.40, 1e-6);
    EXPECT_NEAR(state.draft.output_max, 2.50, 1e-6);

    EXPECT_FALSE(route_editor_set_curve(&state,
                                        (Musi_Interpolation)MUSI_INTERPOLATION_COUNT));
    EXPECT_FALSE(route_editor_set_source(&state,
                                         (Musi_Analysis_Source)MUSI_ANALYSIS_SOURCE_COUNT));
    EXPECT_TRUE(route_editor_can_apply(&state));

    // A closed editor accepts no edits.
    route_editor_close(&state);
    EXPECT_FALSE(route_editor_set_input_min(&state, 0.2));
    EXPECT_FALSE(route_editor_dirty(&state));
}

TEST(route_editor_integer_settings_snap_outputs)
{
    size_t color = setting_index_for(SCENE_SONG_ATLAS, "settings.atlas.color");
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_SONG_ATLAS, color, NULL));
    EXPECT_TRUE(route_editor_set_output_low(&state, -42.4));
    EXPECT_NEAR(state.draft.output_min, -42.0, 1e-9);
    EXPECT_TRUE(route_editor_set_output_high(&state, 260.0));
    EXPECT_NEAR(state.draft.output_max, 180.0, 1e-9);
}

TEST(route_editor_band_stepping_respects_source_and_limit)
{
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, NULL));

    // Band stepping is only meaningful for the band source.
    EXPECT_FALSE(route_editor_step_band(&state, 1, 24));
    EXPECT_TRUE(route_editor_set_source(&state, MUSI_ANALYSIS_BAND));
    EXPECT_TRUE(route_editor_step_band(&state, 5, 24));
    EXPECT_EQ_SIZE((size_t)state.draft.band_index, 5);
    EXPECT_TRUE(route_editor_step_band(&state, 100, 24));
    EXPECT_EQ_SIZE((size_t)state.draft.band_index, 23);
    EXPECT_TRUE(route_editor_step_band(&state, -100, 24));
    EXPECT_EQ_SIZE((size_t)state.draft.band_index, 0);
    EXPECT_FALSE(route_editor_step_band(&state, 1, 0));

    // Leaving the band source drops the index (schema: zero unless band).
    EXPECT_TRUE(route_editor_step_band(&state, 3, 24));
    EXPECT_TRUE(route_editor_set_source(&state, MUSI_ANALYSIS_PEAK));
    EXPECT_EQ_SIZE((size_t)state.draft.band_index, 0);
    EXPECT_TRUE(route_editor_can_apply(&state));
}

TEST(route_editor_apply_commits_and_replaces_in_table)
{
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    Scene_Route_Table table;
    scene_route_table_init(&table);
    Route_Editor_State state;
    route_editor_init(&state);
    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, NULL));
    EXPECT_TRUE(route_editor_set_source(&state, MUSI_ANALYSIS_BAND));
    EXPECT_TRUE(route_editor_step_band(&state, 2, 24));
    EXPECT_TRUE(route_editor_dirty(&state));

    EXPECT_TRUE(route_editor_apply(&state, &table));
    EXPECT_EQ_SIZE(table.scenes[SCENE_LOOM].count, 1);
    EXPECT_TRUE(route_editor_is_open(&state));
    EXPECT_TRUE(state.has_committed);
    EXPECT_FALSE(route_editor_dirty(&state));
    const Musi_Parameter_Mapping *routed =
        route_editor_find_route(&table, SCENE_LOOM, weight);
    EXPECT_TRUE(routed != NULL);
    EXPECT_EQ_SIZE((size_t)routed->band_index, 2);

    // Re-applying an edited draft replaces the parameter's slot in place.
    EXPECT_TRUE(route_editor_set_curve(&state, MUSI_INTERPOLATION_EASE_OUT));
    EXPECT_TRUE(route_editor_apply(&state, &table));
    EXPECT_EQ_SIZE(table.scenes[SCENE_LOOM].count, 1);
    routed = route_editor_find_route(&table, SCENE_LOOM, weight);
    EXPECT_TRUE(routed != NULL);
    EXPECT_TRUE(routed->interpolation == MUSI_INTERPOLATION_EASE_OUT);
    EXPECT_TRUE(scene_route_table_valid(&table));

    size_t density = setting_index_for(SCENE_LOOM, "settings.loom.density");
    EXPECT_TRUE(route_editor_find_route(&table, SCENE_LOOM, density) == NULL);
}

TEST(route_editor_remove_deletes_route_and_closes)
{
    size_t weight = setting_index_for(SCENE_LOOM, "settings.loom.weight");
    Scene_Route_Table table;
    scene_route_table_init(&table);
    Musi_Parameter_Mapping route = committed_band_route();
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &route));

    Route_Editor_State state;
    route_editor_init(&state);
    // A fresh draft that never committed has nothing to remove.
    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, NULL));
    EXPECT_FALSE(route_editor_remove(&state, &table));
    EXPECT_EQ_SIZE(table.scenes[SCENE_LOOM].count, 1);

    EXPECT_TRUE(route_editor_open(&state, TRACK_A, SCENE_LOOM, weight, &route));
    EXPECT_TRUE(route_editor_remove(&state, &table));
    EXPECT_EQ_SIZE(table.scenes[SCENE_LOOM].count, 0);
    EXPECT_FALSE(route_editor_is_open(&state));
    EXPECT_TRUE(route_editor_find_route(&table, SCENE_LOOM, weight) == NULL);
}

TEST(route_editor_summary_and_meter_read_back)
{
    Musi_Parameter_Mapping route = committed_band_route();
    char summary[96];
    route_editor_summary(&route, 2, summary, sizeof(summary));
    EXPECT_TRUE(strcmp(summary,
                       "Band 2 \xC2\xB7 Smooth \xC2\xB7 0.40 \xE2\x86\x92 2.20") == 0);
    route.clamp = false;
    route.source = MUSI_ANALYSIS_BEAT_PHASE;
    route.band_index = 0;
    route_editor_summary(&route, 2, summary, sizeof(summary));
    EXPECT_TRUE(strcmp(summary,
                       "Beat \xC2\xB7 Smooth \xC2\xB7 0.40 \xE2\x86\x92 2.20"
                       " \xC2\xB7 unclamped") == 0);
    route_editor_summary(NULL, 2, summary, sizeof(summary));
    EXPECT_TRUE(summary[0] == '\0');

    route.input_min = 0.2;
    route.input_max = 0.7;
    EXPECT_NEAR(route_editor_meter_position(&route, 0.45), 0.5, 1e-6);
    EXPECT_NEAR(route_editor_meter_position(&route, -3.0), 0.0, 1e-6);
    EXPECT_NEAR(route_editor_meter_position(&route, 9.0), 1.0, 1e-6);
    EXPECT_NEAR(route_editor_meter_position(&route, NAN), 0.0, 1e-6);
    EXPECT_NEAR(route_editor_meter_position(NULL, 0.5), 0.0, 1e-6);
}
