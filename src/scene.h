#ifndef MUSIALIZER_SCENE_H_
#define MUSIALIZER_SCENE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <raylib.h>

#include "ascii_art.h"
#include "event_timeline.h"
#include "lyrics.h"
#include "semantic_lane.h"
#include "scene_settings.h"
#include "song_atlas_map.h"

typedef enum {
    SCENE_SPECTRUM,
    SCENE_PULSE_FIELD,
    SCENE_ORBITAL_LATTICE,
    SCENE_ASCII_FIELD,
    SCENE_SONG_ATLAS,
    SCENE_SPECTRAL_TERRARIUM,
    SCENE_CONSTELLATION,
    SCENE_CADENCE,
    SCENE_LOOM,
    SCENE_PENTAGRAM,
    COUNT_SCENES,
} Scene_Id;

typedef struct {
    const float *bands;
    const float *trails;
    size_t bands_count;
    float rms;
    float peak;
    float spectral_flux;
    float beat_phase;
    bool onset;
} Scene_Audio_Frame;

typedef struct {
    double time_seconds;
    double duration_seconds;
    float delta_seconds;
    uint64_t frame_index;
    Scene_Audio_Frame audio;
    Semantic_Frame semantic;
    const Lyric_Cue *lyric;
    Event_Timeline_View events;
    const Scene_Settings *settings;
} Scene_Frame;

typedef struct {
    Shader circle_shader;
    int circle_radius_location;
    int circle_power_location;
    Font font;
    const AsciiCell *ascii_cells;
    size_t ascii_columns;
    size_t ascii_rows;
    const Song_Atlas_Map *song_atlas_map;
    const Scene_Settings *settings;
    // Physical target pixels per logical output pixel. Fixed-pixel scene
    // details use this so supersampling changes sampling, not composition.
    float pixel_scale;
} Scene_Renderer;

typedef struct {
    Scene_Id id;
    uint32_t state_version;
    size_t state_size;
    void *state;
    uint64_t seed;
} Scene_Instance;

typedef struct Scene_Descriptor {
    Scene_Id id;
    const char *name;
    uint32_t state_version;
    size_t state_size;
    void (*init)(void *state, uint64_t seed);
    void (*update)(void *state, const Scene_Frame *frame);
    void (*draw)(const void *state, const Scene_Frame *frame, const Scene_Renderer *renderer, Rectangle boundary);
    void (*unload)(void *state);
} Scene_Descriptor;

const Scene_Descriptor *scene_descriptor(Scene_Id id);
const char *scene_name(Scene_Id id);
const char *scene_stable_name(Scene_Id id);

bool scene_instance_init(Scene_Instance *scene, Scene_Id id, uint64_t seed);
bool scene_instance_rebind(Scene_Instance *scene);
bool scene_instance_select(Scene_Instance *scene, Scene_Id id, uint64_t seed);
void scene_instance_update(Scene_Instance *scene, const Scene_Frame *frame);
void scene_instance_draw(const Scene_Instance *scene, const Scene_Frame *frame, const Scene_Renderer *renderer, Rectangle boundary);
void scene_instance_unload(Scene_Instance *scene);

#endif // MUSIALIZER_SCENE_H_
