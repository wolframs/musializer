#include "scene_settings.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SETTING(key_, label_, min_, max_, default_, precision_) \
    { key_, label_, min_, max_, default_, precision_, SCENE_SETTING_SLIDER }
#define TOGGLE(key_, label_, default_) \
    { key_, label_, 0.0f, 1.0f, default_, 0, SCENE_SETTING_TOGGLE }

static const Scene_Setting_Descriptor spectrum_settings[] = {
    SETTING("settings.spectrum.amplitude", "Amplitude", 0.40f, 2.00f, 1.00f, 2),
    SETTING("settings.spectrum.trail", "Trail size", 0.25f, 2.50f, 1.00f, 2),
    SETTING("settings.spectrum.saturation", "Saturation", 0.25f, 1.25f, 1.00f, 2),
    SETTING("settings.spectrum.glow_softness", "Glow softness", 1.00f, 8.00f, 3.00f, 2),
    SETTING("settings.spectrum.hue_swing", "Semantic hue swing", 0.00f, 120.00f, 55.00f, 0),
    SETTING("settings.spectrum.core_glow", "Core glow size", 0.00f, 3.00f, 1.00f, 2),
    SETTING("settings.spectrum.bar_taper", "Bar taper", 0.30f, 1.50f, 0.50f, 2),
    SETTING("settings.spectrum.reflection", "Floor reflection", 0.00f, 1.00f, 0.30f, 2),
};

static const Scene_Setting_Descriptor pulse_settings[] = {
    SETTING("settings.pulse.scale", "Field scale", 0.55f, 1.15f, 1.00f, 2),
    SETTING("settings.pulse.rings", "Ring count", 6.00f, 48.00f, 24.00f, 0),
    SETTING("settings.pulse.motion", "Rotation speed", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.pulse.arc", "Arc length", 0.50f, 1.50f, 1.00f, 2),
    SETTING("settings.pulse.weight", "Line weight", 0.30f, 2.50f, 1.00f, 2),
    SETTING("settings.pulse.petals", "Petal fold (0 = auto)", 0.00f, 12.00f, 0.00f, 0),
    SETTING("settings.pulse.hue", "Hue shift (deg)", -180.0f, 180.0f, 0.0f, 0),
    SETTING("settings.pulse.glow", "Center bloom", 0.00f, 2.00f, 1.00f, 2),
};

static const Scene_Setting_Descriptor orbital_settings[] = {
    SETTING("settings.orbital.motion", "Motion speed", 0.15f, 2.00f, 1.00f, 2),
    SETTING("settings.orbital.radius", "Lattice radius", 0.55f, 1.45f, 1.00f, 2),
    SETTING("settings.orbital.depth", "Depth spacing", 0.55f, 1.55f, 1.00f, 2),
    SETTING("settings.orbital.nodes", "Node size", 0.35f, 2.20f, 1.00f, 2),
    SETTING("settings.orbital.links", "Link weight", 0.00f, 2.20f, 1.00f, 2),
    SETTING("settings.orbital.tilt", "Camera tilt", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.orbital.hue", "Hue shift (deg)", -180.0f, 180.0f, 0.0f, 0),
    SETTING("settings.orbital.reactivity", "Beat reactivity", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.orbital.sway", "Drift & sway", 0.00f, 2.00f, 1.00f, 2),
};

static const Scene_Setting_Descriptor ascii_settings[] = {
    SETTING("settings.ascii.motion", "Wave motion", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.ascii.cycling", "Glyph cycling", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.ascii.scanlines", "Scanlines", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.ascii.split", "Color split", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.ascii.gain", "Brightness gain", 0.50f, 2.50f, 1.30f, 2),
    SETTING("settings.ascii.tint", "Band hue spread", 0.00f, 1.00f, 0.45f, 2),
};

static const Scene_Setting_Descriptor atlas_settings[] = {
    SETTING("settings.atlas.height", "Terrain height", 0.35f, 2.75f, 1.00f, 2),
    SETTING("settings.atlas.width", "Terrain width", 0.55f, 3.20f, 1.00f, 2),
    SETTING("settings.atlas.depth", "Depth spacing", 0.50f, 1.65f, 1.00f, 2),
    SETTING("settings.atlas.camera", "Camera height", 0.25f, 1.75f, 1.00f, 2),
    SETTING("settings.atlas.contours", "Contour weight", 0.00f, 2.50f, 1.00f, 2),
    SETTING("settings.atlas.color", "Hue shift (deg)", -180.0f, 180.0f, 0.0f, 0),
    SETTING("settings.atlas.speed", "Camera drift", 0.00f, 2.50f, 1.00f, 2),
    TOGGLE("settings.atlas.wireframe", "Surface style", 0.0f),
    SETTING("settings.atlas.detail", "Sampling detail", 1.00f, 3.00f, 1.00f, 0),
    TOGGLE("settings.atlas.hue_motion", "Hue motion", 0.0f),
    SETTING("settings.atlas.orbit", "Camera orbit", -180.0f, 180.0f, 0.0f, 0),
    SETTING("settings.atlas.zoom", "Camera distance", 0.60f, 1.80f, 1.00f, 2),
};

static const Scene_Setting_Descriptor terrarium_settings[] = {
    SETTING("settings.terrarium.motion", "Camera motion", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.terrarium.growth", "Plant growth", 0.40f, 1.80f, 1.00f, 2),
    SETTING("settings.terrarium.particles", "Particle size", 0.00f, 2.20f, 1.00f, 2),
    SETTING("settings.terrarium.sim_speed", "Ecosystem motion", 0.20f, 2.50f, 1.00f, 2),
    SETTING("settings.terrarium.creature_speed", "Creature speed", 0.20f, 2.00f, 1.00f, 2),
    SETTING("settings.terrarium.glass_opacity", "Habitat glass", 0.00f, 0.40f, 0.13f, 2),
    SETTING("settings.terrarium.density", "Population density", 0.30f, 1.00f, 1.00f, 2),
    SETTING("settings.terrarium.creature_glow", "Creature glow", 0.00f, 2.00f, 1.00f, 2),
};

static const Scene_Setting_Descriptor constellation_settings[] = {
    SETTING("settings.constellation.motion", "Camera motion", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.constellation.scale", "Field scale", 0.50f, 1.60f, 1.00f, 2),
    SETTING("settings.constellation.glow", "Node glow", 0.00f, 2.20f, 1.00f, 2),
    SETTING("settings.constellation.event_duration", "Event glow duration", 0.50f, 5.00f, 2.40f, 2),
    SETTING("settings.constellation.event_reach", "Event spread", 1.00f, 6.00f, 2.00f, 0),
    SETTING("settings.constellation.hue_swing", "Semantic hue swing", 0.00f, 140.00f, 70.00f, 0),
    SETTING("settings.constellation.density", "Star density", 1.00f, 3.00f, 3.00f, 0),
    SETTING("settings.constellation.web", "Web brightness", 0.00f, 2.00f, 1.00f, 2),
};

static const Scene_Setting_Descriptor cadence_settings[] = {
    SETTING("settings.cadence.scale", "Type scale", 0.55f, 1.35f, 1.00f, 2),
    SETTING("settings.cadence.swarm", "Swarm spread", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.cadence.focus", "Focus speed", 0.50f, 3.00f, 1.00f, 2),
    SETTING("settings.cadence.beat", "Beat breathing", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.cadence.glow", "Particle glow", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.cadence.spacing", "Letter spacing", 0.50f, 2.00f, 1.00f, 2),
    SETTING("settings.cadence.hue_swing", "Semantic hue swing", 0.00f, 160.00f, 80.00f, 0),
};

static const Scene_Setting_Descriptor loom_settings[] = {
    SETTING("settings.loom.density", "Thread density", 0.50f, 2.00f, 1.00f, 2),
    SETTING("settings.loom.weight", "Thread weight", 0.40f, 2.50f, 1.00f, 2),
    SETTING("settings.loom.complexity", "Weave complexity", 0.50f, 2.00f, 1.00f, 2),
    SETTING("settings.loom.edge", "Growth edge", 0.50f, 2.00f, 1.00f, 2),
    SETTING("settings.loom.saturation", "Saturation", 0.30f, 1.30f, 1.00f, 2),
    SETTING("settings.loom.motion", "Beat lift", 0.00f, 2.00f, 1.00f, 2),
    SETTING("settings.loom.glints", "Onset glints", 0.00f, 2.00f, 1.00f, 2),
};

typedef struct Scene_Setting_Table {
    const Scene_Setting_Descriptor *items;
    size_t count;
} Scene_Setting_Table;

static const Scene_Setting_Table tables[SCENE_SETTINGS_SCENE_COUNT] = {
    { spectrum_settings, sizeof(spectrum_settings)/sizeof(spectrum_settings[0]) },
    { pulse_settings, sizeof(pulse_settings)/sizeof(pulse_settings[0]) },
    { orbital_settings, sizeof(orbital_settings)/sizeof(orbital_settings[0]) },
    { ascii_settings, sizeof(ascii_settings)/sizeof(ascii_settings[0]) },
    { atlas_settings, sizeof(atlas_settings)/sizeof(atlas_settings[0]) },
    { terrarium_settings, sizeof(terrarium_settings)/sizeof(terrarium_settings[0]) },
    { constellation_settings,
      sizeof(constellation_settings)/sizeof(constellation_settings[0]) },
    { cadence_settings, sizeof(cadence_settings)/sizeof(cadence_settings[0]) },
    { loom_settings, sizeof(loom_settings)/sizeof(loom_settings[0]) },
};

static bool value_in_range(float value, const Scene_Setting_Descriptor *descriptor)
{
    if (descriptor == NULL || !isfinite(value) ||
        value < descriptor->minimum || value > descriptor->maximum) return false;
    return descriptor->kind != SCENE_SETTING_TOGGLE ||
           value == 0.0f || value == 1.0f;
}

size_t scene_settings_count(size_t scene_index)
{
    return scene_index < SCENE_SETTINGS_SCENE_COUNT ? tables[scene_index].count : 0;
}

const Scene_Setting_Descriptor *scene_settings_descriptor(
    size_t scene_index, size_t setting_index)
{
    if (scene_index >= SCENE_SETTINGS_SCENE_COUNT ||
        setting_index >= tables[scene_index].count) return NULL;
    return &tables[scene_index].items[setting_index];
}

void scene_settings_init(Scene_Settings *settings)
{
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < tables[scene].count; ++index) {
            settings->values[scene][index] = tables[scene].items[index].default_value;
        }
    }
}

bool scene_settings_valid(const Scene_Settings *settings)
{
    if (settings == NULL) return false;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < tables[scene].count; ++index) {
            if (!value_in_range(settings->values[scene][index],
                                &tables[scene].items[index])) return false;
        }
    }
    return true;
}

float scene_settings_get(const Scene_Settings *settings,
                         size_t scene_index, size_t setting_index)
{
    const Scene_Setting_Descriptor *descriptor = scene_settings_descriptor(
        scene_index, setting_index);
    if (descriptor == NULL) return 1.0f;
    if (settings == NULL ||
        !value_in_range(settings->values[scene_index][setting_index], descriptor)) {
        return descriptor->default_value;
    }
    return settings->values[scene_index][setting_index];
}

bool scene_settings_set(Scene_Settings *settings, size_t scene_index,
                        size_t setting_index, float value)
{
    const Scene_Setting_Descriptor *descriptor = scene_settings_descriptor(
        scene_index, setting_index);
    if (settings == NULL || !value_in_range(value, descriptor)) return false;
    settings->values[scene_index][setting_index] = value;
    return true;
}

bool scene_settings_reset_scene(Scene_Settings *settings, size_t scene_index)
{
    if (settings == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) return false;
    for (size_t index = 0; index < tables[scene_index].count; ++index) {
        settings->values[scene_index][index] = tables[scene_index].items[index].default_value;
    }
    return true;
}

bool scene_settings_capture(const Scene_Settings *settings, size_t scene_index,
                            Scene_Settings_Snapshot *snapshot)
{
    if (settings == NULL || snapshot == NULL ||
        scene_index >= SCENE_SETTINGS_SCENE_COUNT) return false;
    Scene_Settings_Snapshot staged = {
        .captured = true,
        .count = tables[scene_index].count,
    };
    for (size_t index = 0; index < staged.count; ++index) {
        float value = settings->values[scene_index][index];
        if (!value_in_range(value, &tables[scene_index].items[index])) return false;
        staged.values[index] = value;
    }
    *snapshot = staged;
    return true;
}

// Every historical per-scene control count remains loadable; missing values
// back-fill from descriptor defaults in scene_settings_apply_snapshot.
static bool scene_settings_count_is_legacy(size_t scene_index, size_t count)
{
    switch (scene_index) {
    case 0: return count == 3 || count == 7;  // spectrum
    case 1: return count == 5;                // pulse
    case 2: return count == 5 || count == 7;  // orbital
    case 3: return count == 4;                // ascii
    case 4: return count == 8 || count == 10; // atlas
    case 5: return count == 3 || count == 7;  // terrarium
    case 6: return count == 3 || count == 7;  // constellation
    default: return false;
    }
}

bool scene_settings_snapshot_valid(size_t scene_index,
                                   const Scene_Settings_Snapshot *snapshot)
{
    if (snapshot == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT) return false;
    if (!snapshot->captured) return snapshot->count == 0;
    bool legacy_snapshot = scene_settings_count_is_legacy(scene_index,
                                                          snapshot->count);
    if (snapshot->count != tables[scene_index].count && !legacy_snapshot) return false;
    for (size_t index = 0; index < snapshot->count; ++index) {
        if (!value_in_range(snapshot->values[index],
                            &tables[scene_index].items[index])) return false;
    }
    return true;
}

bool scene_settings_apply_snapshot(Scene_Settings *settings, size_t scene_index,
                                   const Scene_Settings_Snapshot *snapshot)
{
    if (settings == NULL || !scene_settings_snapshot_valid(scene_index, snapshot) ||
        !snapshot->captured) return false;
    Scene_Settings staged = *settings;
    for (size_t index = 0; index < snapshot->count; ++index) {
        staged.values[scene_index][index] = snapshot->values[index];
    }
    for (size_t index = snapshot->count; index < tables[scene_index].count; ++index) {
        staged.values[scene_index][index] = tables[scene_index].items[index].default_value;
    }
    *settings = staged;
    return true;
}

void scene_settings_preset_library_init(Scene_Settings_Preset_Library *library)
{
    if (library == NULL) return;
    memset(library, 0, sizeof(*library));
    library->next_id = 1;
}

bool scene_settings_preset_library_valid(
    const Scene_Settings_Preset_Library *library)
{
    if (library == NULL || library->next_id == 0) return false;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        if (library->counts[scene] > SCENE_SETTINGS_PRESETS_PER_SCENE) return false;
        for (size_t index = 0; index < library->counts[scene]; ++index) {
            const Scene_Settings_Preset *preset = &library->items[scene][index];
            if (preset->id == 0 || preset->name[0] == '\0' ||
                memchr(preset->name, '\0', sizeof(preset->name)) == NULL ||
                !scene_settings_snapshot_valid(scene, &preset->snapshot)) return false;
            if (preset->id >= library->next_id) return false;
            for (size_t previous_scene = 0; previous_scene <= scene; ++previous_scene) {
                size_t previous_count = previous_scene == scene ? index :
                                        library->counts[previous_scene];
                for (size_t previous = 0; previous < previous_count; ++previous) {
                    if (library->items[previous_scene][previous].id == preset->id) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool scene_settings_preset_save(Scene_Settings_Preset_Library *library,
                                size_t scene_index, const char *name,
                                const Scene_Settings *settings,
                                size_t *preset_index)
{
    if (library == NULL || name == NULL || settings == NULL ||
        scene_index >= SCENE_SETTINGS_SCENE_COUNT || library->next_id == 0 ||
        library->next_id == UINT64_MAX ||
        library->counts[scene_index] >= SCENE_SETTINGS_PRESETS_PER_SCENE) return false;
    if (!scene_settings_preset_library_valid(library)) return false;
    const char *name_end = memchr(name, '\0', SCENE_SETTINGS_PRESET_NAME_CAPACITY);
    if (name_end == NULL || name_end == name) return false;
    size_t name_length = (size_t)(name_end - name);
    Scene_Settings_Preset preset = {.id = library->next_id};
    memcpy(preset.name, name, name_length + 1);
    if (!scene_settings_capture(settings, scene_index, &preset.snapshot)) return false;
    size_t index = library->counts[scene_index];
    library->items[scene_index][index] = preset;
    library->counts[scene_index]++;
    library->next_id++;
    if (preset_index != NULL) *preset_index = index;
    return true;
}

bool scene_settings_preset_replace(Scene_Settings_Preset_Library *library,
                                   size_t scene_index, size_t preset_index,
                                   const Scene_Settings *settings)
{
    if (library == NULL || settings == NULL ||
        scene_index >= SCENE_SETTINGS_SCENE_COUNT ||
        preset_index >= library->counts[scene_index]) return false;
    if (!scene_settings_preset_library_valid(library)) return false;
    Scene_Settings_Snapshot snapshot;
    if (!scene_settings_capture(settings, scene_index, &snapshot)) return false;
    library->items[scene_index][preset_index].snapshot = snapshot;
    return true;
}

bool scene_settings_preset_apply(const Scene_Settings_Preset_Library *library,
                                 size_t scene_index, size_t preset_index,
                                 Scene_Settings *settings)
{
    if (library == NULL || settings == NULL ||
        scene_index >= SCENE_SETTINGS_SCENE_COUNT ||
        preset_index >= library->counts[scene_index]) return false;
    if (!scene_settings_preset_library_valid(library)) return false;
    return scene_settings_apply_snapshot(
        settings, scene_index, &library->items[scene_index][preset_index].snapshot);
}

bool scene_settings_preset_remove(Scene_Settings_Preset_Library *library,
                                  size_t scene_index, size_t preset_index)
{
    if (library == NULL || scene_index >= SCENE_SETTINGS_SCENE_COUNT ||
        preset_index >= library->counts[scene_index]) return false;
    if (!scene_settings_preset_library_valid(library)) return false;
    size_t count = library->counts[scene_index];
    if (preset_index + 1 < count) {
        memmove(&library->items[scene_index][preset_index],
                &library->items[scene_index][preset_index + 1],
                (count - preset_index - 1)*sizeof(library->items[scene_index][0]));
    }
    memset(&library->items[scene_index][count - 1], 0,
           sizeof(library->items[scene_index][0]));
    library->counts[scene_index]--;
    return true;
}

static const Scene_Setting_Descriptor *descriptor_for_key(
    const char *key, size_t *scene_index, size_t *setting_index)
{
    if (key == NULL) return NULL;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < tables[scene].count; ++index) {
            if (strcmp(key, tables[scene].items[index].key) == 0) {
                if (scene_index != NULL) *scene_index = scene;
                if (setting_index != NULL) *setting_index = index;
                return &tables[scene].items[index];
            }
        }
    }
    return NULL;
}

bool scene_settings_mapping_supported(const Musi_Parameter_Mapping *mapping)
{
    if (mapping == NULL || descriptor_for_key(mapping->parameter, NULL, NULL) == NULL) {
        return false;
    }
    return mapping->source == MUSI_ANALYSIS_RMS && mapping->band_index == 0 &&
           mapping->input_min == 0.0 && mapping->input_max == 1.0 &&
           mapping->output_min == mapping->output_max &&
           isfinite(mapping->output_min) &&
           mapping->interpolation == MUSI_INTERPOLATION_LINEAR && mapping->clamp;
}

bool scene_settings_mappings_supported(const Musi_Parameter_Mapping *mappings,
                                       size_t count)
{
    if (count > MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE ||
        (count > 0 && mappings == NULL)) return false;
    for (size_t index = 0; index < count; ++index) {
        if (!scene_settings_mapping_supported(&mappings[index])) return false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (strcmp(mappings[previous].parameter,
                       mappings[index].parameter) == 0) return false;
        }
    }
    return true;
}

bool scene_settings_export_mappings(
    const Scene_Settings *settings,
    Musi_Parameter_Mapping *mappings, size_t capacity, size_t *count)
{
    if (!scene_settings_valid(settings) || mappings == NULL || count == NULL) return false;
    size_t required = 0;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        required += tables[scene].count;
    }
    if (required > capacity) return false;

    Musi_Parameter_Mapping staged[MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE] = {0};
    if (required > sizeof(staged)/sizeof(staged[0])) return false;
    size_t at = 0;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < tables[scene].count; ++index) {
            const Scene_Setting_Descriptor *descriptor = &tables[scene].items[index];
            Musi_Parameter_Mapping *mapping = &staged[at++];
            int length = snprintf(mapping->parameter, sizeof(mapping->parameter),
                                  "%s", descriptor->key);
            if (length <= 0 || (size_t)length >= sizeof(mapping->parameter)) return false;
            mapping->source = MUSI_ANALYSIS_RMS;
            mapping->input_min = 0.0;
            mapping->input_max = 1.0;
            mapping->output_min = settings->values[scene][index];
            mapping->output_max = settings->values[scene][index];
            mapping->interpolation = MUSI_INTERPOLATION_LINEAR;
            mapping->clamp = true;
        }
    }
    memcpy(mappings, staged, required*sizeof(staged[0]));
    *count = required;
    return true;
}

bool scene_settings_import_mappings(
    Scene_Settings *settings,
    const Musi_Parameter_Mapping *mappings, size_t count)
{
    if (settings == NULL || !scene_settings_mappings_supported(mappings, count)) {
        return false;
    }
    Scene_Settings staged;
    scene_settings_init(&staged);
    for (size_t at = 0; at < count; ++at) {
        size_t scene = 0;
        size_t index = 0;
        const Scene_Setting_Descriptor *descriptor = descriptor_for_key(
            mappings[at].parameter, &scene, &index);
        float value = (float)mappings[at].output_min;
        if (!value_in_range(value, descriptor)) return false;
        staged.values[scene][index] = value;
    }
    *settings = staged;
    return true;
}

bool scene_settings_ui_layout(float window_width, bool inspector_open,
                              Scene_Settings_Ui_Layout *layout)
{
    if (layout == NULL || !isfinite(window_width) || window_width < 640.0f) return false;
    float inspector_width = 0.0f;
    if (inspector_open) {
        inspector_width = window_width*0.265f;
        if (inspector_width < 280.0f) inspector_width = 280.0f;
        if (inspector_width > 340.0f) inspector_width = 340.0f;
        if (window_width - inspector_width < 620.0f) {
            inspector_width = window_width - 620.0f;
        }
        if (inspector_width < 240.0f) inspector_width = 240.0f;
    }
    float workspace_width = window_width - inspector_width;
    float tracks_width = inspector_open ? workspace_width*0.25f : 320.0f;
    if (tracks_width < 240.0f) tracks_width = 240.0f;
    if (tracks_width > 320.0f) tracks_width = 320.0f;
    // At very narrow embedder widths, preserve the visualizer as the primary
    // surface while retaining a compact (rather than vanished) project rail.
    float minimum_preview = window_width*0.30f;
    if (inspector_open && workspace_width - tracks_width < minimum_preview) {
        tracks_width = fmaxf(168.0f, workspace_width - minimum_preview);
    }
    *layout = (Scene_Settings_Ui_Layout) {
        .inspector_width = inspector_width,
        .workspace_width = workspace_width,
        .tracks_width = tracks_width,
    };
    return true;
}

bool scene_settings_window_can_expand(int window_x, int window_width,
                                      int monitor_x, int monitor_width,
                                      int inspector_width)
{
    if (window_width <= 0 || monitor_width <= 0 || inspector_width <= 0) return false;
    int64_t window_right = (int64_t)window_x + window_width;
    int64_t target_right = window_right + inspector_width;
    int64_t monitor_left = monitor_x;
    int64_t monitor_right = (int64_t)monitor_x + monitor_width;
    return window_x >= monitor_left && window_right <= monitor_right &&
           target_right <= monitor_right;
}
