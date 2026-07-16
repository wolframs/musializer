#include "scene_settings.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

TEST(scene_settings_defaults_are_complete_valid_and_scene_specific)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    EXPECT_TRUE(scene_settings_valid(&settings));
    EXPECT_EQ_SIZE(scene_settings_count(0), 8);
    EXPECT_EQ_SIZE(scene_settings_count(1), 8);
    EXPECT_EQ_SIZE(scene_settings_count(2), 9);
    EXPECT_EQ_SIZE(scene_settings_count(3), 6);
    EXPECT_EQ_SIZE(scene_settings_count(4), 12);
    EXPECT_EQ_SIZE(scene_settings_count(5), 8);
    EXPECT_EQ_SIZE(scene_settings_count(6), 8);
    EXPECT_EQ_SIZE(scene_settings_count(7), 7);
    EXPECT_EQ_SIZE(scene_settings_count(8), 7);
    EXPECT_EQ_SIZE(scene_settings_count(9), 8);
    EXPECT_EQ_SIZE(scene_settings_count(99), 0);
    EXPECT_NEAR(scene_settings_get(&settings, 1, 1), 24.0f, 0.0f);
    const Scene_Setting_Descriptor *height = scene_settings_descriptor(
        4, ATLAS_SETTING_HEIGHT);
    const Scene_Setting_Descriptor *width = scene_settings_descriptor(
        4, ATLAS_SETTING_WIDTH);
    const Scene_Setting_Descriptor *camera = scene_settings_descriptor(
        4, ATLAS_SETTING_CAMERA);
    const Scene_Setting_Descriptor *wireframe = scene_settings_descriptor(
        4, ATLAS_SETTING_WIREFRAME);
    const Scene_Setting_Descriptor *detail = scene_settings_descriptor(
        4, ATLAS_SETTING_DETAIL);
    const Scene_Setting_Descriptor *hue_motion = scene_settings_descriptor(
        4, ATLAS_SETTING_HUE_MOTION);
    REQUIRE_TRUE(height != NULL && width != NULL && camera != NULL &&
                 wireframe != NULL && detail != NULL && hue_motion != NULL);
    EXPECT_NEAR(height->maximum, 2.75f, 0.0f);
    EXPECT_NEAR(width->maximum, 3.20f, 0.0f);
    EXPECT_NEAR(camera->minimum, 0.25f, 0.0f);
    EXPECT_NEAR(camera->maximum, 1.75f, 0.0f);
    EXPECT_TRUE(wireframe->kind == SCENE_SETTING_TOGGLE);
    EXPECT_NEAR(detail->minimum, 1.0f, 0.0f);
    EXPECT_NEAR(detail->maximum, 3.0f, 0.0f);
    EXPECT_TRUE(hue_motion->kind == SCENE_SETTING_TOGGLE);
}

TEST(scene_settings_set_and_reset_are_bounded)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    EXPECT_TRUE(scene_settings_set(&settings, 1, 0, 0.75f));
    EXPECT_NEAR(scene_settings_get(&settings, 1, 0), 0.75f, 0.0f);
    EXPECT_FALSE(scene_settings_set(&settings, 1, 0, 99.0f));
    EXPECT_FALSE(scene_settings_set(&settings, 1, 0, NAN));
    EXPECT_FALSE(scene_settings_set(&settings, 99, 0, 1.0f));
    EXPECT_FALSE(scene_settings_set(&settings, 4, ATLAS_SETTING_WIREFRAME, 0.5f));
    EXPECT_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_WIREFRAME, 1.0f));
    EXPECT_NEAR(scene_settings_get(&settings, 1, 0), 0.75f, 0.0f);
    EXPECT_TRUE(scene_settings_reset_scene(&settings, 1));
    EXPECT_NEAR(scene_settings_get(&settings, 1, 0), 1.0f, 0.0f);
}

TEST(scene_settings_snapshots_are_scene_specific_and_atomic)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    REQUIRE_TRUE(scene_settings_set(&settings, 1, PULSE_SETTING_RINGS, 31.0f));
    Scene_Settings_Snapshot snapshot;
    REQUIRE_TRUE(scene_settings_capture(&settings, 1, &snapshot));
    EXPECT_TRUE(scene_settings_snapshot_valid(1, &snapshot));
    EXPECT_EQ_SIZE(snapshot.count, 8);

    REQUIRE_TRUE(scene_settings_set(&settings, 1, PULSE_SETTING_RINGS, 12.0f));
    REQUIRE_TRUE(scene_settings_apply_snapshot(&settings, 1, &snapshot));
    EXPECT_NEAR(scene_settings_get(&settings, 1, PULSE_SETTING_RINGS), 31.0f, 0.0f);

    Scene_Settings before = settings;
    snapshot.count = 4;
    EXPECT_FALSE(scene_settings_apply_snapshot(&settings, 1, &snapshot));
    EXPECT_TRUE(memcmp(&settings, &before, sizeof(settings)) == 0);
}

TEST(scene_settings_legacy_atlas_snapshots_default_new_controls)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    REQUIRE_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_DETAIL, 3.0f));
    REQUIRE_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_HUE_MOTION, 1.0f));
    Scene_Settings_Snapshot legacy = {
        .captured = true,
        .count = 8,
        .values = {1.2f, 2.0f, 1.0f, 1.1f, 1.0f, 30.0f, 0.8f, 1.0f},
    };
    EXPECT_TRUE(scene_settings_snapshot_valid(4, &legacy));
    REQUIRE_TRUE(scene_settings_apply_snapshot(&settings, 4, &legacy));
    EXPECT_NEAR(scene_settings_get(&settings, 4, ATLAS_SETTING_DETAIL), 1.0f, 0.0f);
    EXPECT_NEAR(scene_settings_get(&settings, 4, ATLAS_SETTING_HUE_MOTION),
                0.0f, 0.0f);
}

TEST(scene_settings_prior_generation_snapshots_default_new_controls)
{
    // Snapshot counts saved before the tunability expansions: spectrum,
    // terrarium, and constellation at 7, pulse and orbital at 5, ascii at 4;
    // orbital again at 7 after its first widening but before the liveliness
    // controls landed; atlas at 8, then 10 before its camera orbit/distance
    // controls landed.
    const struct { size_t scene; size_t count; } legacy_counts[] = {
        {0, 7}, {1, 5}, {2, 5}, {2, 7}, {3, 4}, {4, 8}, {4, 10}, {5, 7}, {6, 7},
    };
    Scene_Settings settings;
    scene_settings_init(&settings);
    for (size_t at = 0; at < sizeof(legacy_counts)/sizeof(legacy_counts[0]); ++at) {
        size_t scene = legacy_counts[at].scene;
        size_t count = legacy_counts[at].count;
        Scene_Settings_Snapshot legacy = { .captured = true, .count = count };
        for (size_t index = 0; index < count; ++index) {
            legacy.values[index] =
                scene_settings_descriptor(scene, index)->default_value;
        }
        EXPECT_TRUE(scene_settings_snapshot_valid(scene, &legacy));
        REQUIRE_TRUE(scene_settings_apply_snapshot(&settings, scene, &legacy));
        const Scene_Setting_Descriptor *added =
            scene_settings_descriptor(scene, count);
        REQUIRE_TRUE(added != NULL);
        EXPECT_NEAR(scene_settings_get(&settings, scene, count),
                    added->default_value, 0.000001f);
        Scene_Settings_Snapshot wrong = legacy;
        wrong.count = count - 1U;
        EXPECT_FALSE(scene_settings_snapshot_valid(scene, &wrong));
    }
}

TEST(scene_settings_legacy_three_control_snapshots_default_new_controls)
{
    const size_t scenes[] = {0, 5, 6};
    Scene_Settings settings;
    scene_settings_init(&settings);
    for (size_t at = 0; at < sizeof(scenes)/sizeof(scenes[0]); ++at) {
        size_t scene = scenes[at];
        Scene_Settings_Snapshot legacy = {
            .captured = true,
            .count = 3,
            .values = {1.0f, 1.0f, 1.0f},
        };
        EXPECT_TRUE(scene_settings_snapshot_valid(scene, &legacy));
        REQUIRE_TRUE(scene_settings_apply_snapshot(&settings, scene, &legacy));
        const Scene_Setting_Descriptor *added = scene_settings_descriptor(scene, 3);
        REQUIRE_TRUE(added != NULL);
        EXPECT_NEAR(scene_settings_get(&settings, scene, 3),
                    added->default_value, 0.000001f);
    }
}

TEST(scene_settings_presets_save_replace_apply_and_remove_per_scene)
{
    Scene_Settings settings;
    Scene_Settings_Preset_Library library;
    scene_settings_init(&settings);
    scene_settings_preset_library_init(&library);
    REQUIRE_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_WIDTH, 2.75f));
    size_t selected = SIZE_MAX;
    REQUIRE_TRUE(scene_settings_preset_save(
        &library, 4, "Preset 1", &settings, &selected));
    EXPECT_EQ_SIZE(selected, 0);
    EXPECT_EQ_SIZE(library.counts[4], 1);
    EXPECT_TRUE(scene_settings_preset_library_valid(&library));

    library.items[1][0].id = library.items[4][0].id;
    strcpy(library.items[1][0].name, "Duplicate ID");
    REQUIRE_TRUE(scene_settings_capture(
        &settings, 1, &library.items[1][0].snapshot));
    library.counts[1] = 1;
    EXPECT_FALSE(scene_settings_preset_library_valid(&library));
    memset(&library.items[1][0], 0, sizeof(library.items[1][0]));
    library.counts[1] = 0;

    REQUIRE_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_WIDTH, 1.25f));
    REQUIRE_TRUE(scene_settings_preset_apply(&library, 4, 0, &settings));
    EXPECT_NEAR(scene_settings_get(&settings, 4, ATLAS_SETTING_WIDTH), 2.75f, 0.0f);

    REQUIRE_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_WIDTH, 3.0f));
    REQUIRE_TRUE(scene_settings_preset_replace(&library, 4, 0, &settings));
    REQUIRE_TRUE(scene_settings_set(&settings, 4, ATLAS_SETTING_WIDTH, 1.0f));
    REQUIRE_TRUE(scene_settings_preset_apply(&library, 4, 0, &settings));
    EXPECT_NEAR(scene_settings_get(&settings, 4, ATLAS_SETTING_WIDTH), 3.0f, 0.0f);

    EXPECT_EQ_SIZE(library.counts[1], 0);
    REQUIRE_TRUE(scene_settings_preset_remove(&library, 4, 0));
    EXPECT_EQ_SIZE(library.counts[4], 0);
    EXPECT_TRUE(scene_settings_preset_library_valid(&library));
}

TEST(scene_settings_constant_mapping_round_trip_is_atomic)
{
    Scene_Settings source;
    Scene_Settings decoded;
    scene_settings_init(&source);
    REQUIRE_TRUE(scene_settings_set(&source, 2, 0, 0.42f));
    REQUIRE_TRUE(scene_settings_set(&source, 4, 3, 1.24f));
    Musi_Parameter_Mapping mappings[MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE];
    memset(mappings, 0xa5, sizeof(mappings));
    size_t count = 999;
    REQUIRE_TRUE(scene_settings_export_mappings(
        &source, mappings, MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE, &count));
    EXPECT_EQ_SIZE(count, 81);
    EXPECT_TRUE(scene_settings_mappings_supported(mappings, count));
    REQUIRE_TRUE(scene_settings_import_mappings(&decoded, mappings, count));
    EXPECT_NEAR(scene_settings_get(&decoded, 2, 0), 0.42f, 0.000001f);
    EXPECT_NEAR(scene_settings_get(&decoded, 4, 3), 1.24f, 0.000001f);

    Scene_Settings before = decoded;
    char second_parameter[MUSI_PROJECT_PARAMETER_CAPACITY];
    strcpy(second_parameter, mappings[1].parameter);
    strcpy(mappings[1].parameter, mappings[0].parameter);
    EXPECT_FALSE(scene_settings_mappings_supported(mappings, count));
    EXPECT_FALSE(scene_settings_import_mappings(&decoded, mappings, count));
    EXPECT_TRUE(memcmp(&decoded, &before, sizeof(decoded)) == 0);
    strcpy(mappings[1].parameter, second_parameter);

    mappings[0].output_max += 0.1;
    EXPECT_FALSE(scene_settings_import_mappings(&decoded, mappings, count));
    EXPECT_TRUE(memcmp(&decoded, &before, sizeof(decoded)) == 0);
}

TEST(scene_settings_rejects_arbitrary_analysis_mappings)
{
    Musi_Parameter_Mapping mapping = {0};
    strcpy(mapping.parameter, "glow");
    mapping.source = MUSI_ANALYSIS_BAND;
    mapping.input_max = 1.0;
    mapping.output_max = 1.0;
    mapping.interpolation = MUSI_INTERPOLATION_LINEAR;
    mapping.clamp = true;
    EXPECT_FALSE(scene_settings_mapping_supported(&mapping));

    strcpy(mapping.parameter, "settings.pulse.scale");
    mapping.source = MUSI_ANALYSIS_RMS;
    mapping.output_min = 1.0;
    EXPECT_TRUE(scene_settings_mapping_supported(&mapping));
}

TEST(scene_settings_layout_expands_or_compacts_without_collapsing_workspace)
{
    Scene_Settings_Ui_Layout closed;
    Scene_Settings_Ui_Layout open;
    REQUIRE_TRUE(scene_settings_ui_layout(1280.0f, false, &closed));
    REQUIRE_TRUE(scene_settings_ui_layout(1280.0f, true, &open));
    EXPECT_NEAR(closed.workspace_width, 1280.0f, 0.0f);
    EXPECT_NEAR(closed.tracks_width, 320.0f, 0.0f);
    EXPECT_TRUE(open.inspector_width >= 280.0f);
    EXPECT_NEAR(open.workspace_width + open.inspector_width, 1280.0f, 0.001f);
    EXPECT_TRUE(open.tracks_width >= 240.0f && open.tracks_width <= 320.0f);

    REQUIRE_TRUE(scene_settings_ui_layout(960.0f, true, &open));
    EXPECT_TRUE(open.workspace_width >= 620.0f);
    REQUIRE_TRUE(scene_settings_ui_layout(640.0f, true, &open));
    EXPECT_TRUE(open.workspace_width - open.tracks_width >= 640.0f*0.30f);
    EXPECT_TRUE(open.tracks_width >= 168.0f);
    EXPECT_FALSE(scene_settings_ui_layout(NAN, true, &open));

    EXPECT_TRUE(scene_settings_window_can_expand(100, 1280, 0, 1920, 340));
    EXPECT_FALSE(scene_settings_window_can_expand(400, 1280, 0, 1920, 340));
    EXPECT_FALSE(scene_settings_window_can_expand(-20, 1280, 0, 1920, 340));
}
