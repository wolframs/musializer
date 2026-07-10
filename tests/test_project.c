#include "project.h"
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
    EXPECT_EQ_SIZE(project.output.width, 1600);
    EXPECT_EQ_SIZE(project.output.height, 900);
    EXPECT_EQ_SIZE(project.output.fps_numerator, 30);
    EXPECT_EQ_SIZE(project.output.fps_denominator, 1);
    EXPECT_TRUE(musi_project_validate(&project).error != MUSI_PROJECT_VALID);

    project = valid_project();
    result = musi_project_validate(&project);
    EXPECT_EQ_SIZE(result.error, MUSI_PROJECT_VALID);
    EXPECT_TRUE(strcmp(musi_analysis_lane_kind_name(MUSI_LANE_SEMANTIC_SCORE),
                       "semantic_score") == 0);
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
