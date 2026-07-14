#ifndef MUSIALIZER_SCENE_SETTINGS_H_
#define MUSIALIZER_SCENE_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>

#include "project.h"
#include "scene_settings_values.h"

enum {
    SCENE_SETTINGS_PRESET_NAME_CAPACITY = 129,
};

typedef enum Scene_Setting_Kind {
    SCENE_SETTING_SLIDER,
    SCENE_SETTING_TOGGLE,
} Scene_Setting_Kind;

enum { SPECTRUM_SETTING_AMPLITUDE, SPECTRUM_SETTING_TRAIL,
       SPECTRUM_SETTING_SATURATION, SPECTRUM_SETTING_GLOW_SOFTNESS,
       SPECTRUM_SETTING_HUE_SWING, SPECTRUM_SETTING_CORE_GLOW,
       SPECTRUM_SETTING_BAR_TAPER, SPECTRUM_SETTING_REFLECTION };
enum { PULSE_SETTING_SCALE, PULSE_SETTING_RINGS, PULSE_SETTING_MOTION,
       PULSE_SETTING_ARC, PULSE_SETTING_WEIGHT, PULSE_SETTING_PETALS,
       PULSE_SETTING_HUE, PULSE_SETTING_GLOW };
enum { ORBITAL_SETTING_MOTION, ORBITAL_SETTING_RADIUS, ORBITAL_SETTING_DEPTH,
       ORBITAL_SETTING_NODES, ORBITAL_SETTING_LINKS, ORBITAL_SETTING_TILT,
       ORBITAL_SETTING_HUE };
enum { ASCII_SETTING_MOTION, ASCII_SETTING_CYCLING, ASCII_SETTING_SCANLINES,
       ASCII_SETTING_SPLIT, ASCII_SETTING_GAIN, ASCII_SETTING_TINT };
enum { ATLAS_SETTING_HEIGHT, ATLAS_SETTING_WIDTH, ATLAS_SETTING_DEPTH,
       ATLAS_SETTING_CAMERA, ATLAS_SETTING_CONTOURS, ATLAS_SETTING_COLOR,
       ATLAS_SETTING_SPEED, ATLAS_SETTING_WIREFRAME,
       ATLAS_SETTING_DETAIL, ATLAS_SETTING_HUE_MOTION };
enum { TERRARIUM_SETTING_MOTION, TERRARIUM_SETTING_GROWTH,
       TERRARIUM_SETTING_PARTICLES, TERRARIUM_SETTING_SIM_SPEED,
       TERRARIUM_SETTING_CREATURE_SPEED, TERRARIUM_SETTING_GLASS_OPACITY,
       TERRARIUM_SETTING_DENSITY, TERRARIUM_SETTING_CREATURE_GLOW };
enum { CONSTELLATION_SETTING_MOTION, CONSTELLATION_SETTING_SCALE,
       CONSTELLATION_SETTING_GLOW, CONSTELLATION_SETTING_EVENT_DURATION,
       CONSTELLATION_SETTING_EVENT_REACH, CONSTELLATION_SETTING_HUE_SWING,
       CONSTELLATION_SETTING_DENSITY, CONSTELLATION_SETTING_WEB };
enum { CADENCE_SETTING_SCALE, CADENCE_SETTING_SWARM, CADENCE_SETTING_FOCUS,
       CADENCE_SETTING_BEAT, CADENCE_SETTING_GLOW, CADENCE_SETTING_SPACING,
       CADENCE_SETTING_HUE_SWING };
enum { LOOM_SETTING_DENSITY, LOOM_SETTING_WEIGHT, LOOM_SETTING_COMPLEXITY,
       LOOM_SETTING_EDGE, LOOM_SETTING_SATURATION, LOOM_SETTING_MOTION,
       LOOM_SETTING_GLINTS };

typedef struct Scene_Setting_Descriptor {
    const char *key;
    const char *label;
    float minimum;
    float maximum;
    float default_value;
    unsigned precision;
    Scene_Setting_Kind kind;
} Scene_Setting_Descriptor;

typedef struct Scene_Settings {
    float values[SCENE_SETTINGS_SCENE_COUNT][SCENE_SETTINGS_MAX_CONTROLS];
} Scene_Settings;

typedef struct Scene_Settings_Preset {
    uint64_t id;
    char name[SCENE_SETTINGS_PRESET_NAME_CAPACITY];
    Scene_Settings_Snapshot snapshot;
} Scene_Settings_Preset;

typedef struct Scene_Settings_Preset_Library {
    uint64_t next_id;
    size_t counts[SCENE_SETTINGS_SCENE_COUNT];
    Scene_Settings_Preset items[SCENE_SETTINGS_SCENE_COUNT]
                               [SCENE_SETTINGS_PRESETS_PER_SCENE];
} Scene_Settings_Preset_Library;

typedef struct Scene_Settings_Ui_Layout {
    float inspector_width;
    float workspace_width;
    float tracks_width;
} Scene_Settings_Ui_Layout;

void scene_settings_init(Scene_Settings *settings);
bool scene_settings_valid(const Scene_Settings *settings);
size_t scene_settings_count(size_t scene_index);
const Scene_Setting_Descriptor *scene_settings_descriptor(
    size_t scene_index, size_t setting_index);
float scene_settings_get(const Scene_Settings *settings,
                         size_t scene_index, size_t setting_index);
bool scene_settings_set(Scene_Settings *settings, size_t scene_index,
                        size_t setting_index, float value);
bool scene_settings_reset_scene(Scene_Settings *settings, size_t scene_index);
bool scene_settings_capture(const Scene_Settings *settings, size_t scene_index,
                            Scene_Settings_Snapshot *snapshot);
bool scene_settings_snapshot_valid(size_t scene_index,
                                   const Scene_Settings_Snapshot *snapshot);
bool scene_settings_apply_snapshot(Scene_Settings *settings, size_t scene_index,
                                   const Scene_Settings_Snapshot *snapshot);

void scene_settings_preset_library_init(Scene_Settings_Preset_Library *library);
bool scene_settings_preset_library_valid(
    const Scene_Settings_Preset_Library *library);
bool scene_settings_preset_save(Scene_Settings_Preset_Library *library,
                                size_t scene_index, const char *name,
                                const Scene_Settings *settings,
                                size_t *preset_index);
bool scene_settings_preset_replace(Scene_Settings_Preset_Library *library,
                                   size_t scene_index, size_t preset_index,
                                   const Scene_Settings *settings);
bool scene_settings_preset_apply(const Scene_Settings_Preset_Library *library,
                                 size_t scene_index, size_t preset_index,
                                 Scene_Settings *settings);
bool scene_settings_preset_remove(Scene_Settings_Preset_Library *library,
                                  size_t scene_index, size_t preset_index);

/* The v1 editor stores presets as canonical constant parameter mappings. */
bool scene_settings_mapping_supported(const Musi_Parameter_Mapping *mapping);
bool scene_settings_mappings_supported(const Musi_Parameter_Mapping *mappings,
                                       size_t count);
bool scene_settings_export_mappings(
    const Scene_Settings *settings,
    Musi_Parameter_Mapping *mappings, size_t capacity, size_t *count);
bool scene_settings_import_mappings(
    Scene_Settings *settings,
    const Musi_Parameter_Mapping *mappings, size_t count);

bool scene_settings_ui_layout(float window_width, bool inspector_open,
                              Scene_Settings_Ui_Layout *layout);
bool scene_settings_window_can_expand(int window_x, int window_width,
                                      int monitor_x, int monitor_width,
                                      int inspector_width);

#endif
