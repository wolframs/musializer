#include "scene_routes.h"

#include <math.h>
#include <string.h>

#include "audio_analyzer.h"

void scene_route_table_init(Scene_Route_Table *table)
{
    if (table == NULL) return;
    memset(table, 0, sizeof(*table));
}

bool scene_route_table_valid(const Scene_Route_Table *table)
{
    if (table == NULL) return false;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        const Scene_Routes *routes = &table->scenes[scene];
        if (routes->count > SCENE_ROUTES_PER_SCENE) return false;
        for (size_t index = 0; index < routes->count; ++index) {
            if (!scene_route_valid(scene, &routes->items[index])) return false;
            for (size_t previous = 0; previous < index; ++previous) {
                if (strcmp(routes->items[previous].parameter,
                           routes->items[index].parameter) == 0) return false;
            }
        }
    }
    return true;
}

bool scene_route_valid(size_t scene_index, const Musi_Parameter_Mapping *route)
{
    if (route == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) {
        return false;
    }
    size_t owner_scene = 0;
    size_t setting_index = 0;
    if (scene_settings_descriptor_by_key(route->parameter, &owner_scene,
                                         &setting_index) == NULL ||
        owner_scene != scene_index) return false;
    if ((int)route->source < 0 ||
        (int)route->source >= MUSI_ANALYSIS_SOURCE_COUNT) return false;
    if (route->source == MUSI_ANALYSIS_BAND) {
        if (route->band_index >= AUDIO_ANALYZER_MAX_BANDS) return false;
    } else if (route->band_index != 0) {
        return false;
    }
    if ((int)route->interpolation < 0 ||
        (int)route->interpolation >= MUSI_INTERPOLATION_COUNT) return false;
    if (!isfinite(route->input_min) || !isfinite(route->input_max) ||
        !(route->input_max > route->input_min)) return false;
    if (!isfinite(route->output_min) || !isfinite(route->output_max)) {
        return false;
    }
    return true;
}

bool scene_route_table_add(Scene_Route_Table *table, size_t scene_index,
                           const Musi_Parameter_Mapping *route)
{
    if (table == NULL || !scene_route_valid(scene_index, route)) return false;
    Scene_Routes *routes = &table->scenes[scene_index];
    if (routes->count >= SCENE_ROUTES_PER_SCENE) return false;
    for (size_t index = 0; index < routes->count; ++index) {
        if (strcmp(routes->items[index].parameter, route->parameter) == 0) {
            return false;
        }
    }
    routes->items[routes->count] = *route;
    routes->count += 1;
    return true;
}

bool scene_route_table_remove(Scene_Route_Table *table, size_t scene_index,
                              size_t route_index)
{
    if (table == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) {
        return false;
    }
    Scene_Routes *routes = &table->scenes[scene_index];
    if (route_index >= routes->count) return false;
    for (size_t index = route_index; index + 1 < routes->count; ++index) {
        routes->items[index] = routes->items[index + 1];
    }
    routes->count -= 1;
    memset(&routes->items[routes->count], 0, sizeof(routes->items[0]));
    return true;
}

bool scene_routes_source_value(const Scene_Route_Sources *sources,
                               Musi_Analysis_Source source,
                               uint16_t band_index, double *value)
{
    if (sources == NULL || value == NULL) return false;
    float sample = 0.0f;
    switch (source) {
    case MUSI_ANALYSIS_RMS: sample = sources->rms; break;
    case MUSI_ANALYSIS_PEAK: sample = sources->peak; break;
    case MUSI_ANALYSIS_SPECTRAL_FLUX: sample = sources->spectral_flux; break;
    case MUSI_ANALYSIS_BEAT_PHASE: sample = sources->beat_phase; break;
    case MUSI_ANALYSIS_BAND:
        if (sources->bands == NULL ||
            (size_t)band_index >= sources->bands_count) return false;
        sample = sources->bands[band_index];
        break;
    default: return false;
    }
    if (!isfinite(sample)) return false;
    *value = (double)sample;
    return true;
}

bool scene_routes_apply(const Scene_Route_Table *table, size_t scene_index,
                        const Scene_Route_Sources *sources,
                        const Scene_Settings *base, Scene_Settings *effective)
{
    if (table == NULL || sources == NULL || effective == NULL ||
        scene_index >= SCENE_SETTINGS_SCENE_COUNT ||
        !scene_settings_valid(base)) return false;
    if (effective != base) *effective = *base;

    const Scene_Routes *routes = &table->scenes[scene_index];
    if (routes->count > SCENE_ROUTES_PER_SCENE) return false;
    for (size_t index = 0; index < routes->count; ++index) {
        const Musi_Parameter_Mapping *route = &routes->items[index];
        size_t owner_scene = 0;
        size_t setting_index = 0;
        const Scene_Setting_Descriptor *descriptor =
            scene_settings_descriptor_by_key(route->parameter, &owner_scene,
                                             &setting_index);
        if (descriptor == NULL || owner_scene != scene_index) continue;
        double source_value = 0.0;
        double mapped = 0.0;
        if (!scene_routes_source_value(sources, route->source,
                                       route->band_index, &source_value) ||
            !musi_mapping_evaluate(route, source_value, &mapped)) continue;
        if (mapped < (double)descriptor->minimum) {
            mapped = (double)descriptor->minimum;
        }
        if (mapped > (double)descriptor->maximum) {
            mapped = (double)descriptor->maximum;
        }
        (void)scene_settings_set(effective, scene_index, setting_index,
                                 (float)mapped);
    }
    return true;
}
