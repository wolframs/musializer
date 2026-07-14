#ifndef MUSIALIZER_PROJECT_H_
#define MUSIALIZER_PROJECT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ascii_art.h"
#include "event_timeline.h"
#include "lyrics.h"
#include "scene_switch.h"

#define MUSI_PROJECT_SCHEMA_VERSION 1u
#define MUSI_PROJECT_MAX_SCENES 32u
#define MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE 64u
#define MUSI_PROJECT_MAX_CUES 256u
#define MUSI_PROJECT_MAX_ANALYSIS_LANES 8u
#define MUSI_PROJECT_MAX_SCENE_PRESETS 56u

#define MUSI_PROJECT_ID_CAPACITY 65u
#define MUSI_PROJECT_NAME_CAPACITY 129u
#define MUSI_PROJECT_PATH_CAPACITY 1025u
#define MUSI_PROJECT_TYPE_CAPACITY 65u
#define MUSI_PROJECT_PARAMETER_CAPACITY 65u
#define MUSI_PROJECT_VERSION_CAPACITY 65u
#define MUSI_PROJECT_PROVIDER_CAPACITY 129u
#define MUSI_PROJECT_TIMESTAMP_CAPACITY 33u

typedef enum Musi_Asset_Mode {
    MUSI_ASSET_IMPORTED = 0,
    MUSI_ASSET_REFERENCED = 1,
    MUSI_ASSET_MODE_COUNT
} Musi_Asset_Mode;

typedef enum Musi_Output_Format {
    MUSI_OUTPUT_MP4_H264 = 0,
    MUSI_OUTPUT_MKV_H264 = 1,
    MUSI_OUTPUT_WEBM_VP9 = 2,
    MUSI_OUTPUT_MOV_PRORES = 3,
    MUSI_OUTPUT_PNG_SEQUENCE = 4,
    MUSI_OUTPUT_FORMAT_COUNT
} Musi_Output_Format;

typedef enum Musi_Output_Quality {
    MUSI_OUTPUT_QUALITY_BALANCED = 0,
    MUSI_OUTPUT_QUALITY_HIGH = 1,
    MUSI_OUTPUT_QUALITY_MASTER = 2,
    MUSI_OUTPUT_QUALITY_COUNT
} Musi_Output_Quality;

typedef enum Musi_Blend_Mode {
    MUSI_BLEND_NORMAL = 0,
    MUSI_BLEND_ADD = 1,
    MUSI_BLEND_MULTIPLY = 2,
    MUSI_BLEND_SCREEN = 3,
    MUSI_BLEND_MODE_COUNT
} Musi_Blend_Mode;

typedef enum Musi_Analysis_Source {
    MUSI_ANALYSIS_RMS = 0,
    MUSI_ANALYSIS_PEAK = 1,
    MUSI_ANALYSIS_SPECTRAL_FLUX = 2,
    MUSI_ANALYSIS_BEAT_PHASE = 3,
    MUSI_ANALYSIS_BAND = 4,
    MUSI_ANALYSIS_SOURCE_COUNT
} Musi_Analysis_Source;

typedef enum Musi_Interpolation {
    MUSI_INTERPOLATION_STEP = 0,
    MUSI_INTERPOLATION_LINEAR = 1,
    MUSI_INTERPOLATION_SMOOTHSTEP = 2,
    MUSI_INTERPOLATION_EASE_IN = 3,
    MUSI_INTERPOLATION_EASE_OUT = 4,
    MUSI_INTERPOLATION_COUNT
} Musi_Interpolation;

typedef enum Musi_Analysis_Lane_Kind {
    MUSI_LANE_MEASURED_SIGNAL = 0,
    MUSI_LANE_LYRIC_TIMING = 1,
    MUSI_LANE_SEMANTIC_SCORE = 2,
    MUSI_LANE_KIND_COUNT
} Musi_Analysis_Lane_Kind;

typedef struct Musi_Project_Metadata {
    char project_id[MUSI_PROJECT_ID_CAPACITY];
    char title[MUSI_PROJECT_NAME_CAPACITY];
    char author[MUSI_PROJECT_NAME_CAPACITY];
    char created_utc[MUSI_PROJECT_TIMESTAMP_CAPACITY];
    char modified_utc[MUSI_PROJECT_TIMESTAMP_CAPACITY];
    char application_version[MUSI_PROJECT_VERSION_CAPACITY];
} Musi_Project_Metadata;

typedef struct Musi_Audio_Asset {
    Musi_Asset_Mode mode;
    char path[MUSI_PROJECT_PATH_CAPACITY];
    char sha256[MUSI_PROJECT_ID_CAPACITY];
    double duration_seconds;
    uint32_t sample_rate;
    uint16_t channels;
} Musi_Audio_Asset;

typedef struct Musi_Ascii_Image_Asset {
    bool present;
    char path[MUSI_PROJECT_PATH_CAPACITY];
    char sha256[MUSI_PROJECT_ID_CAPACITY];
    uint32_t columns;
    uint32_t rows;
} Musi_Ascii_Image_Asset;

typedef struct Musi_Output_Settings {
    uint32_t width;
    uint32_t height;
    uint32_t fps_numerator;
    uint32_t fps_denominator;
    double start_seconds;
    double end_seconds;
    Musi_Output_Format format;
    // Supersampling and encoder details are derived from this durable intent.
    Musi_Output_Quality quality;
} Musi_Output_Settings;

typedef struct Musi_Parameter_Mapping {
    char parameter[MUSI_PROJECT_PARAMETER_CAPACITY];
    Musi_Analysis_Source source;
    uint16_t band_index;
    double input_min;
    double input_max;
    double output_min;
    double output_max;
    Musi_Interpolation interpolation;
    bool clamp;
} Musi_Parameter_Mapping;

typedef struct Musi_Scene_Entry {
    uint64_t instance_id;
    char scene_type[MUSI_PROJECT_TYPE_CAPACITY];
    bool enabled;
    double start_seconds;
    double end_seconds;
    double opacity;
    Musi_Blend_Mode blend_mode;
    size_t mapping_count;
    Musi_Parameter_Mapping mappings[MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE];
} Musi_Scene_Entry;

typedef struct Musi_Parameter_Cue {
    uint64_t cue_id;
    uint64_t target_scene_id;
    char parameter[MUSI_PROJECT_PARAMETER_CAPACITY];
    double start_seconds;
    double end_seconds;
    double from_value;
    double to_value;
    Musi_Interpolation interpolation;
} Musi_Parameter_Cue;

typedef struct Musi_Analysis_Provenance {
    char adapter[MUSI_PROJECT_TYPE_CAPACITY];
    char adapter_version[MUSI_PROJECT_VERSION_CAPACITY];
    char schema_version[MUSI_PROJECT_VERSION_CAPACITY];
    char model[MUSI_PROJECT_PROVIDER_CAPACITY];
    char provider[MUSI_PROJECT_PROVIDER_CAPACITY];
    char prompt_version[MUSI_PROJECT_VERSION_CAPACITY];
} Musi_Analysis_Provenance;

typedef struct Musi_Analysis_Lane_Reference {
    Musi_Analysis_Lane_Kind kind;
    char path[MUSI_PROJECT_PATH_CAPACITY];
    char sha256[MUSI_PROJECT_ID_CAPACITY];
    char audio_sha256[MUSI_PROJECT_ID_CAPACITY];
    Musi_Analysis_Provenance provenance;
} Musi_Analysis_Lane_Reference;

typedef struct Musi_Scene_Switch_Suggestion {
    uint64_t id;
    double start_seconds;
    double end_seconds;
    char scene_name[MUSI_PROJECT_TYPE_CAPACITY];
    float strength;
    size_t setting_count;
    float settings[SCENE_SETTINGS_MAX_CONTROLS];
} Musi_Scene_Switch_Suggestion;

typedef struct Musi_Scene_Switch_Suggestions {
    bool enabled;
    size_t count;
    Musi_Scene_Switch_Suggestion cues[SCENE_SWITCH_CAPACITY];
} Musi_Scene_Switch_Suggestions;

typedef struct Musi_Scene_Preset {
    uint64_t id;
    char scene_name[MUSI_PROJECT_TYPE_CAPACITY];
    char name[MUSI_PROJECT_NAME_CAPACITY];
    size_t setting_count;
    float settings[SCENE_SETTINGS_MAX_CONTROLS];
} Musi_Scene_Preset;

typedef struct Musi_Project {
    uint32_t schema_version;
    Musi_Project_Metadata metadata;
    Musi_Audio_Asset audio;
    Musi_Ascii_Image_Asset ascii_image;
    Musi_Output_Settings output;
    uint64_t deterministic_seed;
    size_t scene_count;
    Musi_Scene_Entry scenes[MUSI_PROJECT_MAX_SCENES];
    size_t cue_count;
    Musi_Parameter_Cue cues[MUSI_PROJECT_MAX_CUES];
    size_t analysis_lane_count;
    Musi_Analysis_Lane_Reference analysis_lanes[MUSI_PROJECT_MAX_ANALYSIS_LANES];
    Lyrics_Document lyrics;
    Musi_Scene_Switch_Suggestions scene_switches;
    size_t scene_preset_count;
    Musi_Scene_Preset scene_presets[MUSI_PROJECT_MAX_SCENE_PRESETS];
    // Validated model-derived semantic values are embedded project data. The
    // analysis_lanes entries above are provenance metadata, not dependencies
    // required to reconstruct this evaluated lane.
    Event_Timeline semantic_events;
    // This lane contains user-authored events only. Semantic model output stays
    // separate and must not be copied here as if manually authored.
    Event_Timeline manual_events;
} Musi_Project;

typedef enum Musi_Project_Error {
    MUSI_PROJECT_VALID = 0,
    MUSI_PROJECT_ERROR_NULL,
    MUSI_PROJECT_ERROR_SCHEMA_VERSION,
    MUSI_PROJECT_ERROR_METADATA,
    MUSI_PROJECT_ERROR_AUDIO,
    MUSI_PROJECT_ERROR_ASCII_IMAGE,
    MUSI_PROJECT_ERROR_OUTPUT,
    MUSI_PROJECT_ERROR_COUNT,
    MUSI_PROJECT_ERROR_SCENE,
    MUSI_PROJECT_ERROR_MAPPING,
    MUSI_PROJECT_ERROR_CUE,
    MUSI_PROJECT_ERROR_CUE_ORDER,
    MUSI_PROJECT_ERROR_CUE_OVERLAP,
    MUSI_PROJECT_ERROR_ANALYSIS_LANE,
    MUSI_PROJECT_ERROR_DUPLICATE_ID,
    MUSI_PROJECT_ERROR_LYRICS,
    MUSI_PROJECT_ERROR_SCENE_SWITCH,
    MUSI_PROJECT_ERROR_SCENE_PRESET,
    MUSI_PROJECT_ERROR_MANUAL_EVENT,
    MUSI_PROJECT_ERROR_SEMANTIC_EVENT
} Musi_Project_Error;

typedef struct Musi_Project_Validation {
    Musi_Project_Error error;
    size_t index;
    size_t subindex;
} Musi_Project_Validation;

typedef enum Musi_Project_Editor_Support {
    MUSI_PROJECT_EDITOR_SUPPORTED = 0,
    MUSI_PROJECT_EDITOR_ERROR_NULL,
    MUSI_PROJECT_EDITOR_ERROR_AUDIO_MODE,
    MUSI_PROJECT_EDITOR_ERROR_OUTPUT_RANGE,
    MUSI_PROJECT_EDITOR_ERROR_OUTPUT_FORMAT,
    MUSI_PROJECT_EDITOR_ERROR_SCENE_COUNT,
    MUSI_PROJECT_EDITOR_ERROR_PARAMETER_CUES,
    MUSI_PROJECT_EDITOR_ERROR_SCENE_LAYOUT,
    MUSI_PROJECT_EDITOR_ERROR_SCENE_MAPPINGS,
} Musi_Project_Editor_Support;

void musi_project_init(Musi_Project *project);
Musi_Project_Validation musi_project_validate(const Musi_Project *project);
const char *musi_project_error_string(Musi_Project_Error error);

// The schema is intentionally broader than today's single-scene MP4 editor.
// This check prevents open/edit/autosave from silently normalizing valid fields
// that the current UI cannot represent yet.
Musi_Project_Editor_Support musi_project_editor_support(
    const Musi_Project *project);
const char *musi_project_editor_support_string(Musi_Project_Editor_Support support);
bool musi_project_audio_metadata_matches(const Musi_Project *project,
                                         double decoded_duration_seconds,
                                         uint32_t decoded_sample_rate,
                                         uint16_t decoded_channels,
                                         double duration_tolerance_seconds);

double musi_interpolate(double from_value, double to_value, double amount,
                        Musi_Interpolation interpolation);
bool musi_mapping_evaluate(const Musi_Parameter_Mapping *mapping,
                           double source_value, double *result);
bool musi_project_parameter_at(const Musi_Project *project,
                               uint64_t target_scene_id,
                               const char *parameter,
                               double base_value,
                               double time_seconds,
                               double *result);

const char *musi_asset_mode_name(Musi_Asset_Mode value);
const char *musi_output_format_name(Musi_Output_Format value);
const char *musi_output_quality_name(Musi_Output_Quality value);
const char *musi_blend_mode_name(Musi_Blend_Mode value);
const char *musi_analysis_source_name(Musi_Analysis_Source value);
const char *musi_interpolation_name(Musi_Interpolation value);
const char *musi_analysis_lane_kind_name(Musi_Analysis_Lane_Kind value);

#endif // MUSIALIZER_PROJECT_H_
