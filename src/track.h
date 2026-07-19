#ifndef MUSIALIZER_TRACK_H_
#define MUSIALIZER_TRACK_H_

// Internal track model shared by the desktop UI modules. This type is part of
// the hot-reload Plug state: it must stay a plain POD with fixed-capacity
// members so the whole Plug struct can be memcpy'd between the old and new
// images. Do not add owning pointers or constructor invariants here.
//
// This header is internal to the engine build (src/), not a public API.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <raylib.h>

#include "ascii_art.h"
#include "event_timeline.h"
#include "lyrics.h"
#include "plug.h"
#include "project.h"
#include "render_export.h"
#include "scene.h"
#include "scene_routes.h"
#include "scene_settings.h"
#include "scene_switch.h"
#include "sha256.h"
#include "song_atlas_map.h"
#include "track_timeline.h"

typedef struct {
    char *file_path;
    Music music;
    double duration_seconds;
    bool transport_seekable;
    Lyrics_Document lyrics;
    Scene_Switch_Timeline scene_switches;
    Event_Timeline semantic_events;
    Event_Timeline manual_events;
    uint64_t next_manual_event_id;
    Scene_Id base_scene;
    Scene_Id previous_base_scene;
    bool scene_selection_pending;
    uint64_t scene_seed;
    uint64_t scene_instance_id;
    Render_Export_Config render_config;
    Track_Timeline_Waveform timeline_waveform;
    Song_Atlas_Map song_atlas_map;
    bool song_atlas_map_attempted;
    Scene_Settings scene_settings;
    Scene_Settings playback_scene_settings;
    Scene_Route_Table scene_routes;
    bool cue_settings_active;
    // Track-local presets are project data: read and written byte-stable for
    // old .musi files, copied into the shared per-user library on open, and
    // no longer surfaced directly in the UI.
    Scene_Settings_Preset_Library scene_presets;
    AsciiCell ascii_cells[ASCII_GRID_MAX_CELLS];
    size_t ascii_columns;
    size_t ascii_rows;
    char ascii_image_path[PLUG_RELOAD_PATH_CAPACITY];
    char ascii_image_sha256[SHA256_HEX_SIZE];
    char project_path[PLUG_RELOAD_PATH_CAPACITY];
    Musi_Project_Metadata project_metadata;
    bool project_metadata_initialized;
    bool project_dirty;
    bool project_autosave_failed;
    double project_dirty_since;
    char audio_sha256[SHA256_HEX_SIZE];
    size_t analysis_lane_count;
    Musi_Analysis_Lane_Reference analysis_lanes[MUSI_PROJECT_MAX_ANALYSIS_LANES];
} Track;

typedef struct {
    Track *items;
    size_t count;
    size_t capacity;
} Tracks;

#endif // MUSIALIZER_TRACK_H_
