#ifndef PLUG_H_
#define PLUG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_timeline.h"

// Stable envelope exchanged across the dlclose/LoadLibrary boundary.
//
// Contract and guarantees:
// - Before returning it, the old plug releases callbacks, capture devices,
//   encoder process/pipes, decoded render audio, raylib audio/GPU handles, and
//   any scene resource requiring an old-code destructor.
// - A matching Plug layout retains its plain UI/analyzer/scene/ASCII state;
//   tracks and GPU/audio assets are reopened, and the current track resumes at
//   its previous time and play/pause state.  An active offline render is
//   intentionally cancelled because its process stream is not resumable.
// - A non-matching Plug layout frees opaque heap ownership solely through the
//   allocation inventory, then starts fresh and best-effort restores the
//   selected scene, current track/time, and bounded event snapshot.
//
// The prefix through current_track_was_playing is append-only and lets readers
// reject/release a future opaque layout even when they do not understand fields
// appended by that ABI. Readers also retain an explicit decoder for ABI 1.
// Crossing directly from a legacy plug that predates any envelope still needs
// a one-time process restart: after dlclose, no safe destructor for unknown
// opaque state exists.
#define PLUG_RELOAD_HANDOFF_MAGIC UINT64_C(0x4D555349524C4431)
#define PLUG_RELOAD_HANDOFF_ABI_VERSION 2u
#define PLUG_RELOAD_PATH_CAPACITY 4096u
#define PLUG_RELOAD_SCENE_NAME_CAPACITY 64u

typedef struct {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t struct_size;

    void *opaque_state;
    uint64_t state_magic;
    uint32_t state_version;
    uint32_t reserved;
    size_t state_size;

    // Every heap allocation reachable exclusively through opaque_state.  The
    // receiving plug frees these entries when the opaque layout is rejected.
    void **owned_allocations;
    size_t owned_allocation_count;
    size_t owned_allocation_capacity;

    // Small layout-independent recovery record for incompatible reloads.
    char current_track_path[PLUG_RELOAD_PATH_CAPACITY];
    char scene_name[PLUG_RELOAD_SCENE_NAME_CAPACITY];
    float current_track_position;
    uint64_t scene_seed;
    bool current_track_was_playing;
    size_t event_count;
    Event_Record events[EVENT_TIMELINE_CAPACITY];
} Plug_Reload_Handoff;

// Deterministic workspace state for headless UI capture (`--ui-probe`).
//
// This is a diagnostics-only surface. It opens exactly one workspace panel and
// parks the transport so a screenshot of a given state is reproducible across
// runs; it never mutates project data and never marks a project dirty. It is
// the state-transition equivalent of the button a user would click, so a probe
// capture shows the same UI a real interaction produces -- it is deliberately
// not synthetic mouse/keyboard injection.
typedef enum {
    PLUG_UI_PANEL_NONE = 0,
    PLUG_UI_PANEL_TUNE,
    PLUG_UI_PANEL_EXPORT,
    PLUG_UI_PANEL_LYRICS,
    PLUG_UI_PANEL_ASSIST,
} Plug_Ui_Panel;

// The probe crosses the plug boundary by value, so any path it carries is
// copied rather than borrowed. Ample for a capture-script path and small
// enough that the struct stays cheap to pass.
#define PLUG_UI_PROBE_PATH_CAPACITY 512u

typedef struct {
    Plug_Ui_Panel panel;
    bool fullscreen;
    // Seeking needs a loaded, seekable track; the probe fails rather than
    // silently capturing an unintended playhead.
    bool seek_requested;
    double seek_seconds;
    // Leave the transport running. The spectrum analyzer is fed by the audio
    // callback, so a parked transport decays audio-reactive scenes toward their
    // idle state: judging scene visuals needs playback, at the cost of a
    // frame-exact reproducible capture. Chrome and panel layout should be
    // captured parked.
    bool playing;
    // Arm the Assist confirmation prompt, the two-click state that shrinks the
    // sidebar far enough to collapse the tracks panel. UI state only.
    bool assist_confirmation;
    // Select the nth existing lyric cue (1-based; 0 selects none). Selecting
    // copies the canonical cue into the draft, so nothing becomes dirty. Without
    // it no capture can show the cue-editing form, which is where the panel's
    // worst layout defects live.
    unsigned lyric_selection;
    // Zoom the timeline strip by this factor about the probe's playhead. The
    // probe applies state transitions and cannot turn a mouse wheel, so without
    // this knob no capture can show a zoomed strip -- and a zoom level nobody
    // can photograph is a zoom level nobody reviews. 0 or 1 leaves the whole
    // track in view.
    double timeline_zoom;
    // Show the caption-style pane instead of the cue form. Needs panel=lyrics.
    bool caption_style_pane;
    // Show the caption face browser. Implies caption_style_pane.
    bool font_browser;
    // A family-list file to load straight into the browser, bypassing the
    // network entirely. Without it the browser can only ever be photographed
    // at its consent panel, and a capture run must not be the thing that
    // contacts Google. Empty leaves consent ungranted.
    //
    // Owned by value rather than borrowed: the probe is parsed out of a buffer
    // the parser owns and then handed across the plug boundary, so a pointer
    // into that buffer would dangle by the time it is read.
    char font_catalogue_path[PLUG_UI_PROBE_PATH_CAPACITY];
} Plug_Ui_Probe;

#define LIST_OF_PLUGS \
    PLUG(plug_init, void, void) \
    PLUG(plug_pre_reload, void*, void) \
    PLUG(plug_post_reload, void, void*) \
    PLUG(plug_load_resource, void*, const char*, size_t*) \
    PLUG(plug_free_resource, void, void*) \
    PLUG(plug_load_track, bool, const char*) \
    PLUG(plug_load_project, bool, const char*) \
    PLUG(plug_save_project, bool, const char*) \
    PLUG(plug_load_ascii_image, bool, const char*) \
    PLUG(plug_select_scene, bool, const char*) \
    PLUG(plug_record_event, bool, Event_Record) \
    PLUG(plug_add_scene_route, bool, const char*) \
    PLUG(plug_load_analysis_bridge, bool, const char*) \
    PLUG(plug_set_auto_scenes, bool, bool) \
    PLUG(plug_apply_ui_probe, bool, Plug_Ui_Probe) \
    PLUG(plug_mark_command_line_state_clean, void, void) \
    PLUG(plug_configure_render, bool, uint32_t, uint32_t, uint32_t, const char*) \
    PLUG(plug_configure_render_window, bool, double, double) \
    PLUG(plug_start_render, bool, const char*) \
    PLUG(plug_render_active, bool, void) \
    PLUG(plug_render_failed, bool, void) \
    PLUG(plug_confirm_close, bool, void) \
    PLUG(plug_shutdown, void, void) \
    PLUG(plug_update, void, void)

#define PLUG(name, ret, ...) typedef ret (name##_t)(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#endif // PLUG_H_
