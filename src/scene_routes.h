#ifndef MUSIALIZER_SCENE_ROUTES_H_
#define MUSIALIZER_SCENE_ROUTES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scene_settings.h"

// Dynamic audio-to-parameter routes: the runtime model of the non-constant
// Musi_Parameter_Mapping capability the .musi v1 format has always specified.
// A route replaces its target setting's value each frame with the mapped
// audio source (constants are the degenerate case the sliders already use),
// clamped to the setting descriptor's range. Routes are user-authored
// configuration, never analysis output.
enum { SCENE_ROUTES_PER_SCENE = SCENE_SETTINGS_MAX_CONTROLS };

typedef struct Scene_Routes {
    size_t count;
    Musi_Parameter_Mapping items[SCENE_ROUTES_PER_SCENE];
} Scene_Routes;

typedef struct Scene_Route_Table {
    Scene_Routes scenes[SCENE_SETTINGS_SCENE_COUNT];
} Scene_Route_Table;

// Mirror of the per-frame Scene_Audio_Frame source values, kept free of
// scene.h so this module stays headless.
typedef struct Scene_Route_Sources {
    const float *bands;
    size_t bands_count;
    float rms;
    float peak;
    float spectral_flux;
    float beat_phase;
} Scene_Route_Sources;

void scene_route_table_init(Scene_Route_Table *table);
bool scene_route_table_valid(const Scene_Route_Table *table);

// True when the mapping is well-formed for this scene: the parameter key
// resolves to one of the scene's settings, the source and interpolation
// enums are in range, band_index obeys the schema rule (zero unless the
// source is band, bounded by the analyzer band capacity), and every range
// is finite with input_max > input_min.
bool scene_route_valid(size_t scene_index, const Musi_Parameter_Mapping *route);

// Adds a valid route, rejecting duplicates of the same parameter within the
// scene and additions beyond capacity.
bool scene_route_table_add(Scene_Route_Table *table, size_t scene_index,
                           const Musi_Parameter_Mapping *route);
bool scene_route_table_remove(Scene_Route_Table *table, size_t scene_index,
                              size_t route_index);

// Binds a mapping source to its current frame value. False when the value
// is unavailable (missing or out-of-range band) or not finite; callers skip
// the route for this frame rather than propagating a bad value.
bool scene_routes_source_value(const Scene_Route_Sources *sources,
                               Musi_Analysis_Source source,
                               uint16_t band_index, double *value);

// Parses a route spec of the shape
//   parameter:source:band:in_min:in_max:out_min:out_max[:curve][:clamp|noclamp]
// e.g. "loom.weight:band:2:0:1:0.4:2.2:smoothstep". The "settings." key
// prefix may be omitted; source and curve names are the .musi codec's
// canonical names; curve defaults to linear and clamping is on unless
// "noclamp" is given. The parsed route is validated against the scene the
// parameter belongs to, returned via scene_index.
bool scene_route_parse_spec(const char *spec, size_t *scene_index,
                            Musi_Parameter_Mapping *route);

// True when every mapping is either a canonical constant slider mapping or
// a valid dynamic route for its parameter's scene, with parameter names
// unique across the list. This is the widened .musi editor-support rule: a
// parameter is persisted as exactly one of the two.
bool scene_routes_mappings_supported(const Musi_Parameter_Mapping *mappings,
                                     size_t count);

// Serializes editor state as a project scene's mappings: every setting as
// its canonical constant, except settings driven by a route, which persist
// the route in that position instead. Deterministic order for byte-stable
// saves.
bool scene_routes_export_mappings(const Scene_Settings *settings,
                                  const Scene_Route_Table *table,
                                  Musi_Parameter_Mapping *mappings,
                                  size_t capacity, size_t *count);

// Partitions project mappings into slider constants and dynamic routes.
// All-or-nothing: a mapping that is neither a supported constant nor a
// valid route fails the import; nothing is silently dropped. Settings for
// routed parameters keep their scene defaults.
bool scene_routes_import_mappings(Scene_Settings *settings,
                                  Scene_Route_Table *table,
                                  const Musi_Parameter_Mapping *mappings,
                                  size_t count);

// Copies base into effective, then applies this scene's routes on top.
// A route whose source or evaluation fails this frame leaves the base value
// untouched; successful routes are clamped to the descriptor range. The
// result is deterministic for identical inputs.
bool scene_routes_apply(const Scene_Route_Table *table, size_t scene_index,
                        const Scene_Route_Sources *sources,
                        const Scene_Settings *base, Scene_Settings *effective);

#endif // MUSIALIZER_SCENE_ROUTES_H_
