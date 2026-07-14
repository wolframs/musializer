#include "scene_settings.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

TEST(scene_settings_defaults_are_complete_valid_and_scene_specific)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    EXPECT_TRUE(scene_settings_valid(&settings));
    EXPECT_EQ_SIZE(scene_settings_count(0), 3);
    EXPECT_EQ_SIZE(scene_settings_count(1), 5);
    EXPECT_EQ_SIZE(scene_settings_count(2), 5);
    EXPECT_EQ_SIZE(scene_settings_count(4), 8);
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
    REQUIRE_TRUE(height != NULL && width != NULL && camera != NULL &&
                 wireframe != NULL);
    EXPECT_NEAR(height->maximum, 2.75f, 0.0f);
    EXPECT_NEAR(width->maximum, 3.20f, 0.0f);
    EXPECT_NEAR(camera->minimum, 0.25f, 0.0f);
    EXPECT_NEAR(camera->maximum, 1.75f, 0.0f);
    EXPECT_TRUE(wireframe->kind == SCENE_SETTING_TOGGLE);
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
    EXPECT_EQ_SIZE(count, 31);
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
    EXPECT_FALSE(scene_settings_ui_layout(NAN, true, &open));

    EXPECT_TRUE(scene_settings_window_can_expand(100, 1280, 0, 1920, 340));
    EXPECT_FALSE(scene_settings_window_can_expand(400, 1280, 0, 1920, 340));
    EXPECT_FALSE(scene_settings_window_can_expand(-20, 1280, 0, 1920, 340));
}
