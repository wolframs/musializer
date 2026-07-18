#include "scene_routes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    // A flat full-range RMS mapping is byte-for-byte indistinguishable from
    // the v1 slider representation. More generally, equal endpoints are not
    // audio-reactive at all. Keep those values in Scene_Settings so a route
    // can never silently reopen as a slider and lose its authoring identity.
    if (route->output_min == route->output_max) return false;
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

enum { SCENE_ROUTE_SPEC_CAPACITY = 256, SCENE_ROUTE_SPEC_MAX_FIELDS = 9 };

static bool scene_route_parse_double(const char *text, double *value)
{
    if (text == NULL || text[0] == '\0' || value == NULL) return false;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (end == text || *end != '\0' || !isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

bool scene_route_parse_spec(const char *spec, size_t *scene_index,
                            Musi_Parameter_Mapping *route)
{
    if (spec == NULL || scene_index == NULL || route == NULL ||
        strlen(spec) >= SCENE_ROUTE_SPEC_CAPACITY) return false;
    char buffer[SCENE_ROUTE_SPEC_CAPACITY];
    memcpy(buffer, spec, strlen(spec) + 1U);

    char *fields[SCENE_ROUTE_SPEC_MAX_FIELDS] = {0};
    size_t field_count = 0;
    char *cursor = buffer;
    while (field_count < SCENE_ROUTE_SPEC_MAX_FIELDS) {
        fields[field_count++] = cursor;
        char *separator = strchr(cursor, ':');
        if (separator == NULL) break;
        *separator = '\0';
        cursor = separator + 1;
    }
    if (field_count < 7 || strchr(cursor, ':') != NULL) return false;

    memset(route, 0, sizeof(*route));
    // The persisted keys all carry the "settings." prefix; accept the short
    // form people actually type.
    const char *key = fields[0];
    int written = strncmp(key, "settings.", 9) == 0 ?
        snprintf(route->parameter, sizeof(route->parameter), "%s", key) :
        snprintf(route->parameter, sizeof(route->parameter), "settings.%s", key);
    if (written < 0 || (size_t)written >= sizeof(route->parameter)) {
        return false;
    }

    bool source_found = false;
    for (int value = 0; value < MUSI_ANALYSIS_SOURCE_COUNT; ++value) {
        if (strcmp(fields[1], musi_analysis_source_name(
                       (Musi_Analysis_Source)value)) == 0) {
            route->source = (Musi_Analysis_Source)value;
            source_found = true;
            break;
        }
    }
    if (!source_found) return false;

    char *end = NULL;
    unsigned long band = strtoul(fields[2], &end, 10);
    if (end == fields[2] || *end != '\0' || band > 0xFFFFu) return false;
    route->band_index = (uint16_t)band;

    if (!scene_route_parse_double(fields[3], &route->input_min) ||
        !scene_route_parse_double(fields[4], &route->input_max) ||
        !scene_route_parse_double(fields[5], &route->output_min) ||
        !scene_route_parse_double(fields[6], &route->output_max)) return false;

    route->interpolation = MUSI_INTERPOLATION_LINEAR;
    route->clamp = true;
    for (size_t extra = 7; extra < field_count; ++extra) {
        if (strcmp(fields[extra], "clamp") == 0) {
            route->clamp = true;
            continue;
        }
        if (strcmp(fields[extra], "noclamp") == 0) {
            route->clamp = false;
            continue;
        }
        bool curve_found = false;
        for (int value = 0; value < MUSI_INTERPOLATION_COUNT; ++value) {
            if (strcmp(fields[extra], musi_interpolation_name(
                           (Musi_Interpolation)value)) == 0) {
                route->interpolation = (Musi_Interpolation)value;
                curve_found = true;
                break;
            }
        }
        if (!curve_found) return false;
    }

    size_t owner_scene = 0;
    if (scene_settings_descriptor_by_key(route->parameter, &owner_scene,
                                         NULL) == NULL) return false;
    *scene_index = owner_scene;
    return scene_route_valid(owner_scene, route);
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

static bool scene_route_mapping_supported_any(
    const Musi_Parameter_Mapping *mapping)
{
    if (scene_settings_mapping_supported(mapping)) return true;
    size_t scene_index = 0;
    if (scene_settings_descriptor_by_key(mapping->parameter, &scene_index,
                                         NULL) == NULL) return false;
    return scene_route_valid(scene_index, mapping);
}

bool scene_routes_mappings_supported(const Musi_Parameter_Mapping *mappings,
                                     size_t count)
{
    if (count > MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE ||
        (count > 0 && mappings == NULL)) return false;
    for (size_t index = 0; index < count; ++index) {
        if (!scene_route_mapping_supported_any(&mappings[index])) return false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (strcmp(mappings[previous].parameter,
                       mappings[index].parameter) == 0) return false;
        }
    }
    return true;
}

bool scene_routes_export_mappings(const Scene_Settings *settings,
                                  const Scene_Route_Table *table,
                                  Musi_Parameter_Mapping *mappings,
                                  size_t capacity, size_t *count)
{
    if (!scene_settings_export_mappings(settings, mappings, capacity, count)) {
        return false;
    }
    if (table == NULL) return true;
    if (!scene_route_table_valid(table)) return false;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        const Scene_Routes *routes = &table->scenes[scene];
        for (size_t index = 0; index < routes->count; ++index) {
            const Musi_Parameter_Mapping *route = &routes->items[index];
            bool replaced = false;
            for (size_t at = 0; at < *count; ++at) {
                if (strcmp(mappings[at].parameter, route->parameter) != 0) {
                    continue;
                }
                mappings[at] = *route;
                replaced = true;
                break;
            }
            // Every valid route targets a real descriptor, and the constant
            // export lists every descriptor, so a missing slot means the
            // table and settings tables disagree - refuse to save that.
            if (!replaced) return false;
        }
    }
    return true;
}

bool scene_routes_import_mappings(Scene_Settings *settings,
                                  Scene_Route_Table *table,
                                  const Musi_Parameter_Mapping *mappings,
                                  size_t count)
{
    if (settings == NULL || table == NULL ||
        !scene_routes_mappings_supported(mappings, count)) return false;
    Scene_Settings staged;
    scene_settings_init(&staged);
    Scene_Route_Table staged_routes;
    scene_route_table_init(&staged_routes);
    for (size_t index = 0; index < count; ++index) {
        const Musi_Parameter_Mapping *mapping = &mappings[index];
        size_t scene_index = 0;
        size_t setting_index = 0;
        if (scene_settings_descriptor_by_key(mapping->parameter, &scene_index,
                                             &setting_index) == NULL) {
            return false;
        }
        if (scene_settings_mapping_supported(mapping)) {
            if (!scene_settings_set(&staged, scene_index, setting_index,
                                    (float)mapping->output_min)) return false;
        } else if (!scene_route_table_add(&staged_routes, scene_index,
                                          mapping)) {
            return false;
        }
    }
    *settings = staged;
    *table = staged_routes;
    return true;
}

bool scene_routes_apply(const Scene_Route_Table *table, size_t scene_index,
                        const Scene_Route_Sources *sources,
                        const Scene_Settings *base, Scene_Settings *effective)
{
    if (table == NULL || sources == NULL || effective == NULL ||
        scene_index >= SCENE_SETTINGS_SCENE_COUNT ||
        !scene_settings_valid(base)) return false;
    Scene_Settings staged = *base;

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
        // Toggle descriptors accept only their two canonical values. Routes
        // are continuous, so cross the binary boundary at the descriptor's
        // midpoint instead of silently rejecting almost every mapped frame.
        if (descriptor->kind == SCENE_SETTING_TOGGLE) {
            double midpoint = ((double)descriptor->minimum +
                               (double)descriptor->maximum)*0.5;
            mapped = mapped >= midpoint ? (double)descriptor->maximum :
                                          (double)descriptor->minimum;
        }
        if (!scene_settings_set(&staged, scene_index, setting_index,
                                (float)mapped)) return false;
    }
    *effective = staged;
    return true;
}
