#include "project.h"

#include "scene_routes.h"
#include "scene_settings.h"

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
    project->output.width = 1920;
    project->output.height = 1080;
    project->output.fps_numerator = 30;
    project->output.fps_denominator = 1;
    project->output.format = MUSI_OUTPUT_MP4_H264;
    project->output.quality = MUSI_OUTPUT_QUALITY_HIGH;
    musi_caption_style_init(&project->caption_style);
    project->lyrics.schema_version = LYRICS_DOCUMENT_SCHEMA_VERSION;
    project->lyrics.next_id = 1;
    project->lyrics.revision = 1;
    event_timeline_init(&project->semantic_events);
    event_timeline_init(&project->manual_events);
}

void musi_caption_style_init(Musi_Caption_Style *style)
{
    if (style == NULL) return;
    memset(style, 0, sizeof(*style));
    style->face = MUSI_CAPTION_FACE_ALEGREYA;
    style->box = MUSI_CAPTION_BOX_PLATE;
    style->anchor = MUSI_CAPTION_ANCHOR_BOTTOM_CENTER;
    style->size_scale = MUSI_CAPTION_SIZE_DEFAULT;
    style->margin_scale = MUSI_CAPTION_MARGIN_DEFAULT;
    style->width_scale = MUSI_CAPTION_WIDTH_DEFAULT;
    style->text_rgba = MUSI_CAPTION_TEXT_RGBA_DEFAULT;
    style->box_rgba = MUSI_CAPTION_BOX_RGBA_DEFAULT;
}

bool musi_caption_style_is_default(const Musi_Caption_Style *style)
{
    if (style == NULL) return false;
    Musi_Caption_Style shipped;
    musi_caption_style_init(&shipped);
    return style->face == shipped.face && style->box == shipped.box &&
           style->anchor == shipped.anchor &&
           style->size_scale == shipped.size_scale &&
           style->margin_scale == shipped.margin_scale &&
           style->width_scale == shipped.width_scale &&
           style->text_rgba == shipped.text_rgba &&
           style->box_rgba == shipped.box_rgba && !style->font.present;
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
    const Musi_Ascii_Image_Asset *ascii = &project->ascii_image;
    if ((!ascii->present &&
         (ascii->path[0] != '\0' || ascii->sha256[0] != '\0' ||
          ascii->columns != 0 || ascii->rows != 0)) ||
        (ascii->present &&
         (!bounded_string(ascii->path, sizeof(ascii->path), false) ||
          !sha256_string(ascii->sha256) || ascii->columns == 0 ||
          ascii->columns > ASCII_GRID_MAX_COLUMNS || ascii->rows == 0 ||
          ascii->rows > ASCII_GRID_MAX_ROWS))) {
        return validation(MUSI_PROJECT_ERROR_ASCII_IMAGE, 0, 0);
    }
    const Musi_Caption_Style *caption = &project->caption_style;
    // The imported face and the font asset are one fact stated twice. Either
    // arrangement where they disagree would open a project whose captions are
    // typeset in a face the file does not carry.
    if (!enum_in_range((int) caption->face, MUSI_CAPTION_FACE_COUNT) ||
        !enum_in_range((int) caption->box, MUSI_CAPTION_BOX_COUNT) ||
        !enum_in_range((int) caption->anchor, MUSI_CAPTION_ANCHOR_COUNT) ||
        !isfinite(caption->size_scale) ||
        caption->size_scale < MUSI_CAPTION_SIZE_MINIMUM ||
        caption->size_scale > MUSI_CAPTION_SIZE_MAXIMUM ||
        !isfinite(caption->margin_scale) ||
        caption->margin_scale < MUSI_CAPTION_MARGIN_MINIMUM ||
        caption->margin_scale > MUSI_CAPTION_MARGIN_MAXIMUM ||
        !isfinite(caption->width_scale) ||
        caption->width_scale < MUSI_CAPTION_WIDTH_MINIMUM ||
        caption->width_scale > MUSI_CAPTION_WIDTH_MAXIMUM ||
        (caption->face == MUSI_CAPTION_FACE_IMPORTED) != caption->font.present ||
        (!caption->font.present &&
         (caption->font.path[0] != '\0' || caption->font.sha256[0] != '\0' ||
          caption->font.family[0] != '\0')) ||
        (caption->font.present &&
         (!bounded_string(caption->font.path, sizeof(caption->font.path), false) ||
          !sha256_string(caption->font.sha256) ||
          !bounded_string(caption->font.family, sizeof(caption->font.family), false)))) {
        return validation(MUSI_PROJECT_ERROR_CAPTION_STYLE, 0, 0);
    }
    if (project->output.width < 16 || project->output.width > 16384 ||
        project->output.height < 16 || project->output.height > 16384 ||
        project->output.fps_denominator == 0 || project->output.fps_denominator > 1001 ||
        project->output.fps_numerator == 0 ||
        project->output.fps_numerator > UINT32_C(240)*project->output.fps_denominator ||
        !isfinite(project->output.start_seconds) || project->output.start_seconds < 0.0 ||
        !isfinite(project->output.end_seconds) ||
        project->output.end_seconds <= project->output.start_seconds ||
        project->output.end_seconds > project->audio.duration_seconds ||
        !enum_in_range((int) project->output.format, MUSI_OUTPUT_FORMAT_COUNT) ||
        !enum_in_range((int) project->output.quality, MUSI_OUTPUT_QUALITY_COUNT)) {
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

    if (project->lyrics.duration_seconds != project->audio.duration_seconds ||
        lyrics_document_validate(&project->lyrics).result != LYRICS_OK) {
        return validation(MUSI_PROJECT_ERROR_LYRICS, 0, 0);
    }
    if (project->scene_switches.count > SCENE_SWITCH_CAPACITY ||
        (project->scene_switches.enabled && project->scene_switches.count == 0)) {
        return validation(MUSI_PROJECT_ERROR_SCENE_SWITCH, 0, 0);
    }
    double switch_cursor = 0.0;
    for (i = 0; i < project->scene_switches.count; ++i) {
        const Musi_Scene_Switch_Suggestion *cue = &project->scene_switches.cues[i];
        if (cue->id == 0 || !stable_name(cue->scene_name, sizeof(cue->scene_name)) ||
            !isfinite(cue->start_seconds) || !isfinite(cue->end_seconds) ||
            cue->start_seconds < 0.0 || cue->end_seconds <= cue->start_seconds ||
            cue->end_seconds > project->audio.duration_seconds ||
            fabs(cue->start_seconds - switch_cursor) > 0.001 ||
            !isfinite(cue->strength) || cue->strength < 0.0f || cue->strength > 1.0f) {
            return validation(MUSI_PROJECT_ERROR_SCENE_SWITCH, i, 0);
        }
        if (cue->setting_count > SCENE_SETTINGS_MAX_CONTROLS) {
            return validation(MUSI_PROJECT_ERROR_SCENE_SWITCH, i, 0);
        }
        for (j = 0; j < cue->setting_count; ++j) {
            if (!isfinite(cue->settings[j])) {
                return validation(MUSI_PROJECT_ERROR_SCENE_SWITCH, i, j);
            }
        }
        for (j = 0; j < i; ++j) if (project->scene_switches.cues[j].id == cue->id) {
            return validation(MUSI_PROJECT_ERROR_DUPLICATE_ID, i, j);
        }
        switch_cursor = cue->end_seconds;
    }
    if (project->scene_switches.count > 0 &&
        fabs(switch_cursor - project->audio.duration_seconds) > 0.001) {
        return validation(MUSI_PROJECT_ERROR_SCENE_SWITCH,
                          project->scene_switches.count - 1, 0);
    }
    if (project->scene_preset_count > MUSI_PROJECT_MAX_SCENE_PRESETS) {
        return validation(MUSI_PROJECT_ERROR_SCENE_PRESET, 0, 0);
    }
    for (i = 0; i < project->scene_preset_count; ++i) {
        const Musi_Scene_Preset *preset = &project->scene_presets[i];
        if (preset->id == 0 ||
            !stable_name(preset->scene_name, sizeof(preset->scene_name)) ||
            !bounded_string(preset->name, sizeof(preset->name), false) ||
            preset->setting_count == 0 ||
            preset->setting_count > SCENE_SETTINGS_MAX_CONTROLS) {
            return validation(MUSI_PROJECT_ERROR_SCENE_PRESET, i, 0);
        }
        for (j = 0; j < preset->setting_count; ++j) {
            if (!isfinite(preset->settings[j])) {
                return validation(MUSI_PROJECT_ERROR_SCENE_PRESET, i, j);
            }
        }
        for (j = 0; j < i; ++j) {
            if (project->scene_presets[j].id == preset->id) {
                return validation(MUSI_PROJECT_ERROR_DUPLICATE_ID, i, j);
            }
        }
    }
    if (event_timeline_validate(&project->manual_events) != EVENT_TIMELINE_OK) {
        return validation(MUSI_PROJECT_ERROR_MANUAL_EVENT, 0, 0);
    }
    for (i = 0; i < project->manual_events.count; ++i) {
        if (project->manual_events.events[i].timestamp_seconds > project->audio.duration_seconds) {
            return validation(MUSI_PROJECT_ERROR_MANUAL_EVENT, i, 0);
        }
    }
    if (event_timeline_validate(&project->semantic_events) != EVENT_TIMELINE_OK) {
        return validation(MUSI_PROJECT_ERROR_SEMANTIC_EVENT, 0, 0);
    }
    for (i = 0; i < project->semantic_events.count; ++i) {
        const Event_Record *event = &project->semantic_events.events[i];
        if (event->timestamp_seconds > project->audio.duration_seconds ||
            event->type != EVENT_TYPE_SEMANTIC || event->value_count != 4 ||
            event->values[0] < 0.0f || event->values[0] > 1.0f ||
            event->values[1] < 0.0f || event->values[1] > 1.0f ||
            event->values[2] < -1.0f || event->values[2] > 1.0f ||
            event->values[3] < 0.0f || event->values[3] > 1.0f) {
            return validation(MUSI_PROJECT_ERROR_SEMANTIC_EVENT, i, 0);
        }
    }

    return validation(MUSI_PROJECT_VALID, 0, 0);
}

const char *musi_project_error_string(Musi_Project_Error error)
{
    static const char *const names[] = {
        "valid", "null project", "unsupported schema version", "invalid metadata",
        "invalid audio asset", "invalid ASCII image asset",
        "invalid caption style", "invalid output settings", "capacity/count violation",
        "invalid scene", "invalid parameter mapping", "invalid cue", "cues are unsorted",
        "cues overlap", "invalid analysis lane", "duplicate stable id",
        "invalid lyrics", "invalid scene-switch suggestions", "invalid scene preset", "invalid manual event",
        "invalid semantic event"
    };
    if (!enum_in_range((int) error, (int) (sizeof(names) / sizeof(names[0])))) {
        return "unknown project error";
    }
    return names[error];
}

Musi_Project_Editor_Support musi_project_editor_support(
    const Musi_Project *project)
{
    if (project == NULL) return MUSI_PROJECT_EDITOR_ERROR_NULL;
    if (project->audio.mode != MUSI_ASSET_REFERENCED &&
        project->audio.mode != MUSI_ASSET_IMPORTED) {
        return MUSI_PROJECT_EDITOR_ERROR_AUDIO_MODE;
    }
    // An imported caption face names a file in the sibling asset bundle that
    // this build cannot publish or verify yet. Opening such a project would
    // typeset in the fallback face and then autosave that substitution over the
    // author's choice, which is exactly the silent normalization the editor
    // support check exists to prevent.
    if (project->caption_style.face == MUSI_CAPTION_FACE_IMPORTED ||
        project->caption_style.font.present) {
        return MUSI_PROJECT_EDITOR_ERROR_CAPTION_FONT;
    }
    if (fabs(project->output.start_seconds) > 0.000001 ||
        fabs(project->output.end_seconds - project->audio.duration_seconds) >
            0.000001) {
        return MUSI_PROJECT_EDITOR_ERROR_OUTPUT_RANGE;
    }
    if (project->output.format != MUSI_OUTPUT_MP4_H264 ||
        project->output.fps_denominator != 1) {
        return MUSI_PROJECT_EDITOR_ERROR_OUTPUT_FORMAT;
    }
    if (project->scene_count != 1) return MUSI_PROJECT_EDITOR_ERROR_SCENE_COUNT;
    if (project->cue_count != 0) return MUSI_PROJECT_EDITOR_ERROR_PARAMETER_CUES;
    const Musi_Scene_Entry *scene = &project->scenes[0];
    if (!scene->enabled || fabs(scene->start_seconds) > 0.000001 ||
        fabs(scene->end_seconds - project->audio.duration_seconds) > 0.000001 ||
        fabs(scene->opacity - 1.0) > 0.000001 ||
        scene->blend_mode != MUSI_BLEND_NORMAL) {
        return MUSI_PROJECT_EDITOR_ERROR_SCENE_LAYOUT;
    }
    if (scene->mapping_count != 0 &&
        !scene_routes_mappings_supported(scene->mappings,
                                         scene->mapping_count)) {
        return MUSI_PROJECT_EDITOR_ERROR_SCENE_MAPPINGS;
    }
    return MUSI_PROJECT_EDITOR_SUPPORTED;
}

const char *musi_project_editor_support_string(Musi_Project_Editor_Support support)
{
    static const char *const names[] = {
        "supported",
        "null project",
        "the audio asset mode is not supported by this editor",
        "partial render ranges are not supported by this editor yet",
        "only integer-frame-rate H.264 MP4 output is supported by this editor",
        "only one scene is supported by this editor yet",
        "parameter automation cues are not supported by this editor yet",
        "the scene must cover the full track, be enabled, opaque, and Normal blend",
        "only built-in scene settings are supported, persisted as slider constants or audio-driven routes",
        "an imported caption face is not supported by this editor yet",
    };
    return (unsigned)support < sizeof(names)/sizeof(names[0]) ?
           names[support] : "unknown editor compatibility result";
}

bool musi_project_audio_metadata_matches(const Musi_Project *project,
                                         double decoded_duration_seconds,
                                         uint32_t decoded_sample_rate,
                                         uint16_t decoded_channels,
                                         double duration_tolerance_seconds)
{
    return project != NULL && isfinite(decoded_duration_seconds) &&
           decoded_duration_seconds > 0.0 &&
           isfinite(duration_tolerance_seconds) &&
           duration_tolerance_seconds >= 0.0 &&
           fabs(decoded_duration_seconds - project->audio.duration_seconds) <=
               duration_tolerance_seconds &&
           decoded_sample_rate == project->audio.sample_rate &&
           decoded_channels == project->audio.channels;
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
MUSI_NAME_FUNCTION(musi_output_quality_name, Musi_Output_Quality, MUSI_OUTPUT_QUALITY_COUNT,
                   "balanced", "high", "master")
MUSI_NAME_FUNCTION(musi_blend_mode_name, Musi_Blend_Mode, MUSI_BLEND_MODE_COUNT,
                   "normal", "add", "multiply", "screen")
MUSI_NAME_FUNCTION(musi_analysis_source_name, Musi_Analysis_Source, MUSI_ANALYSIS_SOURCE_COUNT,
                   "rms", "peak", "spectral_flux", "beat_phase", "band")
MUSI_NAME_FUNCTION(musi_interpolation_name, Musi_Interpolation, MUSI_INTERPOLATION_COUNT,
                   "step", "linear", "smoothstep", "ease_in", "ease_out")
MUSI_NAME_FUNCTION(musi_analysis_lane_kind_name, Musi_Analysis_Lane_Kind, MUSI_LANE_KIND_COUNT,
                   "measured_signal", "lyric_timing", "semantic_score")
MUSI_NAME_FUNCTION(musi_caption_face_name, Musi_Caption_Face, MUSI_CAPTION_FACE_COUNT,
                   "alegreya", "space_grotesk", "imported")
MUSI_NAME_FUNCTION(musi_caption_box_name, Musi_Caption_Box, MUSI_CAPTION_BOX_COUNT,
                   "none", "shadow", "plate")
MUSI_NAME_FUNCTION(musi_caption_anchor_name, Musi_Caption_Anchor, MUSI_CAPTION_ANCHOR_COUNT,
                   "bottom_left", "bottom_center", "bottom_right",
                   "middle_left", "middle_center", "middle_right",
                   "top_left", "top_center", "top_right")

#undef MUSI_NAME_FUNCTION
