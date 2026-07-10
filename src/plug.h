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
// Readers retain an explicit decoder for ABI 1. Crossing directly from a
// legacy plug that predates any envelope still needs a one-time process
// restart: after dlclose, no safe destructor for unknown opaque state exists.
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

#define LIST_OF_PLUGS \
    PLUG(plug_init, void, void) \
    PLUG(plug_pre_reload, void*, void) \
    PLUG(plug_post_reload, void, void*) \
    PLUG(plug_load_resource, void*, const char*, size_t*) \
    PLUG(plug_free_resource, void, void*) \
    PLUG(plug_load_track, bool, const char*) \
    PLUG(plug_load_ascii_image, bool, const char*) \
    PLUG(plug_select_scene, bool, const char*) \
    PLUG(plug_record_event, bool, Event_Record) \
    PLUG(plug_start_render, bool, const char*) \
    PLUG(plug_render_active, bool, void) \
    PLUG(plug_render_failed, bool, void) \
    PLUG(plug_shutdown, void, void) \
    PLUG(plug_update, void, void)

#define PLUG(name, ret, ...) typedef ret (name##_t)(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#endif // PLUG_H_
