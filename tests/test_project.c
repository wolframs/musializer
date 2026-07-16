#include "project.h"
#include "scene_settings.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

static void fill_sha256(char value[MUSI_PROJECT_ID_CAPACITY], char digit)
{
    size_t i = 0;
    for (; i < 64; ++i) value[i] = digit;
    value[64] = '\0';
}

static Musi_Project valid_project(void)
{
    Musi_Project project;
    Musi_Scene_Entry *scene = NULL;
    Musi_Parameter_Mapping *mapping = NULL;

    musi_project_init(&project);
    strcpy(project.metadata.project_id, "kitty-atlas-01");
    strcpy(project.metadata.title, "Autoregressive Kitty Atlas");
    strcpy(project.metadata.application_version, "0.1.0");
    strcpy(project.audio.path, "assets/autoregressive-kitty.mp3");
    fill_sha256(project.audio.sha256, 'a');
    project.audio.duration_seconds = 180.0;
    project.audio.sample_rate = 48000;
    project.audio.channels = 2;
    project.lyrics.duration_seconds = 180.0;
    project.output.start_seconds = 0.0;
    project.output.end_seconds = 180.0;
    project.deterministic_seed = UINT64_C(0xf00dcafe12345678);

    project.scene_count = 1;
    scene = &project.scenes[0];
    scene->instance_id = 101;
    strcpy(scene->scene_type, "spectrum");
    scene->enabled = true;
    scene->start_seconds = 0.0;
    scene->end_seconds = 180.0;
    scene->opacity = 1.0;
    scene->blend_mode = MUSI_BLEND_NORMAL;
    scene->mapping_count = 1;
    mapping = &scene->mappings[0];
    strcpy(mapping->parameter, "bar_height");
    mapping->source = MUSI_ANALYSIS_RMS;
    mapping->input_min = 0.0;
    mapping->input_max = 1.0;
    mapping->output_min = 0.25;
    mapping->output_max = 2.0;
    mapping->interpolation = MUSI_INTERPOLATION_SMOOTHSTEP;
    mapping->clamp = true;

    project.cue_count = 2;
    project.cues[0].cue_id = 1;
    project.cues[0].target_scene_id = 101;
    strcpy(project.cues[0].parameter, "exposure");
    project.cues[0].start_seconds = 10.0;
    project.cues[0].end_seconds = 20.0;
    project.cues[0].from_value = 0.5;
    project.cues[0].to_value = 1.5;
    project.cues[0].interpolation = MUSI_INTERPOLATION_LINEAR;
    project.cues[1].cue_id = 2;
    project.cues[1].target_scene_id = 101;
    strcpy(project.cues[1].parameter, "exposure");
    project.cues[1].start_seconds = 30.0;
    project.cues[1].end_seconds = 40.0;
    project.cues[1].from_value = 1.5;
    project.cues[1].to_value = 0.75;
    project.cues[1].interpolation = MUSI_INTERPOLATION_SMOOTHSTEP;

    project.analysis_lane_count = 1;
    project.analysis_lanes[0].kind = MUSI_LANE_SEMANTIC_SCORE;
    strcpy(project.analysis_lanes[0].path, "analysis/semantic-score.json");
    fill_sha256(project.analysis_lanes[0].sha256, 'b');
    strcpy(project.analysis_lanes[0].audio_sha256, project.audio.sha256);
    strcpy(project.analysis_lanes[0].provenance.adapter, "mimo-openrouter");
    strcpy(project.analysis_lanes[0].provenance.adapter_version, "1");
    strcpy(project.analysis_lanes[0].provenance.schema_version,
           "musializer.semantic-score/v1");
    strcpy(project.analysis_lanes[0].provenance.model, "xiaomi/mimo-v2.5");
    strcpy(project.analysis_lanes[0].provenance.provider, "DeepInfra");
    strcpy(project.analysis_lanes[0].provenance.prompt_version, "v1");
    return project;
}

TEST(project_defaults_and_valid_contract)
{
    Musi_Project project;
    Musi_Project_Validation result;
    musi_project_init(&project);
    EXPECT_EQ_SIZE(project.schema_version, MUSI_PROJECT_SCHEMA_VERSION);
    EXPECT_EQ_SIZE(project.output.width, 1920);
    EXPECT_EQ_SIZE(project.output.height, 1080);
    EXPECT_EQ_SIZE(project.output.fps_numerator, 30);
    EXPECT_EQ_SIZE(project.output.fps_denominator, 1);
    EXPECT_EQ_SIZE(project.output.quality, MUSI_OUTPUT_QUALITY_HIGH);
    EXPECT_TRUE(musi_project_validate(&project).error != MUSI_PROJECT_VALID);

    project = valid_project();
    result = musi_project_validate(&project);
    EXPECT_EQ_SIZE(result.error, MUSI_PROJECT_VALID);
    EXPECT_TRUE(strcmp(musi_analysis_lane_kind_name(MUSI_LANE_SEMANTIC_SCORE),
                       "semantic_score") == 0);

    project.output.fps_numerator = 24000;
    project.output.fps_denominator = 1001;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_VALID);
    project.audio.mode = MUSI_ASSET_REFERENCED;
    EXPECT_EQ_SIZE(musi_project_editor_support(&project),
                   MUSI_PROJECT_EDITOR_ERROR_OUTPUT_FORMAT);
    project.output.fps_numerator = 240241;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error,
                   MUSI_PROJECT_ERROR_OUTPUT);
}

TEST(project_editor_subset_rejects_every_lossy_normalization)
{
    Musi_Project project = valid_project();
    project.audio.mode = MUSI_ASSET_REFERENCED;
    project.cue_count = 0;
    project.scenes[0].mapping_count = 0;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_SUPPORTED);

    project.output.start_seconds = 1.0;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_ERROR_OUTPUT_RANGE);
    project.output.start_seconds = 0.0;
    project.scenes[0].opacity = 0.5;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_ERROR_SCENE_LAYOUT);
    project.scenes[0].opacity = 1.0;
    project.scenes[0].mapping_count = 1;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_ERROR_SCENE_MAPPINGS);
    project.scenes[0].mapping_count = 0;
    project.cue_count = 1;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_ERROR_PARAMETER_CUES);
    project.cue_count = 0;
    project.audio.mode = MUSI_ASSET_IMPORTED;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_SUPPORTED);
    project.audio.mode = MUSI_ASSET_MODE_COUNT;
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_ERROR_AUDIO_MODE);
}

TEST(project_editor_subset_accepts_canonical_scene_setting_presets)
{
    Musi_Project project = valid_project();
    Scene_Settings settings;
    scene_settings_init(&settings);
    REQUIRE_TRUE(scene_settings_set(&settings, 1, PULSE_SETTING_RINGS, 31.0f));
    project.audio.mode = MUSI_ASSET_REFERENCED;
    project.cue_count = 0;
    REQUIRE_TRUE(scene_settings_export_mappings(
        &settings, project.scenes[0].mappings,
        MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE,
        &project.scenes[0].mapping_count));
    EXPECT_TRUE(musi_project_validate(&project).error == MUSI_PROJECT_VALID);
    EXPECT_TRUE(musi_project_editor_support(&project) ==
                MUSI_PROJECT_EDITOR_SUPPORTED);
}

TEST(project_validates_scene_presets_and_cue_setting_snapshots)
{
    Musi_Project project = valid_project();
    project.scene_switches.count = 1;
    project.scene_switches.cues[0] = (Musi_Scene_Switch_Suggestion) {
        .id = 90, .start_seconds = 0.0, .end_seconds = 180.0,
        .strength = 1.0f, .setting_count = 3, .settings = {1.0f, 1.0f, 1.0f},
    };
    strcpy(project.scene_switches.cues[0].scene_name, "spectrum");
    project.scene_preset_count = 1;
    project.scene_presets[0] = (Musi_Scene_Preset) {
        .id = 91, .setting_count = 3, .settings = {1.0f, 1.0f, 1.0f},
    };
    strcpy(project.scene_presets[0].scene_name, "spectrum");
    strcpy(project.scene_presets[0].name, "Preset 1");
    EXPECT_TRUE(musi_project_validate(&project).error == MUSI_PROJECT_VALID);

    project.scene_switches.cues[0].settings[1] = NAN;
    EXPECT_TRUE(musi_project_validate(&project).error ==
                MUSI_PROJECT_ERROR_SCENE_SWITCH);
    project.scene_switches.cues[0].settings[1] = 1.0f;
    project.scene_presets[0].setting_count = 0;
    EXPECT_TRUE(musi_project_validate(&project).error ==
                MUSI_PROJECT_ERROR_SCENE_PRESET);
}

TEST(project_accepts_every_per_scene_preset_slot)
{
    static const char *const scene_names[SCENE_SETTINGS_SCENE_COUNT] = {
        "spectrum", "pulse", "orbital", "ascii", "atlas",
        "terrarium", "constellation", "cadence", "loom", "pentagram",
    };
    Musi_Project project = valid_project();
    Scene_Settings settings;
    scene_settings_init(&settings);

    EXPECT_EQ_SIZE(MUSI_PROJECT_MAX_SCENE_PRESETS,
                   SCENE_SETTINGS_SCENE_COUNT*SCENE_SETTINGS_PRESETS_PER_SCENE);
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        Scene_Settings_Snapshot snapshot;
        REQUIRE_TRUE(scene_settings_capture(&settings, scene, &snapshot));
        for (size_t slot = 0; slot < SCENE_SETTINGS_PRESETS_PER_SCENE; ++slot) {
            size_t index = scene*SCENE_SETTINGS_PRESETS_PER_SCENE + slot;
            Musi_Scene_Preset *preset = &project.scene_presets[index];
            preset->id = index + 1;
            strcpy(preset->scene_name, scene_names[scene]);
            strcpy(preset->name, "Preset");
            preset->setting_count = snapshot.count;
            memcpy(preset->settings, snapshot.values,
                   snapshot.count*sizeof(snapshot.values[0]));
        }
    }
    project.scene_preset_count = MUSI_PROJECT_MAX_SCENE_PRESETS;
    EXPECT_TRUE(musi_project_validate(&project).error == MUSI_PROJECT_VALID);
}

TEST(project_audio_metadata_identity_is_strict_and_tolerant_only_in_time)
{
    Musi_Project project = valid_project();
    EXPECT_TRUE(musi_project_audio_metadata_matches(
        &project, 180.0005, 48000, 2, 0.001));
    EXPECT_FALSE(musi_project_audio_metadata_matches(
        &project, 180.01, 48000, 2, 0.001));
    EXPECT_FALSE(musi_project_audio_metadata_matches(
        &project, 180.0, 44100, 2, 0.001));
    EXPECT_FALSE(musi_project_audio_metadata_matches(
        &project, 180.0, 48000, 1, 0.001));
    EXPECT_FALSE(musi_project_audio_metadata_matches(
        &project, NAN, 48000, 2, 0.001));
}

TEST(project_rejects_ranges_counts_and_non_finite_values)
{
    Musi_Project project = valid_project();
    project.scene_count = MUSI_PROJECT_MAX_SCENES + 1u;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_COUNT);

    project = valid_project();
    project.output.end_seconds = project.output.start_seconds;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_OUTPUT);

    project = valid_project();
    project.output.quality = MUSI_OUTPUT_QUALITY_COUNT;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_OUTPUT);

    project = valid_project();
    project.scenes[0].opacity = NAN;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SCENE);

    project = valid_project();
    project.scenes[0].mappings[0].input_max =
        project.scenes[0].mappings[0].input_min;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_MAPPING);

    project = valid_project();
    fill_sha256(project.analysis_lanes[0].audio_sha256, 'c');
    EXPECT_EQ_SIZE(musi_project_validate(&project).error,
                   MUSI_PROJECT_ERROR_ANALYSIS_LANE);

    project = valid_project();
    project.ascii_image.present = true;
    strcpy(project.ascii_image.path, "show.assets/images/source.png");
    fill_sha256(project.ascii_image.sha256, 'd');
    project.ascii_image.columns = ASCII_GRID_MAX_COLUMNS;
    project.ascii_image.rows = ASCII_GRID_MAX_ROWS;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_VALID);
    project.ascii_image.columns = ASCII_GRID_MAX_COLUMNS + 1u;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error,
                   MUSI_PROJECT_ERROR_ASCII_IMAGE);

    project = valid_project();
    project.ascii_image.path[0] = 'x';
    project.ascii_image.path[1] = '\0';
    EXPECT_EQ_SIZE(musi_project_validate(&project).error,
                   MUSI_PROJECT_ERROR_ASCII_IMAGE);
}

TEST(project_rejects_duplicate_ids_unsorted_and_overlapping_cues)
{
    Musi_Project project = valid_project();
    Musi_Project_Validation result;

    project.scene_count = 2;
    project.scenes[1] = project.scenes[0];
    result = musi_project_validate(&project);
    EXPECT_EQ_SIZE(result.error, MUSI_PROJECT_ERROR_DUPLICATE_ID);
    EXPECT_EQ_SIZE(result.index, 1);

    project = valid_project();
    project.cues[1].start_seconds = 5.0;
    project.cues[1].end_seconds = 8.0;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error,
                   MUSI_PROJECT_ERROR_CUE_ORDER);

    project = valid_project();
    project.cues[1].start_seconds = 15.0;
    project.cues[1].end_seconds = 25.0;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error,
                   MUSI_PROJECT_ERROR_CUE_OVERLAP);

    project = valid_project();
    project.cues[1].start_seconds = project.cues[0].start_seconds;
    project.cues[1].end_seconds = project.cues[0].end_seconds;
    strcpy(project.cues[1].parameter, "hue");
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_VALID);
}

TEST(project_interpolation_is_deterministic_and_cues_persist)
{
    Musi_Project project = valid_project();
    double value = 0.0;

    EXPECT_NEAR(musi_interpolate(10.0, 20.0, -1.0, MUSI_INTERPOLATION_LINEAR),
                10.0, 0.0);
    EXPECT_NEAR(musi_interpolate(10.0, 20.0, 0.5, MUSI_INTERPOLATION_LINEAR),
                15.0, 0.0);
    EXPECT_NEAR(musi_interpolate(10.0, 20.0, 0.5, MUSI_INTERPOLATION_SMOOTHSTEP),
                15.0, 0.0);
    EXPECT_NEAR(musi_interpolate(10.0, 20.0, 0.5, MUSI_INTERPOLATION_STEP),
                10.0, 0.0);
    EXPECT_TRUE(isnan(musi_interpolate(0.0, 1.0, NAN, MUSI_INTERPOLATION_LINEAR)));

    REQUIRE_TRUE(musi_project_parameter_at(&project, 101, "exposure", 0.25,
                                           5.0, &value));
    EXPECT_NEAR(value, 0.25, 0.0);
    REQUIRE_TRUE(musi_project_parameter_at(&project, 101, "exposure", 0.25,
                                           15.0, &value));
    EXPECT_NEAR(value, 1.0, 0.000001);
    REQUIRE_TRUE(musi_project_parameter_at(&project, 101, "exposure", 0.25,
                                           25.0, &value));
    EXPECT_NEAR(value, 1.5, 0.0);
    REQUIRE_TRUE(musi_project_parameter_at(&project, 101, "exposure", 0.25,
                                           35.0, &value));
    EXPECT_NEAR(value, 1.125, 0.000001);
    REQUIRE_TRUE(musi_project_parameter_at(&project, 101, "exposure", 0.25,
                                           50.0, &value));
    EXPECT_NEAR(value, 0.75, 0.0);
}

TEST(project_mapping_evaluation_checks_inputs_and_clamps)
{
    Musi_Project project = valid_project();
    Musi_Parameter_Mapping *mapping = &project.scenes[0].mappings[0];
    double value = 0.0;

    REQUIRE_TRUE(musi_mapping_evaluate(mapping, -5.0, &value));
    EXPECT_NEAR(value, 0.25, 0.0);
    REQUIRE_TRUE(musi_mapping_evaluate(mapping, 0.5, &value));
    EXPECT_NEAR(value, 1.125, 0.000001);
    REQUIRE_TRUE(musi_mapping_evaluate(mapping, 5.0, &value));
    EXPECT_NEAR(value, 2.0, 0.0);
    mapping->clamp = false;
    mapping->interpolation = MUSI_INTERPOLATION_LINEAR;
    REQUIRE_TRUE(musi_mapping_evaluate(mapping, 2.0, &value));
    EXPECT_NEAR(value, 3.75, 0.000001);
    EXPECT_FALSE(musi_mapping_evaluate(mapping, NAN, &value));
}

TEST(project_validates_authored_workspace_lanes)
{
    Musi_Project project = valid_project();
    project.lyrics.next_id = 3;
    project.lyrics.count = 1;
    project.lyrics.cues[0] = (Lyric_Cue){.id=2,.start_seconds=1,.end_seconds=2};
    strcpy(project.lyrics.cues[0].text, "hello");
    project.scene_switches.enabled = true;
    project.scene_switches.count = 2;
    project.scene_switches.cues[0] = (Musi_Scene_Switch_Suggestion){.id=10,.start_seconds=0,.end_seconds=90,.strength=.5f};
    strcpy(project.scene_switches.cues[0].scene_name, "spectrum");
    project.scene_switches.cues[1] = (Musi_Scene_Switch_Suggestion){.id=11,.start_seconds=90,.end_seconds=180,.strength=.8f};
    strcpy(project.scene_switches.cues[1].scene_name, "atlas");
    project.manual_events.count = 1;
    project.manual_events.events[0] = (Event_Record){.timestamp_seconds=3,.id=20,.type=EVENT_TYPE_CUSTOM,.value_count=1,.values={1}};
    project.semantic_events.count = 1;
    project.semantic_events.events[0] = (Event_Record){.timestamp_seconds=4,.id=30,.type=EVENT_TYPE_SEMANTIC,.value_count=4,.values={.5f,.75f,-.25f,.9f}};
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_VALID);

    project.lyrics.duration_seconds = 179;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_LYRICS);
    project.lyrics.duration_seconds = 180;
    project.scene_switches.cues[1].start_seconds = 91;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SCENE_SWITCH);
    project.scene_switches.cues[1].start_seconds = 90;
    project.manual_events.events[0].timestamp_seconds = 181;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_MANUAL_EVENT);
}

TEST(project_rejects_malformed_or_out_of_range_embedded_semantics)
{
    Musi_Project project = valid_project();
    project.semantic_events.count = 1;
    project.semantic_events.events[0] = (Event_Record){.timestamp_seconds=4,.id=30,.type=EVENT_TYPE_SEMANTIC,.value_count=4,.values={.5f,.75f,-.25f,.9f}};
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_VALID);

    project.semantic_events.events[0].type = EVENT_TYPE_CUSTOM;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SEMANTIC_EVENT);
    project.semantic_events.events[0].type = EVENT_TYPE_SEMANTIC;
    project.semantic_events.events[0].value_count = 3;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SEMANTIC_EVENT);
    project.semantic_events.events[0].value_count = 4;
    project.semantic_events.events[0].values[2] = -1.01f;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SEMANTIC_EVENT);
    project.semantic_events.events[0].values[2] = 0.0f;
    project.semantic_events.events[0].timestamp_seconds = 181;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SEMANTIC_EVENT);
}

TEST(project_rejects_authored_workspace_counts_order_duration_and_utf8)
{
    Musi_Project project = valid_project();
    project.lyrics.count = LYRICS_CUE_CAPACITY + 1;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_LYRICS);
    project = valid_project(); project.scene_switches.count = SCENE_SWITCH_CAPACITY + 1;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SCENE_SWITCH);
    project = valid_project(); project.manual_events.count = EVENT_TIMELINE_CAPACITY + 1;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_MANUAL_EVENT);
    project = valid_project(); project.semantic_events.count = EVENT_TIMELINE_CAPACITY + 1;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_SEMANTIC_EVENT);

    project = valid_project(); project.lyrics.next_id=3;project.lyrics.count=2;
    project.lyrics.cues[0]=(Lyric_Cue){.id=1,.start_seconds=2,.end_seconds=3};strcpy(project.lyrics.cues[0].text,"later");
    project.lyrics.cues[1]=(Lyric_Cue){.id=2,.start_seconds=1,.end_seconds=2};strcpy(project.lyrics.cues[1].text,"earlier");
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_LYRICS);
    project.lyrics.cues[0].start_seconds=1;project.lyrics.cues[0].end_seconds=2;
    project.lyrics.cues[1].start_seconds=2;project.lyrics.cues[1].end_seconds=181;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_LYRICS);
    project.lyrics.cues[1].end_seconds=3;project.lyrics.cues[0].text[0]=(char)0xff;project.lyrics.cues[0].text[1]=0;
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_LYRICS);

    project = valid_project();project.manual_events.count=2;
    project.manual_events.events[0]=(Event_Record){.timestamp_seconds=2,.id=1,.type=EVENT_TYPE_CUE,.value_count=1,.values={1}};
    project.manual_events.events[1]=(Event_Record){.timestamp_seconds=1,.id=2,.type=EVENT_TYPE_CUE,.value_count=1,.values={1}};
    EXPECT_EQ_SIZE(musi_project_validate(&project).error, MUSI_PROJECT_ERROR_MANUAL_EVENT);
}
