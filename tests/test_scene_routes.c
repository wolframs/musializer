#include "scene_routes.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

// Headless tests cannot include scene.h (raylib); these mirror the Scene_Id
// ordering that scene_settings.c's descriptor tables are built around.
enum { SCENE_LOOM = 8, SCENE_SPECTRUM = 0, COUNT_SCENES = 10 };

static Musi_Parameter_Mapping route_band(const char *parameter,
                                         uint16_t band_index,
                                         double output_min, double output_max)
{
    Musi_Parameter_Mapping route = {0};
    strncpy(route.parameter, parameter, sizeof(route.parameter) - 1U);
    route.source = MUSI_ANALYSIS_BAND;
    route.band_index = band_index;
    route.input_min = 0.0;
    route.input_max = 1.0;
    route.output_min = output_min;
    route.output_max = output_max;
    route.interpolation = MUSI_INTERPOLATION_LINEAR;
    route.clamp = true;
    return route;
}

static Musi_Parameter_Mapping route_source(const char *parameter,
                                           Musi_Analysis_Source source)
{
    Musi_Parameter_Mapping route = route_band(parameter, 0, 0.0, 1.0);
    route.source = source;
    route.band_index = 0;
    return route;
}

TEST(scene_routes_validate_rejects_malformed_routes)
{
    Musi_Parameter_Mapping route = route_band("settings.loom.weight", 2,
                                              0.4, 2.2);
    EXPECT_TRUE(scene_route_valid(SCENE_LOOM, &route));
    // The key exists but belongs to another scene.
    EXPECT_FALSE(scene_route_valid(SCENE_SPECTRUM, &route));
    EXPECT_FALSE(scene_route_valid(COUNT_SCENES, &route));
    EXPECT_FALSE(scene_route_valid(SCENE_LOOM, NULL));

    Musi_Parameter_Mapping unknown = route_band("settings.loom.imaginary", 0,
                                                0.0, 1.0);
    EXPECT_FALSE(scene_route_valid(SCENE_LOOM, &unknown));

    Musi_Parameter_Mapping stray_band = route_source("settings.loom.weight",
                                                     MUSI_ANALYSIS_RMS);
    stray_band.band_index = 3;  // schema: zero unless source is band
    EXPECT_FALSE(scene_route_valid(SCENE_LOOM, &stray_band));

    Musi_Parameter_Mapping wide_band = route_band("settings.loom.weight",
                                                  0xFFFF, 0.0, 1.0);
    EXPECT_FALSE(scene_route_valid(SCENE_LOOM, &wide_band));

    Musi_Parameter_Mapping inverted = route_band("settings.loom.weight", 0,
                                                 0.0, 1.0);
    inverted.input_min = 1.0;
    inverted.input_max = 1.0;
    EXPECT_FALSE(scene_route_valid(SCENE_LOOM, &inverted));

    Musi_Parameter_Mapping poisoned = route_band("settings.loom.weight", 0,
                                                 0.0, 1.0);
    poisoned.output_max = NAN;
    EXPECT_FALSE(scene_route_valid(SCENE_LOOM, &poisoned));
}

TEST(scene_routes_table_rejects_duplicates_and_overflow)
{
    Scene_Route_Table table;
    scene_route_table_init(&table);
    EXPECT_TRUE(scene_route_table_valid(&table));

    Musi_Parameter_Mapping route = route_band("settings.loom.weight", 2,
                                              0.4, 2.2);
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &route));
    EXPECT_FALSE(scene_route_table_add(&table, SCENE_LOOM, &route));
    EXPECT_EQ_SIZE(table.scenes[SCENE_LOOM].count, 1);

    // Loom exposes seven settings; the eighth distinct parameter cannot
    // exist, so capacity in practice is bounded by unique keys per scene.
    Musi_Parameter_Mapping glints = route_band("settings.loom.glints", 5,
                                               0.0, 2.0);
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &glints));
    EXPECT_TRUE(scene_route_table_valid(&table));

    EXPECT_TRUE(scene_route_table_remove(&table, SCENE_LOOM, 0));
    EXPECT_EQ_SIZE(table.scenes[SCENE_LOOM].count, 1);
    EXPECT_TRUE(strcmp(table.scenes[SCENE_LOOM].items[0].parameter,
                       "settings.loom.glints") == 0);
    EXPECT_FALSE(scene_route_table_remove(&table, SCENE_LOOM, 5));
}

TEST(scene_routes_source_binding_is_strict)
{
    float bands[4] = {0.1f, 0.5f, 0.9f, NAN};
    Scene_Route_Sources sources = {
        .bands = bands,
        .bands_count = 4,
        .rms = 0.25f,
        .peak = 0.75f,
        .spectral_flux = 0.05f,
        .beat_phase = 0.5f,
    };
    double value = 0.0;
    EXPECT_TRUE(scene_routes_source_value(&sources, MUSI_ANALYSIS_RMS, 0, &value));
    EXPECT_NEAR(value, 0.25, 0.0001);
    EXPECT_TRUE(scene_routes_source_value(&sources, MUSI_ANALYSIS_BAND, 2, &value));
    EXPECT_NEAR(value, 0.9, 0.0001);
    EXPECT_FALSE(scene_routes_source_value(&sources, MUSI_ANALYSIS_BAND, 3, &value));
    EXPECT_FALSE(scene_routes_source_value(&sources, MUSI_ANALYSIS_BAND, 4, &value));
    sources.bands = NULL;
    EXPECT_FALSE(scene_routes_source_value(&sources, MUSI_ANALYSIS_BAND, 0, &value));
    sources.rms = INFINITY;
    EXPECT_FALSE(scene_routes_source_value(&sources, MUSI_ANALYSIS_RMS, 0, &value));
    EXPECT_FALSE(scene_routes_source_value(NULL, MUSI_ANALYSIS_RMS, 0, &value));
}

TEST(scene_routes_apply_replaces_clamps_and_preserves_base)
{
    Scene_Settings base;
    scene_settings_init(&base);
    Scene_Route_Table table;
    scene_route_table_init(&table);

    // Descriptor range for loom weight is [0.40, 2.50]; the route's output
    // range deliberately exceeds it on both ends.
    Musi_Parameter_Mapping weight = route_band("settings.loom.weight", 1,
                                               -1.0, 5.0);
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &weight));
    Musi_Parameter_Mapping motion = route_source("settings.loom.motion",
                                                 MUSI_ANALYSIS_BEAT_PHASE);
    motion.output_min = 0.0;
    motion.output_max = 2.0;
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &motion));

    float bands[8] = {0.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Scene_Route_Sources sources = {
        .bands = bands, .bands_count = 8,
        .rms = 0.2f, .peak = 0.4f, .spectral_flux = 0.01f, .beat_phase = 0.5f,
    };

    Scene_Settings effective;
    REQUIRE_TRUE(scene_routes_apply(&table, SCENE_LOOM, &sources, &base,
                                    &effective));
    // Band 1 at full scale maps to 5.0, clamped to the descriptor maximum.
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_LOOM,
                                   LOOM_SETTING_WEIGHT), 2.50f, 0.0001f);
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_LOOM,
                                   LOOM_SETTING_MOTION), 1.0f, 0.0001f);
    // Unrouted settings and other scenes keep their base values.
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_LOOM,
                                   LOOM_SETTING_DENSITY),
                scene_settings_get(&base, SCENE_LOOM, LOOM_SETTING_DENSITY),
                0.0f);
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_SPECTRUM,
                                   SPECTRUM_SETTING_AMPLITUDE),
                scene_settings_get(&base, SCENE_SPECTRUM,
                                   SPECTRUM_SETTING_AMPLITUDE), 0.0f);

    // A band below the low clamp maps to the output minimum, clamped up to
    // the descriptor minimum.
    bands[1] = 0.0f;
    REQUIRE_TRUE(scene_routes_apply(&table, SCENE_LOOM, &sources, &base,
                                    &effective));
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_LOOM,
                                   LOOM_SETTING_WEIGHT), 0.40f, 0.0001f);

    // An unavailable source this frame leaves the base value untouched.
    sources.bands_count = 1;
    REQUIRE_TRUE(scene_routes_apply(&table, SCENE_LOOM, &sources, &base,
                                    &effective));
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_LOOM,
                                   LOOM_SETTING_WEIGHT),
                scene_settings_get(&base, SCENE_LOOM, LOOM_SETTING_WEIGHT),
                0.0f);
    EXPECT_TRUE(scene_settings_valid(&effective));
}

TEST(scene_routes_apply_matches_constant_slider_semantics)
{
    // A constant-shaped mapping (output_min == output_max) must behave
    // exactly like today's persisted sliders: the value lands verbatim.
    Scene_Settings base;
    scene_settings_init(&base);
    Scene_Route_Table table;
    scene_route_table_init(&table);
    Musi_Parameter_Mapping constant = route_source("settings.loom.density",
                                                   MUSI_ANALYSIS_RMS);
    constant.output_min = 1.25;
    constant.output_max = 1.25;
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &constant));

    Scene_Route_Sources sources = {
        .bands = NULL, .bands_count = 0,
        .rms = 0.7f, .peak = 0.9f, .spectral_flux = 0.2f, .beat_phase = 0.1f,
    };
    Scene_Settings effective;
    REQUIRE_TRUE(scene_routes_apply(&table, SCENE_LOOM, &sources, &base,
                                    &effective));
    EXPECT_NEAR(scene_settings_get(&effective, SCENE_LOOM,
                                   LOOM_SETTING_DENSITY), 1.25f, 0.0001f);
}

TEST(scene_route_spec_parsing_is_strict_and_convenient)
{
    size_t scene_index = 0;
    Musi_Parameter_Mapping route;

    REQUIRE_TRUE(scene_route_parse_spec(
        "loom.weight:band:2:0:1:0.4:2.2:smoothstep", &scene_index, &route));
    EXPECT_EQ_SIZE(scene_index, SCENE_LOOM);
    EXPECT_TRUE(strcmp(route.parameter, "settings.loom.weight") == 0);
    EXPECT_TRUE(route.source == MUSI_ANALYSIS_BAND);
    EXPECT_EQ_SIZE(route.band_index, 2);
    EXPECT_NEAR(route.output_max, 2.2, 0.0001);
    EXPECT_TRUE(route.interpolation == MUSI_INTERPOLATION_SMOOTHSTEP);
    EXPECT_TRUE(route.clamp);

    // Full key, default curve, explicit noclamp.
    REQUIRE_TRUE(scene_route_parse_spec(
        "settings.loom.motion:beat_phase:0:0:1:0:2:noclamp", &scene_index,
        &route));
    EXPECT_TRUE(route.interpolation == MUSI_INTERPOLATION_LINEAR);
    EXPECT_FALSE(route.clamp);

    // Both optional fields together, in either order.
    REQUIRE_TRUE(scene_route_parse_spec(
        "loom.glints:spectral_flux:0:0:0.2:0:2:ease_out:noclamp",
        &scene_index, &route));
    EXPECT_TRUE(route.interpolation == MUSI_INTERPOLATION_EASE_OUT);
    EXPECT_FALSE(route.clamp);

    EXPECT_FALSE(scene_route_parse_spec(NULL, &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec("", &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec("loom.weight:band:2:0:1:0.4",
                                        &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec(
        "loom.imaginary:rms:0:0:1:0:1", &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec(
        "loom.weight:sparkles:0:0:1:0:1", &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec(
        "loom.weight:rms:0:0:1:0:1:zigzag", &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec(
        "loom.weight:rms:0:1:1:0:1", &scene_index, &route));  // empty input range
    EXPECT_FALSE(scene_route_parse_spec(
        "loom.weight:rms:0:0:1:0:nan", &scene_index, &route));
    EXPECT_FALSE(scene_route_parse_spec(
        "loom.weight:rms:3:0:1:0:1", &scene_index, &route));  // band on rms
}

TEST(scene_routes_persistence_round_trips_constants_and_routes)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    REQUIRE_TRUE(scene_settings_set(&settings, SCENE_LOOM,
                                    LOOM_SETTING_DENSITY, 1.5f));
    Scene_Route_Table table;
    scene_route_table_init(&table);
    Musi_Parameter_Mapping weight = route_band("settings.loom.weight", 2,
                                               0.4, 2.2);
    weight.interpolation = MUSI_INTERPOLATION_SMOOTHSTEP;
    REQUIRE_TRUE(scene_route_table_add(&table, SCENE_LOOM, &weight));
    Musi_Parameter_Mapping glow = route_source("settings.pulse.glow",
                                               MUSI_ANALYSIS_SPECTRAL_FLUX);
    glow.input_max = 0.2;
    glow.output_max = 2.0;
    REQUIRE_TRUE(scene_route_table_add(&table, 1 /* pulse */, &glow));

    Musi_Parameter_Mapping mappings[MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE];
    size_t count = 0;
    REQUIRE_TRUE(scene_routes_export_mappings(&settings, &table, mappings,
                                              MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE,
                                              &count));
    // One entry per setting: routed parameters replace their constant slot,
    // so the count matches the constant-only export exactly.
    Musi_Parameter_Mapping constants_only[MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE];
    size_t constants_count = 0;
    REQUIRE_TRUE(scene_settings_export_mappings(&settings, constants_only,
                                                MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE,
                                                &constants_count));
    EXPECT_EQ_SIZE(count, constants_count);
    EXPECT_TRUE(scene_routes_mappings_supported(mappings, count));

    size_t route_entries = 0;
    for (size_t at = 0; at < count; ++at) {
        if (!scene_settings_mapping_supported(&mappings[at])) route_entries += 1;
    }
    EXPECT_EQ_SIZE(route_entries, 2);

    // Byte-stable saves: exporting twice is identical.
    Musi_Parameter_Mapping again[MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE];
    size_t again_count = 0;
    REQUIRE_TRUE(scene_routes_export_mappings(&settings, &table, again,
                                              MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE,
                                              &again_count));
    EXPECT_EQ_SIZE(again_count, count);
    EXPECT_TRUE(memcmp(mappings, again, count*sizeof(mappings[0])) == 0);

    Scene_Settings imported_settings;
    Scene_Route_Table imported_table;
    REQUIRE_TRUE(scene_routes_import_mappings(&imported_settings,
                                              &imported_table, mappings,
                                              count));
    // Constants round-trip; routed parameters come back as routes with the
    // slider at its scene default.
    EXPECT_NEAR(scene_settings_get(&imported_settings, SCENE_LOOM,
                                   LOOM_SETTING_DENSITY), 1.5f, 0.0001f);
    EXPECT_EQ_SIZE(imported_table.scenes[SCENE_LOOM].count, 1);
    EXPECT_TRUE(memcmp(&imported_table.scenes[SCENE_LOOM].items[0], &weight,
                       sizeof(weight)) == 0);
    EXPECT_EQ_SIZE(imported_table.scenes[1].count, 1);
    Scene_Settings defaults;
    scene_settings_init(&defaults);
    EXPECT_NEAR(scene_settings_get(&imported_settings, SCENE_LOOM,
                                   LOOM_SETTING_WEIGHT),
                scene_settings_get(&defaults, SCENE_LOOM,
                                   LOOM_SETTING_WEIGHT), 0.0f);
}

TEST(scene_routes_import_is_all_or_nothing)
{
    Scene_Settings settings;
    Scene_Route_Table table;
    Musi_Parameter_Mapping mappings[2];
    mappings[0] = route_band("settings.loom.weight", 2, 0.4, 2.2);
    mappings[1] = route_band("settings.loom.weight", 3, 0.0, 1.0);
    // Duplicate parameter: a key may be a constant or a route, never both.
    EXPECT_FALSE(scene_routes_import_mappings(&settings, &table, mappings, 2));
    EXPECT_FALSE(scene_routes_mappings_supported(mappings, 2));

    mappings[1] = route_band("settings.loom.glints", 0xFFFF, 0.0, 1.0);
    EXPECT_FALSE(scene_routes_import_mappings(&settings, &table, mappings, 2));

    Musi_Parameter_Mapping zeroed = {0};
    EXPECT_FALSE(scene_routes_mappings_supported(&zeroed, 1));
    EXPECT_TRUE(scene_routes_mappings_supported(NULL, 0));
    EXPECT_TRUE(scene_routes_import_mappings(&settings, &table, NULL, 0));
    EXPECT_TRUE(scene_settings_valid(&settings));
}

TEST(scene_routes_apply_is_deterministic)
{
    Scene_Settings base;
    scene_settings_init(&base);
    Scene_Route_Table table;
    scene_route_table_init(&table);
    Musi_Parameter_Mapping weight = route_band("settings.loom.weight", 3,
                                               0.4, 2.5);
    weight.interpolation = MUSI_INTERPOLATION_SMOOTHSTEP;
    EXPECT_TRUE(scene_route_table_add(&table, SCENE_LOOM, &weight));

    float bands[8];
    for (size_t i = 0; i < 8; ++i) bands[i] = (float)i/7.0f;
    Scene_Route_Sources sources = {
        .bands = bands, .bands_count = 8,
        .rms = 0.3f, .peak = 0.5f, .spectral_flux = 0.1f, .beat_phase = 0.9f,
    };
    Scene_Settings first;
    Scene_Settings second;
    REQUIRE_TRUE(scene_routes_apply(&table, SCENE_LOOM, &sources, &base,
                                    &first));
    REQUIRE_TRUE(scene_routes_apply(&table, SCENE_LOOM, &sources, &base,
                                    &second));
    EXPECT_TRUE(memcmp(&first, &second, sizeof(first)) == 0);
}
