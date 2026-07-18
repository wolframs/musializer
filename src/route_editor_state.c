#include "route_editor_state.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio_analyzer.h"

static const Scene_Setting_Descriptor *route_editor_descriptor(
    const Route_Editor_State *state)
{
    // Looked up on demand rather than cached: descriptor tables are static
    // data inside the hot-reloaded module, so a stored pointer would dangle
    // across a reload while the indices stay stable.
    if (state == NULL || !state->open) return NULL;
    return scene_settings_descriptor(state->scene_index, state->setting_index);
}

static bool route_editor_mapping_equal(const Musi_Parameter_Mapping *a,
                                       const Musi_Parameter_Mapping *b)
{
    return strcmp(a->parameter, b->parameter) == 0 &&
           a->source == b->source &&
           a->band_index == b->band_index &&
           a->input_min == b->input_min &&
           a->input_max == b->input_max &&
           a->output_min == b->output_min &&
           a->output_max == b->output_max &&
           a->interpolation == b->interpolation &&
           a->clamp == b->clamp;
}

static double route_editor_snap_output(const Scene_Setting_Descriptor *descriptor,
                                       double value)
{
    if (value < (double)descriptor->minimum) value = (double)descriptor->minimum;
    if (value > (double)descriptor->maximum) value = (double)descriptor->maximum;
    if (descriptor->precision == 0) value = round(value);
    return value;
}

void route_editor_init(Route_Editor_State *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
}

bool route_editor_open(Route_Editor_State *state, size_t track_slot,
                       size_t scene_index, size_t setting_index,
                       const Musi_Parameter_Mapping *existing)
{
    if (state == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) {
        return false;
    }
    const Scene_Setting_Descriptor *descriptor =
        scene_settings_descriptor(scene_index, setting_index);
    if (descriptor == NULL) return false;
    if (existing != NULL &&
        (!scene_route_valid(scene_index, existing) ||
         strcmp(existing->parameter, descriptor->key) != 0)) return false;

    route_editor_init(state);
    state->open = true;
    state->track_slot = track_slot;
    state->scene_index = scene_index;
    state->setting_index = setting_index;
    if (existing != NULL) {
        state->has_committed = true;
        state->committed = *existing;
        state->draft = *existing;
        return true;
    }
    Musi_Parameter_Mapping *draft = &state->draft;
    int written = snprintf(draft->parameter, sizeof(draft->parameter), "%s",
                           descriptor->key);
    if (written < 0 || (size_t)written >= sizeof(draft->parameter)) {
        route_editor_init(state);
        return false;
    }
    draft->source = MUSI_ANALYSIS_RMS;
    draft->band_index = 0;
    draft->input_min = 0.0;
    draft->input_max = 1.0;
    draft->output_min = (double)descriptor->minimum;
    draft->output_max = (double)descriptor->maximum;
    draft->interpolation = MUSI_INTERPOLATION_LINEAR;
    draft->clamp = true;
    return true;
}

void route_editor_close(Route_Editor_State *state)
{
    route_editor_init(state);
}

bool route_editor_is_open(const Route_Editor_State *state)
{
    return state != NULL && state->open;
}

bool route_editor_targets(const Route_Editor_State *state, size_t track_slot,
                          size_t scene_index, size_t setting_index)
{
    return route_editor_matches_context(state, track_slot, scene_index) &&
           state->setting_index == setting_index;
}

bool route_editor_matches_context(const Route_Editor_State *state,
                                  size_t track_slot, size_t scene_index)
{
    return state != NULL && state->open &&
           state->track_slot == track_slot &&
           state->scene_index == scene_index;
}

bool route_editor_set_source(Route_Editor_State *state,
                             Musi_Analysis_Source source)
{
    if (state == NULL || !state->open || (int)source < 0 ||
        (int)source >= MUSI_ANALYSIS_SOURCE_COUNT) return false;
    if (state->draft.source == source) return true;
    state->draft.source = source;
    if (source != MUSI_ANALYSIS_BAND) state->draft.band_index = 0;
    state->touched = true;
    return true;
}

bool route_editor_step_band(Route_Editor_State *state, int delta,
                            size_t band_limit)
{
    if (state == NULL || !state->open ||
        state->draft.source != MUSI_ANALYSIS_BAND || band_limit == 0) {
        return false;
    }
    if (band_limit > AUDIO_ANALYZER_MAX_BANDS) {
        band_limit = AUDIO_ANALYZER_MAX_BANDS;
    }
    long band = (long)state->draft.band_index + (long)delta;
    if (band < 0) band = 0;
    if (band > (long)(band_limit - 1U)) band = (long)(band_limit - 1U);
    if ((uint16_t)band == state->draft.band_index) return true;
    state->draft.band_index = (uint16_t)band;
    state->touched = true;
    return true;
}

bool route_editor_set_input_min(Route_Editor_State *state, double value)
{
    if (state == NULL || !state->open || !isfinite(value)) return false;
    if (value < 0.0) value = 0.0;
    double ceiling = state->draft.input_max - ROUTE_EDITOR_INPUT_GAP;
    if (value > ceiling) value = ceiling;
    if (value == state->draft.input_min) return true;
    state->draft.input_min = value;
    state->touched = true;
    return true;
}

bool route_editor_set_input_max(Route_Editor_State *state, double value)
{
    if (state == NULL || !state->open || !isfinite(value)) return false;
    if (value > 1.0) value = 1.0;
    double floor_value = state->draft.input_min + ROUTE_EDITOR_INPUT_GAP;
    if (value < floor_value) value = floor_value;
    if (value == state->draft.input_max) return true;
    state->draft.input_max = value;
    state->touched = true;
    return true;
}

bool route_editor_set_output_low(Route_Editor_State *state, double value)
{
    const Scene_Setting_Descriptor *descriptor = route_editor_descriptor(state);
    if (descriptor == NULL || !isfinite(value)) return false;
    value = route_editor_snap_output(descriptor, value);
    if (value == state->draft.output_min) return true;
    state->draft.output_min = value;
    state->touched = true;
    return true;
}

bool route_editor_set_output_high(Route_Editor_State *state, double value)
{
    const Scene_Setting_Descriptor *descriptor = route_editor_descriptor(state);
    if (descriptor == NULL || !isfinite(value)) return false;
    value = route_editor_snap_output(descriptor, value);
    if (value == state->draft.output_max) return true;
    state->draft.output_max = value;
    state->touched = true;
    return true;
}

bool route_editor_swap_output(Route_Editor_State *state)
{
    if (state == NULL || !state->open) return false;
    if (state->draft.output_min == state->draft.output_max) return true;
    double low = state->draft.output_min;
    state->draft.output_min = state->draft.output_max;
    state->draft.output_max = low;
    state->touched = true;
    return true;
}

bool route_editor_set_curve(Route_Editor_State *state,
                            Musi_Interpolation curve)
{
    if (state == NULL || !state->open || (int)curve < 0 ||
        (int)curve >= MUSI_INTERPOLATION_COUNT) return false;
    if (state->draft.interpolation == curve) return true;
    state->draft.interpolation = curve;
    state->touched = true;
    return true;
}

bool route_editor_set_clamp(Route_Editor_State *state, bool clamp)
{
    if (state == NULL || !state->open) return false;
    if (state->draft.clamp == clamp) return true;
    state->draft.clamp = clamp;
    state->touched = true;
    return true;
}

bool route_editor_dirty(const Route_Editor_State *state)
{
    if (state == NULL || !state->open) return false;
    if (state->has_committed) {
        return !route_editor_mapping_equal(&state->draft, &state->committed);
    }
    return state->touched;
}

bool route_editor_dirty_for_track(const Route_Editor_State *state,
                                  size_t track_slot)
{
    return route_editor_dirty(state) && state->track_slot == track_slot;
}

bool route_editor_can_apply(const Route_Editor_State *state)
{
    return state != NULL && state->open &&
           scene_route_valid(state->scene_index, &state->draft);
}

static bool route_editor_table_find(const Scene_Route_Table *table,
                                    size_t scene_index, const char *parameter,
                                    size_t *route_index)
{
    if (table == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) {
        return false;
    }
    const Scene_Routes *routes = &table->scenes[scene_index];
    for (size_t index = 0; index < routes->count; ++index) {
        if (strcmp(routes->items[index].parameter, parameter) == 0) {
            if (route_index != NULL) *route_index = index;
            return true;
        }
    }
    return false;
}

bool route_editor_apply(Route_Editor_State *state, Scene_Route_Table *table)
{
    if (table == NULL || !route_editor_can_apply(state)) return false;
    size_t existing_index = 0;
    if (route_editor_table_find(table, state->scene_index,
                                state->draft.parameter, &existing_index)) {
        // Freeing the parameter's own slot first means the re-add below can
        // only fail on programmer error, never capacity.
        if (!scene_route_table_remove(table, state->scene_index,
                                      existing_index)) return false;
    }
    if (!scene_route_table_add(table, state->scene_index, &state->draft)) {
        return false;
    }
    state->committed = state->draft;
    state->has_committed = true;
    state->touched = false;
    return true;
}

bool route_editor_remove(Route_Editor_State *state, Scene_Route_Table *table)
{
    if (state == NULL || !state->open || !state->has_committed ||
        table == NULL) return false;
    size_t route_index = 0;
    if (!route_editor_table_find(table, state->scene_index,
                                 state->committed.parameter, &route_index) ||
        !scene_route_table_remove(table, state->scene_index, route_index)) {
        return false;
    }
    route_editor_close(state);
    return true;
}

const Musi_Parameter_Mapping *route_editor_find_route(
    const Scene_Route_Table *table, size_t scene_index, size_t setting_index)
{
    if (table == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) {
        return NULL;
    }
    const Scene_Setting_Descriptor *descriptor =
        scene_settings_descriptor(scene_index, setting_index);
    if (descriptor == NULL) return NULL;
    size_t route_index = 0;
    if (!route_editor_table_find(table, scene_index, descriptor->key,
                                 &route_index)) return NULL;
    return &table->scenes[scene_index].items[route_index];
}

const char *route_editor_source_label(Musi_Analysis_Source source)
{
    switch (source) {
    case MUSI_ANALYSIS_RMS: return "RMS";
    case MUSI_ANALYSIS_PEAK: return "Peak";
    case MUSI_ANALYSIS_SPECTRAL_FLUX: return "Flux";
    case MUSI_ANALYSIS_BEAT_PHASE: return "Beat";
    case MUSI_ANALYSIS_BAND: return "Band";
    default: return "?";
    }
}

const char *route_editor_anchor_label(Musi_Analysis_Source source, bool high)
{
    switch (source) {
    case MUSI_ANALYSIS_RMS:
    case MUSI_ANALYSIS_PEAK:
    case MUSI_ANALYSIS_BAND: return high ? "Loud" : "Quiet";
    case MUSI_ANALYSIS_SPECTRAL_FLUX: return high ? "Busy" : "Calm";
    case MUSI_ANALYSIS_BEAT_PHASE: return high ? "Beat end" : "Beat start";
    default: return high ? "High" : "Low";
    }
}

const char *route_editor_curve_label(Musi_Interpolation curve)
{
    switch (curve) {
    case MUSI_INTERPOLATION_STEP: return "Step";
    case MUSI_INTERPOLATION_LINEAR: return "Linear";
    case MUSI_INTERPOLATION_SMOOTHSTEP: return "Smooth";
    case MUSI_INTERPOLATION_EASE_IN: return "Ease in";
    case MUSI_INTERPOLATION_EASE_OUT: return "Ease out";
    default: return "?";
    }
}

void route_editor_summary(const Musi_Parameter_Mapping *route,
                          unsigned precision, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) return;
    buffer[0] = '\0';
    if (route == NULL) return;
    char source[16];
    if (route->source == MUSI_ANALYSIS_BAND) {
        snprintf(source, sizeof(source), "Band %u",
                 (unsigned)route->band_index);
    } else {
        snprintf(source, sizeof(source), "%s",
                 route_editor_source_label(route->source));
    }
    snprintf(buffer, capacity, "%s \xC2\xB7 %s \xC2\xB7 %.*f \xE2\x86\x92 %.*f%s",
             source, route_editor_curve_label(route->interpolation),
             (int)precision, route->output_min,
             (int)precision, route->output_max,
             route->clamp ? "" : " \xC2\xB7 unclamped");
}

float route_editor_meter_position(const Musi_Parameter_Mapping *route,
                                  double source_value)
{
    if (route == NULL || !isfinite(source_value) ||
        !isfinite(route->input_min) || !isfinite(route->input_max) ||
        !(route->input_max > route->input_min)) return 0.0f;
    double normalized = (source_value - route->input_min)/
                        (route->input_max - route->input_min);
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    return (float)normalized;
}
