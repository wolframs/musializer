#ifndef MUSIALIZER_SCENE_SETTINGS_H_
#define MUSIALIZER_SCENE_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>

#include "project.h"

enum {
    SCENE_SETTINGS_SCENE_COUNT = 7,
    SCENE_SETTINGS_MAX_CONTROLS = 8,
};

typedef enum Scene_Setting_Kind {
    SCENE_SETTING_SLIDER,
    SCENE_SETTING_TOGGLE,
} Scene_Setting_Kind;

enum { SPECTRUM_SETTING_AMPLITUDE, SPECTRUM_SETTING_TRAIL,
       SPECTRUM_SETTING_SATURATION };
enum { PULSE_SETTING_SCALE, PULSE_SETTING_RINGS, PULSE_SETTING_MOTION,
       PULSE_SETTING_ARC, PULSE_SETTING_WEIGHT };
enum { ORBITAL_SETTING_MOTION, ORBITAL_SETTING_RADIUS, ORBITAL_SETTING_DEPTH,
       ORBITAL_SETTING_NODES, ORBITAL_SETTING_LINKS };
enum { ASCII_SETTING_MOTION, ASCII_SETTING_CYCLING, ASCII_SETTING_SCANLINES,
       ASCII_SETTING_SPLIT };
enum { ATLAS_SETTING_HEIGHT, ATLAS_SETTING_WIDTH, ATLAS_SETTING_DEPTH,
       ATLAS_SETTING_CAMERA, ATLAS_SETTING_CONTOURS, ATLAS_SETTING_COLOR,
       ATLAS_SETTING_SPEED, ATLAS_SETTING_WIREFRAME };
enum { TERRARIUM_SETTING_MOTION, TERRARIUM_SETTING_GROWTH,
       TERRARIUM_SETTING_PARTICLES };
enum { CONSTELLATION_SETTING_MOTION, CONSTELLATION_SETTING_SCALE,
       CONSTELLATION_SETTING_GLOW };

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
