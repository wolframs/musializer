#include "project.h"

#include <math.h>
#include <string.h>

static Musi_Project_Validation validation(Musi_Project_Error error, size_t index, size_t subindex)
{
    Musi_Project_Validation result = {error, index, subindex};
    return result;
}

static bool bounded_string(const char *value, size_t capacity, bool allow_empty)
{
    const char *end = (const char *) memchr(value, '\0', capacity);
    return end != NULL && (allow_empty || end != value);
}

static bool stable_name(const char *value, size_t capacity)
{
    size_t i = 0;
    if (!bounded_string(value, capacity, false)) return false;
    if (!((value[0] >= 'a' && value[0] <= 'z') ||
          (value[0] >= 'A' && value[0] <= 'Z') ||
          (value[0] >= '0' && value[0] <= '9'))) return false;
    for (; value[i] != '\0'; ++i) {
        const char c = value[i];
        const bool valid = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') ||
                           c == '.' || c == '_' || c == '-' || c == ':';
        if (!valid) return false;
    }
    return true;
}

static bool sha256_string(const char *value)
{
    size_t i = 0;
    for (; i < 64; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return value[64] == '\0';
}

static bool enum_in_range(int value, int count)
{
    return value >= 0 && value < count;
}

void musi_project_init(Musi_Project *project)
{
    if (project == NULL) return;
    memset(project, 0, sizeof(*project));
    project->schema_version = MUSI_PROJECT_SCHEMA_VERSION;
    project->audio.mode = MUSI_ASSET_IMPORTED;
    project->output.width = 1600;
    project->output.height = 900;
    project->output.fps_numerator = 30;
    project->output.fps_denominator = 1;
    project->output.format = MUSI_OUTPUT_MP4_H264;
}

static bool scene_id_exists(const Musi_Project *project, uint64_t id)
{
    size_t i = 0;
    for (; i < project->scene_count; ++i) {
        if (project->scenes[i].instance_id == id) return true;
    }
    return false;
}

Musi_Project_Validation musi_project_validate(const Musi_Project *project)
{
    size_t i = 0;
    size_t j = 0;

    if (project == NULL) return validation(MUSI_PROJECT_ERROR_NULL, 0, 0);
    if (project->schema_version != MUSI_PROJECT_SCHEMA_VERSION) {
        return validation(MUSI_PROJECT_ERROR_SCHEMA_VERSION, 0, 0);
    }
    if (!stable_name(project->metadata.project_id, sizeof(project->metadata.project_id)) ||
        !bounded_string(project->metadata.title, sizeof(project->metadata.title), false) ||
        !bounded_string(project->metadata.author, sizeof(project->metadata.author), true) ||
        !bounded_string(project->metadata.created_utc, sizeof(project->metadata.created_utc), true) ||
        !bounded_string(project->metadata.modified_utc, sizeof(project->metadata.modified_utc), true) ||
        !bounded_string(project->metadata.application_version,
                        sizeof(project->metadata.application_version), false)) {
        return validation(MUSI_PROJECT_ERROR_METADATA, 0, 0);
    }
    if (!enum_in_range((int) project->audio.mode, MUSI_ASSET_MODE_COUNT) ||
        !bounded_string(project->audio.path, sizeof(project->audio.path), false) ||
        !sha256_string(project->audio.sha256) ||
        !isfinite(project->audio.duration_seconds) || project->audio.duration_seconds <= 0.0 ||
        project->audio.sample_rate == 0 || project->audio.sample_rate > 768000 ||
        project->audio.channels == 0 || project->audio.channels > 64) {
        return validation(MUSI_PROJECT_ERROR_AUDIO, 0, 0);
    }
    if (project->output.width < 16 || project->output.width > 16384 ||
        project->output.height < 16 || project->output.height > 16384 ||
        project->output.fps_numerator == 0 || project->output.fps_numerator > 1000 ||
        project->output.fps_denominator == 0 || project->output.fps_denominator > 1001 ||
        !isfinite(project->output.start_seconds) || project->output.start_seconds < 0.0 ||
        !isfinite(project->output.end_seconds) ||
        project->output.end_seconds <= project->output.start_seconds ||
        project->output.end_seconds > project->audio.duration_seconds ||
        !enum_in_range((int) project->output.format, MUSI_OUTPUT_FORMAT_COUNT)) {
        return validation(MUSI_PROJECT_ERROR_OUTPUT, 0, 0);
    }
    if (project->scene_count == 0 || project->scene_count > MUSI_PROJECT_MAX_SCENES ||
        project->cue_count > MUSI_PROJECT_MAX_CUES ||
        project->analysis_lane_count > MUSI_PROJECT_MAX_ANALYSIS_LANES) {
        return validation(MUSI_PROJECT_ERROR_COUNT, 0, 0);
    }

    for (i = 0; i < project->scene_count; ++i) {
        const Musi_Scene_Entry *scene = &project->scenes[i];
        if (scene->instance_id == 0 ||
            !stable_name(scene->scene_type, sizeof(scene->scene_type)) ||
            !isfinite(scene->start_seconds) || scene->start_seconds < 0.0 ||
            !isfinite(scene->end_seconds) || scene->end_seconds <= scene->start_seconds ||
            scene->end_seconds > project->audio.duration_seconds ||
            !isfinite(scene->opacity) || scene->opacity < 0.0 || scene->opacity > 1.0 ||
            !enum_in_range((int) scene->blend_mode, MUSI_BLEND_MODE_COUNT)) {
            return validation(MUSI_PROJECT_ERROR_SCENE, i, 0);
        }
        if (scene->mapping_count > MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE) {
            return validation(MUSI_PROJECT_ERROR_COUNT, i, 0);
        }
        for (j = 0; j < i; ++j) {
            if (project->scenes[j].instance_id == scene->instance_id) {
                return validation(MUSI_PROJECT_ERROR_DUPLICATE_ID, i, j);
            }
        }
        for (j = 0; j < scene->mapping_count; ++j) {
            const Musi_Parameter_Mapping *mapping = &scene->mappings[j];
            size_t k = 0;
            if (!stable_name(mapping->parameter, sizeof(mapping->parameter)) ||
                !enum_in_range((int) mapping->source, MUSI_ANALYSIS_SOURCE_COUNT) ||
                (mapping->source != MUSI_ANALYSIS_BAND && mapping->band_index != 0) ||
                !isfinite(mapping->input_min) || !isfinite(mapping->input_max) ||
                mapping->input_max <= mapping->input_min ||
                !isfinite(mapping->output_min) || !isfinite(mapping->output_max) ||
                !enum_in_range((int) mapping->interpolation, MUSI_INTERPOLATION_COUNT)) {
                return validation(MUSI_PROJECT_ERROR_MAPPING, i, j);
            }
            for (k = 0; k < j; ++k) {
                if (strcmp(scene->mappings[k].parameter, mapping->parameter) == 0) {
                    return validation(MUSI_PROJECT_ERROR_MAPPING, i, j);
                }
            }
        }
    }

    for (i = 0; i < project->cue_count; ++i) {
        const Musi_Parameter_Cue *cue = &project->cues[i];
        if (cue->cue_id == 0 || cue->target_scene_id == 0 ||
            !scene_id_exists(project, cue->target_scene_id) ||
            !stable_name(cue->parameter, sizeof(cue->parameter)) ||
            !isfinite(cue->start_seconds) || cue->start_seconds < 0.0 ||
            !isfinite(cue->end_seconds) || cue->end_seconds <= cue->start_seconds ||
            cue->end_seconds > project->audio.duration_seconds ||
            !isfinite(cue->from_value) || !isfinite(cue->to_value) ||
            !enum_in_range((int) cue->interpolation, MUSI_INTERPOLATION_COUNT)) {
            return validation(MUSI_PROJECT_ERROR_CUE, i, 0);
        }
        if (i > 0 && (cue->start_seconds < project->cues[i - 1].start_seconds ||
                      (cue->start_seconds == project->cues[i - 1].start_seconds &&
                       cue->cue_id <= project->cues[i - 1].cue_id))) {
            return validation(MUSI_PROJECT_ERROR_CUE_ORDER, i, i - 1);
        }
        for (j = 0; j < i; ++j) {
            const Musi_Parameter_Cue *previous = &project->cues[j];
            if (previous->cue_id == cue->cue_id) {
                return validation(MUSI_PROJECT_ERROR_DUPLICATE_ID, i, j);
            }
            if (previous->target_scene_id == cue->target_scene_id &&
                strcmp(previous->parameter, cue->parameter) == 0 &&
                cue->start_seconds < previous->end_seconds) {
                return validation(MUSI_PROJECT_ERROR_CUE_OVERLAP, i, j);
            }
        }
    }

    for (i = 0; i < project->analysis_lane_count; ++i) {
        const Musi_Analysis_Lane_Reference *lane = &project->analysis_lanes[i];
        const Musi_Analysis_Provenance *provenance = &lane->provenance;
        if (!enum_in_range((int) lane->kind, MUSI_LANE_KIND_COUNT) ||
            !bounded_string(lane->path, sizeof(lane->path), false) ||
            !sha256_string(lane->sha256) || !sha256_string(lane->audio_sha256) ||
            strcmp(lane->audio_sha256, project->audio.sha256) != 0 ||
            !stable_name(provenance->adapter, sizeof(provenance->adapter)) ||
            !bounded_string(provenance->adapter_version,
                            sizeof(provenance->adapter_version), false) ||
            !bounded_string(provenance->schema_version,
                            sizeof(provenance->schema_version), false) ||
            !bounded_string(provenance->model, sizeof(provenance->model), true) ||
            !bounded_string(provenance->provider, sizeof(provenance->provider), true) ||
            !bounded_string(provenance->prompt_version,
                            sizeof(provenance->prompt_version), true)) {
            return validation(MUSI_PROJECT_ERROR_ANALYSIS_LANE, i, 0);
        }
        for (j = 0; j < i; ++j) {
            if (project->analysis_lanes[j].kind == lane->kind) {
                return validation(MUSI_PROJECT_ERROR_ANALYSIS_LANE, i, j);
            }
        }
    }

    return validation(MUSI_PROJECT_VALID, 0, 0);
}

const char *musi_project_error_string(Musi_Project_Error error)
{
    static const char *const names[] = {
        "valid", "null project", "unsupported schema version", "invalid metadata",
        "invalid audio asset", "invalid output settings", "capacity/count violation",
        "invalid scene", "invalid parameter mapping", "invalid cue", "cues are unsorted",
        "cues overlap", "invalid analysis lane", "duplicate stable id"
    };
    if (!enum_in_range((int) error, (int) (sizeof(names) / sizeof(names[0])))) {
        return "unknown project error";
    }
    return names[error];
}

static double interpolate_amount(double amount, Musi_Interpolation interpolation)
{
    double shaped = amount;
    switch (interpolation) {
    case MUSI_INTERPOLATION_STEP: shaped = shaped < 1.0 ? 0.0 : 1.0; break;
    case MUSI_INTERPOLATION_LINEAR: break;
    case MUSI_INTERPOLATION_SMOOTHSTEP: shaped = shaped * shaped * (3.0 - 2.0 * shaped); break;
    case MUSI_INTERPOLATION_EASE_IN: shaped *= shaped; break;
    case MUSI_INTERPOLATION_EASE_OUT: shaped = 1.0 - (1.0 - shaped) * (1.0 - shaped); break;
    case MUSI_INTERPOLATION_COUNT: return NAN;
    }
    return shaped;
}

double musi_interpolate(double from_value, double to_value, double amount,
                        Musi_Interpolation interpolation)
{
    double shaped = amount;
    if (!isfinite(from_value) || !isfinite(to_value) || !isfinite(amount) ||
        !enum_in_range((int) interpolation, MUSI_INTERPOLATION_COUNT)) return NAN;
    if (shaped < 0.0) shaped = 0.0;
    if (shaped > 1.0) shaped = 1.0;
    shaped = interpolate_amount(shaped, interpolation);
    return from_value + (to_value - from_value) * shaped;
}

bool musi_mapping_evaluate(const Musi_Parameter_Mapping *mapping,
                           double source_value, double *result)
{
    double amount = 0.0;
    if (mapping == NULL || result == NULL || !isfinite(source_value) ||
        !isfinite(mapping->input_min) || !isfinite(mapping->input_max) ||
        mapping->input_max <= mapping->input_min ||
        !isfinite(mapping->output_min) || !isfinite(mapping->output_max) ||
        !enum_in_range((int) mapping->interpolation, MUSI_INTERPOLATION_COUNT)) return false;
    amount = (source_value - mapping->input_min) /
             (mapping->input_max - mapping->input_min);
    if (mapping->clamp) {
        if (amount < 0.0) amount = 0.0;
        if (amount > 1.0) amount = 1.0;
    }
    if (mapping->clamp) {
        *result = musi_interpolate(mapping->output_min, mapping->output_max,
                                   amount, mapping->interpolation);
    } else {
        amount = interpolate_amount(amount, mapping->interpolation);
        *result = mapping->output_min +
                  (mapping->output_max - mapping->output_min) * amount;
    }
    return isfinite(*result);
}

bool musi_project_parameter_at(const Musi_Project *project,
                               uint64_t target_scene_id,
                               const char *parameter,
                               double base_value,
                               double time_seconds,
                               double *result)
{
    size_t i = 0;
    double value = base_value;
    if (project == NULL || parameter == NULL || result == NULL || target_scene_id == 0 ||
        !isfinite(base_value) || !isfinite(time_seconds) || time_seconds < 0.0 ||
        project->cue_count > MUSI_PROJECT_MAX_CUES) return false;

    for (i = 0; i < project->cue_count; ++i) {
        const Musi_Parameter_Cue *cue = &project->cues[i];
        if (cue->target_scene_id != target_scene_id ||
            strcmp(cue->parameter, parameter) != 0) continue;
        if (time_seconds < cue->start_seconds) break;
        if (time_seconds >= cue->end_seconds) {
            value = cue->to_value;
            continue;
        }
        value = musi_interpolate(cue->from_value, cue->to_value,
                                 (time_seconds - cue->start_seconds) /
                                 (cue->end_seconds - cue->start_seconds),
                                 cue->interpolation);
        *result = value;
        return isfinite(value);
    }
    *result = value;
    return isfinite(value);
}

#define MUSI_NAME_FUNCTION(function_name, enum_type, count_value, ...)            \
    const char *function_name(enum_type value)                                    \
    {                                                                              \
        static const char *const names[] = {__VA_ARGS__};                          \
        if (!enum_in_range((int) value, count_value)) return NULL;                 \
        return names[value];                                                       \
    }

MUSI_NAME_FUNCTION(musi_asset_mode_name, Musi_Asset_Mode, MUSI_ASSET_MODE_COUNT,
                   "imported", "referenced")
MUSI_NAME_FUNCTION(musi_output_format_name, Musi_Output_Format, MUSI_OUTPUT_FORMAT_COUNT,
                   "mp4_h264", "mkv_h264", "webm_vp9", "mov_prores", "png_sequence")
MUSI_NAME_FUNCTION(musi_blend_mode_name, Musi_Blend_Mode, MUSI_BLEND_MODE_COUNT,
                   "normal", "add", "multiply", "screen")
MUSI_NAME_FUNCTION(musi_analysis_source_name, Musi_Analysis_Source, MUSI_ANALYSIS_SOURCE_COUNT,
                   "rms", "peak", "spectral_flux", "beat_phase", "band")
MUSI_NAME_FUNCTION(musi_interpolation_name, Musi_Interpolation, MUSI_INTERPOLATION_COUNT,
                   "step", "linear", "smoothstep", "ease_in", "ease_out")
MUSI_NAME_FUNCTION(musi_analysis_lane_kind_name, Musi_Analysis_Lane_Kind, MUSI_LANE_KIND_COUNT,
                   "measured_signal", "lyric_timing", "semantic_score")

#undef MUSI_NAME_FUNCTION
