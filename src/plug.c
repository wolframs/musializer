#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "build/config.h"
#include "analysis_bridge.h"
#include "analysis_candidate.h"
#include "caption_layout.h"
#include "audio_analyzer.h"
#include "editor_draft.h"
#include "plug.h"
#include "ffmpeg.h"
#include "lyrics.h"
#include "project.h"
#include "project_io.h"
#include "sample_ring.h"
#include "scene.h"
#include "scene_event_merge.h"
#include "scene_switch.h"
#include "sha256.h"
#include "track_timeline.h"
#include "ui_notice.h"
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
// #define NOB_WARN_DEPRECATED
#include "thirdparty/nob.h"
#include "thirdparty/tinyfiledialogs.h"

#include <raylib.h>
#include <rlgl.h>

#if defined(_WIN32) && defined(MUSIALIZER_HOTRELOAD)
    #define MUSIALIZER_PLUG __declspec(dllexport)
#else
    #define MUSIALIZER_PLUG
#endif

#define PLUG(name, ret, ...) MUSIALIZER_PLUG ret name(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#ifndef MUSIALIZER_UNBUNDLE
#include "build/bundle.h"

MUSIALIZER_PLUG void plug_free_resource(void *data)
{
    (void) data;
}

MUSIALIZER_PLUG void *plug_load_resource(const char *file_path, size_t *size)
{
    for (size_t i = 0; i < resources_count; ++i) {
        if (strcmp(resources[i].file_path, file_path) == 0) {
            *size = resources[i].size;
            return &bundle[resources[i].offset];
        }
    }
    return NULL;
}

#else

MUSIALIZER_PLUG void plug_free_resource(void *data)
{
    UnloadFileData(data);
}

MUSIALIZER_PLUG void *plug_load_resource(const char *file_path, size_t *size)
{
    int dataSize;
    void *data = LoadFileData(file_path, &dataSize);
    *size = dataSize;
    return data;
}
#endif

#define _WINDOWS_
#include "external/miniaudio.h"
#include "external/dr_wav.h"

#define GLSL_VERSION 330

#define FONT_SIZE 64
#define SAMPLE_RING_CAPACITY (1u << 14)
#define ASCII_GRID_MAX_COLUMNS 96
#define ASCII_GRID_MAX_ROWS 54
#define ASCII_GRID_MAX_CELLS (ASCII_GRID_MAX_COLUMNS*ASCII_GRID_MAX_ROWS)

#define PREVIEW_FPS 60

#define PLUG_STATE_MAGIC UINT64_C(0x4D555349504C5547)
#define PLUG_STATE_VERSION 14

#define COLOR_ACCENT                  GetColor(0x002FA7FF)
#define COLOR_BACKGROUND              GetColor(0x151515FF)
#define COLOR_UI_SURFACE              GetColor(0xF7F7F8FF)
#define COLOR_UI_RAISED               GetColor(0xFFFFFFFF)
#define COLOR_UI_INK                  GetColor(0x141414FF)
#define COLOR_UI_MUTED                GetColor(0x66666BFF)
#define COLOR_UI_RULE                 GetColor(0xD2D2D6FF)
#define COLOR_TRACK_PANEL_BACKGROUND  COLOR_UI_SURFACE
#define COLOR_TRACK_BUTTON_BACKGROUND COLOR_UI_RAISED
#define COLOR_TRACK_BUTTON_HOVEROVER  GetColor(0xE7EAF2FF)
#define COLOR_TRACK_BUTTON_SELECTED   COLOR_ACCENT
#define COLOR_TIMELINE_CURSOR         COLOR_ACCENT
#define COLOR_TIMELINE_BACKGROUND     COLOR_UI_SURFACE
#define COLOR_HUD_BUTTON_BACKGROUND   COLOR_TRACK_BUTTON_BACKGROUND
#define COLOR_HUD_BUTTON_HOVEROVER    COLOR_TRACK_BUTTON_HOVEROVER
#define COLOR_TOOLTIP_BACKGROUND      COLOR_UI_INK
#define COLOR_TOOLTIP_FOREGROUND      WHITE
#define HUD_TIMER_SECS 1.0f
#define HUD_BUTTON_SIZE 50
#define HUD_BUTTON_MARGIN 50
#define HUD_ICON_SCALE 0.5
#define HUD_POPUP_LIFETIME_SECS 2.0f
#define HUD_POPUP_SLIDEIN_SECS 0.1f
#define TOOLTIP_PADDING 20.0f
#define TRACKLABEL_SCROLL_SECS 0.05f

#define KEY_TOGGLE_PLAY KEY_SPACE
#define KEY_RENDER      KEY_R
#define KEY_FULLSCREEN  KEY_F
#define KEY_CAPTURE     KEY_C
#define KEY_TOGGLE_MUTE KEY_M

typedef struct {
    char *file_path;
    Music music;
    double duration_seconds;
    Lyrics_Document lyrics;
    Scene_Switch_Timeline scene_switches;
    Event_Timeline semantic_events;
    Event_Timeline manual_events;
    uint64_t next_manual_event_id;
    Scene_Id base_scene;
    uint64_t scene_seed;
    uint64_t scene_instance_id;
    Render_Export_Config render_config;
    Track_Timeline_Waveform timeline_waveform;
    AsciiCell ascii_cells[ASCII_GRID_MAX_CELLS];
    size_t ascii_columns;
    size_t ascii_rows;
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

typedef enum {
    ASSIST_MODE_LYRICS,
    ASSIST_MODE_SECTIONS,
    ASSIST_MODE_MIMO,
    ASSIST_MODE_ALL,
} Assist_Mode;

typedef enum {
    ASSIST_JOB_IDLE,
    ASSIST_JOB_RUNNING,
    ASSIST_JOB_SUCCEEDED,
    ASSIST_JOB_FAILED,
    ASSIST_JOB_CANCELLED,
} Assist_Job_State;


typedef enum {
    SIDE_LEFT,
    SIDE_RIGHT,
    SIDE_TOP,
    SIDE_BOTTOM,
} Side;

typedef enum {
    UI_ICON_FULLSCREEN,
    UI_ICON_VOLUME,
    UI_ICON_PLAY,
    UI_ICON_RENDER,
    UI_ICON_MICROPHONE,
    COUNT_UI_ICONS,
} UI_Icon;

static_assert(COUNT_UI_ICONS == 5, "Amount of icons changed");
static const char *icon_file_paths[COUNT_UI_ICONS] = {
    [UI_ICON_FULLSCREEN] = "./resources/icons/fullscreen.png",
    [UI_ICON_VOLUME]     = "./resources/icons/volume.png",
    [UI_ICON_PLAY]       = "./resources/icons/play.png",
    [UI_ICON_RENDER]     = "./resources/icons/render.png",
    [UI_ICON_MICROPHONE] = "./resources/icons/microphone.png",
};

typedef struct {
    // Hot-reload state header. Keep these fields at the beginning and bump the
    // version whenever persisted layout semantics change.
    uint64_t state_magic;
    uint32_t state_version;
    uint32_t state_reserved;
    size_t state_size;

    // Assets
    Texture2D icon_textures[COUNT_UI_ICONS];

    // Visualizer
    Tracks tracks;
    int current_track;
    Font ui_font;
    Font font;
    Shader circle;
    int circle_radius_location;
    int circle_power_location;
    bool fullscreen;

    // Renderer
    bool rendering;
    RenderTexture2D screen;
    Wave wave;
    float *wave_samples;
    size_t wave_cursor;
    FFMPEG *ffmpeg;
    bool cancel_rendering;
    bool render_failed;
    bool render_finishing;
    bool export_panel_open;
    Render_Export_Config render_config;
    Render_Resolution render_resolution;
    Render_Frame_Rate render_frame_rate;
    uint64_t render_total_frames;
    uint64_t render_job_nonce;
    double render_started_at;
    float render_restore_position;
    bool render_restore_playing;
    char render_output_path[PLUG_RELOAD_PATH_CAPACITY];
    char render_audio_path[PLUG_RELOAD_PATH_CAPACITY];

    // Audio analyzer and realtime-safe callback handoff
    AudioAnalyzer analyzer;
    SampleRing sample_ring;
    SampleFrame sample_ring_storage[SAMPLE_RING_CAPACITY];

    // Scene engine
    Scene_Instance scene;
    uint64_t scene_frame_index;
    double scene_previous_time;
    bool scene_clock_initialized;
    AsciiCell ascii_cells[ASCII_GRID_MAX_CELLS];
    size_t ascii_columns;
    size_t ascii_rows;
    Event_Timeline event_timeline;
    Event_Timeline event_undo;
    bool event_undo_available;
    bool clear_events_confirmation;
    uint64_t next_event_id;
    Scene_Event_Merge scene_events;
    uint64_t scene_events_user_revision;
    uint64_t scene_events_semantic_revision;
    int scene_events_track;

    // Lyrics content/sync editor. The canonical document lives with each
    // track; these fields are only the current UI draft.
    bool lyrics_editor_open;
    bool lyric_text_active;
    bool lyric_draft_new;
    uint64_t selected_lyric_id;
    double lyric_draft_start;
    double lyric_draft_end;
    char lyric_draft_text[LYRICS_TEXT_CAPACITY];
    size_t lyric_list_first;
    bool lyric_list_follow_selection;

    // Optional analysis helpers are child processes, never realtime work.
    bool assist_panel_open;
    Assist_Mode assist_mode;
    Assist_Job_State assist_job_state;
    Nob_Proc assist_process;
    size_t assist_track_index;
    char assist_output_dir[PLUG_RELOAD_PATH_CAPACITY];
    char assist_bridge_path[PLUG_RELOAD_PATH_CAPACITY];
    char assist_log_path[PLUG_RELOAD_PATH_CAPACITY];
    uint64_t assist_job_nonce;
    double assist_started_at;
    bool assist_confirmation_pending;
    bool assist_apply_confirmation_pending;
    Analysis_Candidate *assist_candidate;
    size_t assist_candidate_track_index;
    Assist_Mode assist_candidate_mode;

    uint64_t active_button_id;

    Ui_Notice_Queue notices;

    bool tooltip_show;
    char tooltip_buffer[32];
    Side tooltip_align;
    Rectangle tooltip_element_boundary;

#ifdef MUSIALIZER_MICROPHONE
    bool capturing;
    ma_device microphone;
    drwav wav;
    bool microphone_working;
#endif // MUSIALIZER_MICROPHONE
} Plug;

static Plug *p = NULL;

static Font ui_font(void)
{
    if (p != NULL && IsFontValid(p->ui_font)) return p->ui_font;
    return GetFontDefault();
}

static bool ui_font_codepoint(int codepoint)
{
    return (codepoint >= 0x20 && codepoint <= 0x024F) ||
           (codepoint >= 0x2000 && codepoint <= 0x206F) ||
           (codepoint >= 0x20A0 && codepoint <= 0x20CF) ||
           (codepoint >= 0x2100 && codepoint <= 0x214F);
}

static void analyzer_configure(uint32_t sample_rate, uint32_t channels)
{
    AudioAnalyzerConfig config = {
        .sample_rate = sample_rate,
        .channel_count = channels,
        .channel_mode = AUDIO_ANALYZER_CHANNEL_SELECT,
        .selected_channel = 0,
    };
    NOB_ASSERT(audio_analyzer_init(&p->analyzer, config));
    sample_ring_reset(&p->sample_ring);
}

static RenderTexture2D load_offline_render_target(const Render_Export_Config *config)
{
    if (render_export_config_validate(config) != RENDER_EXPORT_OK) {
        return (RenderTexture2D){0};
    }
    const char *supersampling = getenv("MUSIALIZER_RENDER_SUPERSAMPLE");
    uint32_t factor = config->supersample_factor;
    if (supersampling != NULL && strcmp(supersampling, "0") == 0) factor = 1;
    if (factor > 1) {
        RenderTexture2D target = LoadRenderTexture(
            (int)(config->width*factor), (int)(config->height*factor));
        if (IsRenderTextureValid(target) && rlFramebufferComplete(target.id)) return target;

        TraceLog(LOG_WARNING,
                 "Could not create supersampled render target; falling back to output resolution");
        if (target.id != 0) UnloadRenderTexture(target);
    } else if (config->supersample_factor > 1) {
        TraceLog(LOG_INFO, "Offline render supersampling disabled by environment");
    }
    return LoadRenderTexture((int)config->width, (int)config->height);
}

static void fft_clean(void)
{
    audio_analyzer_reset(&p->analyzer);
    sample_ring_reset(&p->sample_ring);
}

static void analyzer_drain_realtime_samples(void)
{
    SampleFrame frames[1024];
    float mono[1024];
#ifdef MUSIALIZER_MICROPHONE
    float stereo[1024*2];
#endif
    size_t count;
    while ((count = sample_ring_pop_many(&p->sample_ring, frames, NOB_ARRAY_LEN(frames))) > 0) {
        // Do not treat an array of structs as a flat float array. Besides
        // relying on padding, pointer arithmetic across member subobjects is
        // undefined in C. Preview parity intentionally analyzes the left side.
        for (size_t i = 0; i < count; ++i) {
            mono[i] = frames[i].left;
#ifdef MUSIALIZER_MICROPHONE
            stereo[i*2] = frames[i].left;
            stereo[i*2 + 1] = frames[i].right;
#endif
        }
        audio_analyzer_push_mono(&p->analyzer, mono, count);
#ifdef MUSIALIZER_MICROPHONE
        if (p->capturing) {
            // Recording I/O belongs to the render thread, never the realtime
            // capture callback. A short write remains diagnosable in logs.
            drwav_uint64 written = drwav_write_pcm_frames(&p->wav, count, stereo);
            if (written != count) {
                TraceLog(LOG_ERROR, "DRWAVE: wrote %llu of %zu captured frames",
                         (unsigned long long)written, count);
            }
        }
#endif
    }
}

static AudioSpectrumView fft_analyze(float dt)
{
    analyzer_drain_realtime_samples();
    if (!audio_analyzer_analyze(&p->analyzer, dt)) return (AudioSpectrumView){0};
    return audio_analyzer_spectrum(&p->analyzer);
}

static void queue_stereo_samples(const void *buffer_data, size_t frame_count)
{
    if (buffer_data == NULL) return;
    const float (*samples)[2] = (const float (*)[2])buffer_data;

    for (size_t i = 0; i < frame_count; ++i) {
        sample_ring_push(&p->sample_ring, (SampleFrame){samples[i][0], samples[i][1]});
    }
}

static void callback(void *bufferData, unsigned int frames)
{
    // raylib invokes stream processors after conversion to its stereo mixing
    // format (AUDIO_DEVICE_CHANNELS == 2 in the vendored configuration).
    queue_stereo_samples(bufferData, frames);
}

#ifdef MUSIALIZER_MICROPHONE
static void ma_callback(ma_device *pDevice, void *pOutput, const void *pInput,ma_uint32 frameCount)
{
    if (pInput == NULL) return;
    queue_stereo_samples(pInput, frameCount);
    (void)pOutput;
    (void)pDevice;
}
#endif // MUSIALIZER_MICROPHONE

static Track *current_track(void)
{
    if (0 <= p->current_track && (size_t) p->current_track < p->tracks.count) {
        return &p->tracks.items[p->current_track];
    }
    return NULL;
}

static uint64_t scene_seed_for_track(const Track *track)
{
    return track != NULL ? track->scene_seed : p->scene.seed;
}

static void mark_project_dirty(Track *track)
{
    if (track == NULL) return;
    track->project_dirty = true;
    track->project_autosave_failed = false;
    track->project_dirty_since = GetTime();
}

static uint64_t timeline_next_id(const Event_Timeline *timeline)
{
    uint64_t next = 1;
    if (timeline == NULL) return next;
    for (size_t i = 0; i < timeline->count; ++i) {
        uint64_t id = timeline->events[i].id;
        if (id >= next && id != UINT64_MAX) next = id + 1;
    }
    return next;
}

static void set_active_render_config(Render_Export_Config config)
{
    p->render_config = config;
    p->render_resolution = RENDER_RESOLUTION_COUNT;
    for (int i = 0; i < RENDER_RESOLUTION_COUNT; ++i) {
        Render_Export_Config candidate = config;
        (void)render_export_config_set_resolution(&candidate, (Render_Resolution)i);
        if (candidate.width == config.width && candidate.height == config.height) {
            p->render_resolution = (Render_Resolution)i;
            break;
        }
    }
    p->render_frame_rate = RENDER_FRAME_RATE_COUNT;
    for (int i = 0; i < RENDER_FRAME_RATE_COUNT; ++i) {
        Render_Export_Config candidate = config;
        (void)render_export_config_set_frame_rate(&candidate, (Render_Frame_Rate)i);
        if (candidate.fps == config.fps) {
            p->render_frame_rate = (Render_Frame_Rate)i;
            break;
        }
    }
}

static void start_preview_track(Track *track)
{
    analyzer_configure(track->music.stream.sampleRate, 2);
    p->scene_frame_index = 0;
    p->scene_clock_initialized = false;
    scene_switch_reset(&track->scene_switches);
    PlayMusicStream(track->music);
}

static float scene_clock_delta(double time_seconds)
{
    if (!p->scene_clock_initialized) {
        p->scene_previous_time = time_seconds;
        p->scene_clock_initialized = true;
        return 0.0f;
    }

    double elapsed = time_seconds - p->scene_previous_time;
    p->scene_previous_time = time_seconds;
    if (elapsed < 0.0 || elapsed > 0.5) {
        // Seeking is a discontinuity, not a half-second simulation step.
        return 0.0f;
    }
    return (float)elapsed;
}

static void load_timeline_waveform(Track *track)
{
    if (track == NULL || track->file_path == NULL) return;
    memset(&track->timeline_waveform, 0, sizeof(track->timeline_waveform));

    Wave wave = LoadWave(track->file_path);
    if (!IsWaveValid(wave)) {
        TraceLog(LOG_WARNING, "TIMELINE: waveform preview could not decode %s",
                 track->file_path);
        return;
    }
    float *samples = LoadWaveSamples(wave);
    if (samples != NULL) {
        track->timeline_waveform.count = track_timeline_build_waveform(
            samples, (size_t)wave.frameCount, (size_t)wave.channels,
            track->timeline_waveform.bins,
            NOB_ARRAY_LEN(track->timeline_waveform.bins));
        UnloadWaveSamples(samples);
    }
    UnloadWave(wave);
}

MUSIALIZER_PLUG bool plug_load_track(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') return false;

    char canonical_path[PLUG_RELOAD_PATH_CAPACITY];
    if (musi_project_canonicalize_existing_file(
            file_path, canonical_path, sizeof(canonical_path)) !=
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE) return false;

    Music music = LoadMusicStream(canonical_path);
    if (!IsMusicValid(music)) return false;

    char *owned_path = strdup(canonical_path);
    if (owned_path == NULL) {
        UnloadMusicStream(music);
        return false;
    }

    nob_da_reserve(&p->tracks, p->tracks.count + 1);
    size_t new_index = p->tracks.count;
    Track *new_track = &p->tracks.items[new_index];
    memset(new_track, 0, sizeof(*new_track));
    double decoded_duration = GetMusicTimeLength(music);
    if (lyrics_document_init(&new_track->lyrics, decoded_duration) !=
        LYRICS_OK) {
        free(owned_path);
        UnloadMusicStream(music);
        return false;
    }
    AttachAudioStreamProcessor(music.stream, callback);
    Track *active_track = current_track();
    Scene_Id initial_scene = active_track != NULL ? active_track->base_scene : p->scene.id;
    uint64_t initial_scene_seed = scene_seed_for_track(active_track);
    new_track->file_path = owned_path;
    new_track->music = music;
    new_track->duration_seconds = decoded_duration;
    scene_switch_init(&new_track->scene_switches);
    event_timeline_init(&new_track->semantic_events);
    event_timeline_init(&new_track->manual_events);
    new_track->next_manual_event_id = 1;
    new_track->base_scene = initial_scene;
    new_track->scene_seed = initial_scene_seed;
    new_track->scene_instance_id = 1;
    new_track->render_config = p->render_config;
    load_timeline_waveform(new_track);
    if (p->ascii_columns > 0 && p->ascii_rows > 0) {
        memcpy(new_track->ascii_cells, p->ascii_cells,
               p->ascii_columns*p->ascii_rows*sizeof(p->ascii_cells[0]));
        new_track->ascii_columns = p->ascii_columns;
        new_track->ascii_rows = p->ascii_rows;
        p->ascii_columns = 0;
        p->ascii_rows = 0;
    }
    p->tracks.count += 1;

    if (current_track() == NULL) {
        if (p->event_timeline.count > 0) {
            (void)event_timeline_replace(&p->tracks.items[new_index].manual_events,
                                         &p->event_timeline);
            p->tracks.items[new_index].next_manual_event_id =
                timeline_next_id(&p->tracks.items[new_index].manual_events);
            event_timeline_clear(&p->event_timeline);
            p->next_event_id = 1;
        }
        p->current_track = (int)new_index;
        start_preview_track(&p->tracks.items[new_index]);
    }
    return true;
}

MUSIALIZER_PLUG bool plug_load_ascii_image(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') return false;

    Image image = LoadImage(file_path);
    if (!IsImageValid(image)) return false;
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    size_t columns = (size_t)image.width;
    if (columns > ASCII_GRID_MAX_COLUMNS) columns = ASCII_GRID_MAX_COLUMNS;
    size_t rows = (size_t)image.height*columns/(size_t)image.width;
    rows = (rows + 1)/2; // Alegreya glyphs are roughly twice as tall as wide.
    if (rows < 1) rows = 1;
    if (rows > ASCII_GRID_MAX_ROWS) rows = ASCII_GRID_MAX_ROWS;

    Track *track = current_track();
    AsciiCell *destination = track != NULL ? track->ascii_cells : p->ascii_cells;
    bool converted = ascii_art_convert_rgba8(
        image.data,
        (size_t)image.width,
        (size_t)image.height,
        columns,
        rows,
        destination,
        ASCII_GRID_MAX_CELLS);
    UnloadImage(image);
    if (!converted) return false;

    if (track != NULL) {
        track->ascii_columns = columns;
        track->ascii_rows = rows;
        mark_project_dirty(track);
    } else {
        p->ascii_columns = columns;
        p->ascii_rows = rows;
    }
    TraceLog(LOG_INFO, "ASCII: imported %s as %zux%zu glyphs", file_path, columns, rows);
    return true;
}

static bool scene_id_from_name(const char *name, Scene_Id *result)
{
    if (name == NULL || result == NULL) return false;

    Scene_Id id;
    if (strcmp(name, "spectrum") == 0) {
        id = SCENE_SPECTRUM;
    } else if (strcmp(name, "pulse") == 0 || strcmp(name, "pulse-field") == 0) {
        id = SCENE_PULSE_FIELD;
    } else if (strcmp(name, "orbital") == 0 || strcmp(name, "orbital-lattice") == 0) {
        id = SCENE_ORBITAL_LATTICE;
    } else if (strcmp(name, "ascii") == 0 || strcmp(name, "ascii-field") == 0) {
        id = SCENE_ASCII_FIELD;
    } else if (strcmp(name, "atlas") == 0 || strcmp(name, "song-atlas") == 0) {
        id = SCENE_SONG_ATLAS;
    } else if (strcmp(name, "terrarium") == 0 || strcmp(name, "spectral-terrarium") == 0) {
        id = SCENE_SPECTRAL_TERRARIUM;
    } else if (strcmp(name, "constellation") == 0) {
        id = SCENE_CONSTELLATION;
    } else {
        return false;
    }

    *result = id;
    return true;
}

MUSIALIZER_PLUG bool plug_select_scene(const char *name)
{
    Scene_Id id;
    if (!scene_id_from_name(name, &id)) return false;

    Track *track = current_track();
    bool selected = scene_instance_select(&p->scene, id, scene_seed_for_track(track));
    if (selected) {
        if (track != NULL) {
            track->base_scene = id;
            track->scene_switches.enabled = false;
            scene_switch_reset(&track->scene_switches);
        }
        mark_project_dirty(track);
    }
    return selected;
}

static const char *scene_stable_name(Scene_Id id)
{
    switch (id) {
    case SCENE_SPECTRUM: return "spectrum";
    case SCENE_PULSE_FIELD: return "pulse";
    case SCENE_ORBITAL_LATTICE: return "orbital";
    case SCENE_ASCII_FIELD: return "ascii";
    case SCENE_SONG_ATLAS: return "atlas";
    case SCENE_SPECTRAL_TERRARIUM: return "terrarium";
    case SCENE_CONSTELLATION: return "constellation";
    case COUNT_SCENES: break;
    }
    return "spectrum";
}

static void apply_auto_scene_switch(Track *track, double time_seconds)
{
    if (track == NULL) return;
    uint32_t scene_index = 0;
    if (scene_switch_update(&track->scene_switches, time_seconds, &scene_index) !=
        SCENE_SWITCH_OK) return;
    Scene_Id scene = (Scene_Id)scene_index;
    if (scene < COUNT_SCENES && p->scene.id != scene) {
        scene_instance_select(&p->scene, scene, track->scene_seed);
    }
}

MUSIALIZER_PLUG bool plug_record_event(Event_Record event)
{
    Track *track = current_track();
    Event_Timeline *timeline = track != NULL ? &track->manual_events : &p->event_timeline;
    if (event_timeline_record(timeline, &event) != EVENT_TIMELINE_OK) {
        return false;
    }
    uint64_t *next_id = track != NULL ? &track->next_manual_event_id : &p->next_event_id;
    if (event.id >= *next_id && event.id != UINT64_MAX) {
        *next_id = event.id + 1;
    }
    if (track != NULL) mark_project_dirty(track);
    p->event_undo_available = false;
    p->clear_events_confirmation = false;
    return true;
}

static Event_Timeline_View combined_scene_events(void)
{
    Track *track = current_track();
    uint64_t semantic_revision = track != NULL ? track->semantic_events.revision : 0;
    uint64_t user_revision = track != NULL ? track->manual_events.revision :
                                             p->event_timeline.revision;
    if (p->scene_events_user_revision == user_revision &&
        p->scene_events_semantic_revision == semantic_revision &&
        p->scene_events_track == p->current_track) {
        return scene_event_merge_view(&p->scene_events);
    }
    Event_Timeline empty_semantic;
    event_timeline_init(&empty_semantic);
    const Event_Timeline *authored = track != NULL ? &track->manual_events :
                                                    &p->event_timeline;
    const Event_Timeline *semantic = track != NULL ? &track->semantic_events :
                                                    &empty_semantic;
    Event_Timeline_Result merged = scene_event_merge_build(
        &p->scene_events, authored, semantic);
    if (merged != EVENT_TIMELINE_OK) {
        TraceLog(LOG_ERROR, "EVENTS: could not build merged scene view: %s",
                 event_timeline_result_string(merged));
        p->scene_events.count = 0;
    }
    p->scene_events_user_revision = user_revision;
    p->scene_events_semantic_revision = semantic_revision;
    p->scene_events_track = p->current_track;
    return scene_event_merge_view(&p->scene_events);
}

static Scene_Frame make_scene_frame(AudioSpectrumView spectrum, double time_seconds, float delta_seconds)
{
    float square_sum = 0.0f;
    float peak = 0.0f;
    float flux = 0.0f;
    for (size_t i = 0; i < spectrum.band_count; ++i) {
        float band = spectrum.smooth[i];
        square_sum += band*band;
        if (peak < band) peak = band;
        if (spectrum.smear[i] < band) flux += band - spectrum.smear[i];
    }

    float rms = spectrum.band_count > 0 ? sqrtf(square_sum/spectrum.band_count) : 0.0f;
    if (spectrum.band_count > 0) flux /= spectrum.band_count;

    Track *track = current_track();
    Semantic_Frame semantic = {0};
    const Lyric_Cue *lyric = NULL;
    if (track != NULL) {
        (void)semantic_lane_sample(event_timeline_view(&track->semantic_events),
                                   time_seconds, &semantic);
        lyric = lyrics_at_time(&track->lyrics, time_seconds);
    }

    return (Scene_Frame) {
        .time_seconds = time_seconds,
        .delta_seconds = delta_seconds,
        .frame_index = p->scene_frame_index++,
        .semantic = semantic,
        .lyric = lyric,
        .events = combined_scene_events(),
        .audio = {
            .bands = spectrum.smooth,
            .trails = spectrum.smear,
            .bands_count = spectrum.band_count,
            .rms = rms,
            .peak = peak,
            .spectral_flux = flux,
            .onset = flux > 0.08f,
        },
    };
}

typedef struct {
    Font font;
    float font_size;
    float spacing;
} Caption_Raylib_Measurement;

static float caption_measure_raylib(const char *text, void *user_data)
{
    Caption_Raylib_Measurement *measurement = user_data;
    return MeasureTextEx(measurement->font, text, measurement->font_size,
                         measurement->spacing).x;
}

static void draw_scene_lyric_overlay(Rectangle boundary,
                                     const Lyric_Cue *lyric,
                                     Font font,
                                     float pixel_scale)
{
    if (lyric == NULL || lyric->text[0] == '\0' ||
        pixel_scale <= 0.0f || boundary.width < 240.0f*pixel_scale ||
        boundary.height < 160.0f*pixel_scale) return;
    float font_size = fminf(42.0f*pixel_scale,
                            fmaxf(20.0f*pixel_scale, boundary.height*0.047f));
    float spacing = 1.0f*pixel_scale;
    float horizontal_padding = font_size*0.7f;
    float vertical_padding = font_size*0.34f;
    float maximum = fminf(boundary.width*0.82f,
                          boundary.width - 2.0f*(horizontal_padding +
                                                12.0f*pixel_scale));
    Caption_Raylib_Measurement measurement = {font, font_size, spacing};
    Caption_Layout layout;
    if (caption_layout_utf8(lyric->text, maximum, caption_measure_raylib,
                            &measurement, &layout) != CAPTION_LAYOUT_OK) return;

    float widest = 0.0f;
    for (size_t i = 0; i < layout.line_count; ++i) {
        if (layout.lines[i].width > widest) widest = layout.lines[i].width;
    }
    float line_advance = font_size*1.12f;
    float text_height = font_size +
        (layout.line_count - 1u)*line_advance;
    float box_width = fminf(boundary.width - 24.0f*pixel_scale,
                            widest + horizontal_padding*2.0f);
    Rectangle box = {
        boundary.x + (boundary.width - box_width)*0.5f,
        boundary.y + boundary.height - text_height - vertical_padding*2.0f -
            boundary.height*0.065f,
        box_width,
        text_height + vertical_padding*2.0f,
    };
    DrawRectangleRounded(box, 0.12f, 8, ColorAlpha(BLACK, 0.72f));
    DrawRectangleLinesEx(box, 1.0f*pixel_scale, ColorAlpha(WHITE, 0.28f));
    BeginScissorMode((int)box.x, (int)box.y, (int)box.width, (int)box.height);
    for (size_t i = 0; i < layout.line_count; ++i) {
        DrawTextEx(font, layout.lines[i].text,
                   (Vector2){box.x + (box.width - layout.lines[i].width)*0.5f,
                             box.y + vertical_padding + i*line_advance},
                   font_size, spacing, WHITE);
    }
    EndScissorMode();
}

static void scene_render(Rectangle boundary, AudioSpectrumView spectrum, double time_seconds, float delta_seconds)
{
    Scene_Frame frame = make_scene_frame(spectrum, time_seconds, delta_seconds);
    Track *track = current_track();
    const AsciiCell *ascii_cells = track != NULL ? track->ascii_cells : p->ascii_cells;
    size_t ascii_columns = track != NULL ? track->ascii_columns : p->ascii_columns;
    size_t ascii_rows = track != NULL ? track->ascii_rows : p->ascii_rows;
    float pixel_scale = 1.0f;
    if (p->rendering) {
        (void)render_export_target_scale(
            &p->render_config, (uint32_t)p->screen.texture.width,
            (uint32_t)p->screen.texture.height, &pixel_scale);
    }
    Scene_Renderer renderer = {
        .circle_shader = p->circle,
        .circle_radius_location = p->circle_radius_location,
        .circle_power_location = p->circle_power_location,
        .font = p->font,
        .ascii_cells = ascii_cells,
        .ascii_columns = ascii_columns,
        .ascii_rows = ascii_rows,
        .pixel_scale = pixel_scale,
    };
    scene_instance_update(&p->scene, &frame);
    scene_instance_draw(&p->scene, &frame, &renderer, boundary);
    draw_scene_lyric_overlay(boundary, frame.lyric, renderer.font,
                             renderer.pixel_scale);
}

static void update_scene_shortcuts(void)
{
    Scene_Id selected = COUNT_SCENES;
    if (IsKeyPressed(KEY_ONE)) selected = SCENE_SPECTRUM;
    if (IsKeyPressed(KEY_TWO)) selected = SCENE_PULSE_FIELD;
    if (IsKeyPressed(KEY_THREE)) selected = SCENE_ORBITAL_LATTICE;
    if (IsKeyPressed(KEY_FOUR)) selected = SCENE_ASCII_FIELD;
    if (IsKeyPressed(KEY_FIVE)) selected = SCENE_SONG_ATLAS;
    if (IsKeyPressed(KEY_SIX)) selected = SCENE_SPECTRAL_TERRARIUM;
    if (IsKeyPressed(KEY_SEVEN)) selected = SCENE_CONSTELLATION;
    Track *track = current_track();
    if (selected != COUNT_SCENES &&
        scene_instance_select(&p->scene, selected, scene_seed_for_track(track))) {
        if (track != NULL) {
            track->base_scene = selected;
            track->scene_switches.enabled = false;
            scene_switch_reset(&track->scene_switches);
        }
        mark_project_dirty(track);
    }
}


static void notice_push(Ui_Notice_Severity severity, const char *title,
                        const char *detail, const char *path, bool persistent)
{
    char bounded_title[UI_NOTICE_TITLE_CAPACITY];
    char bounded_detail[UI_NOTICE_DETAIL_CAPACITY];
    char bounded_path[UI_NOTICE_PATH_CAPACITY];
    snprintf(bounded_title, sizeof(bounded_title), "%s", title ? title : "Action failed");
    snprintf(bounded_detail, sizeof(bounded_detail), "%s", detail ? detail : "");
    snprintf(bounded_path, sizeof(bounded_path), "%s", path ? path : "");
    Ui_Notice_Spec spec = {
        .severity = severity,
        .persistent = persistent,
        .duration_seconds = persistent ? 0.0 : 5.0,
        .title = bounded_title,
        .detail = bounded_detail,
        .path = bounded_path,
    };
    Ui_Notice_Result result = ui_notice_push(&p->notices, &spec, NULL);
    if (result != UI_NOTICE_OK && result != UI_NOTICE_DROPPED) {
        TraceLog(LOG_WARNING, "UI: could not queue notice: %s",
                 ui_notice_result_string(result));
    }
}

static inline float signf(float x)
{
    if (x < 0.0) return -1;
    if (x > 0.0) return 1;
    return 0.0;
}

static void snap_segment_inside_other_segment(float ls, float rs, float *lt, float *rt)
{
    float dt = *rt - *lt;
    if (rs < *lt || rs < *rt) {
        *rt = rs;
        *lt = rs - dt;
    }

    if (*lt < ls || *rt < ls) {
        *lt = ls;
        *rt = ls + dt;
    }
}

static void snap_boundary_inside_screen(Rectangle *boundary)
{
    float ls = 0;
    float rs = GetScreenWidth();
    float ts = 0;
    float bs = GetScreenHeight();

    float lt = boundary->x;
    float rt = boundary->x + boundary->width;
    float tt = boundary->y;
    float bt = boundary->y + boundary->height;

    snap_segment_inside_other_segment(ls, rs, &lt, &rt);
    snap_segment_inside_other_segment(ts, bs, &tt, &bt);

    boundary->x = lt;
    boundary->y = tt;
    boundary->width = rt - lt;
    boundary->height = bt - tt;
}

static void align_to_side_of_rect(Rectangle who, Rectangle *what, Side where)
{
    switch (where) {
        case SIDE_BOTTOM: {
            float cx = who.x + who.width/2;
            float cy = who.y + who.height + TOOLTIP_PADDING;
            what->x = cx - what->width/2;
            what->y = cy;
        } break;

        case SIDE_TOP: {
            float cx = who.x + who.width/2;
            float cy = who.y - TOOLTIP_PADDING - what->height;
            what->x = cx - what->width/2;
            what->y = cy;
        } break;

        case SIDE_RIGHT: {
            float cx = who.x + who.width + TOOLTIP_PADDING;
            float cy = who.y + who.height/2;
            what->x = cx;
            what->y = cy - what->height/2;
        } break;

        case SIDE_LEFT: {
            float cx = who.x - TOOLTIP_PADDING - what->width;
            float cy = who.y + who.height/2;
            what->x = cx;
            what->y = cy - what->height/2;
        } break;

        default: {
            assert(0 && "unreachable");
        }
    }
}

static void begin_tooltip_frame(void)
{
    p->tooltip_show = false;
}

static void end_tooltip_frame(void)
{
    if (!p->tooltip_show) return;

    float fontSize = 30;
    float spacing = 0.0;
    Vector2 margin = {20.0, 10.0};
    Vector2 text_size = MeasureTextEx(ui_font(), p->tooltip_buffer, fontSize, spacing);

    Rectangle tooltip_boundary = {
        .width = text_size.x + margin.x*2.0,
        .height = text_size.y + margin.y*2.0,
    };

    align_to_side_of_rect(p->tooltip_element_boundary, &tooltip_boundary, p->tooltip_align);
    snap_boundary_inside_screen(&tooltip_boundary);

    DrawRectangleRec(tooltip_boundary, COLOR_TOOLTIP_BACKGROUND);
    Vector2 position = {
        .x = tooltip_boundary.x + tooltip_boundary.width/2 - text_size.x/2,
        .y = tooltip_boundary.y + tooltip_boundary.height/2 - text_size.y/2,
    };
    DrawTextEx(ui_font(), p->tooltip_buffer, position, fontSize, spacing,
               COLOR_TOOLTIP_FOREGROUND);
}

static void tooltip(Rectangle boundary, const char *text, Side align, bool persists)
{
    if (!(CheckCollisionPointRec(GetMousePosition(), boundary) || persists)) return;
    p->tooltip_show = true;
    // TODO: this may not work properly if text contains UTF-8
    snprintf(p->tooltip_buffer, sizeof(p->tooltip_buffer), "%s", text);
    p->tooltip_align = align;
    p->tooltip_element_boundary = boundary;
}

typedef enum {
    BS_NONE      = 0,
    BS_HOVEROVER = 1,
    BS_CLICKED   = 2,
} Button_State;

static int button_with_id(uint64_t id, Rectangle boundary);
static bool start_assist_job(Assist_Mode mode, Track *track);
static uint32_t assist_mode_lanes(Assist_Mode mode);
static void poll_assist_job(void);
static void cancel_assist_job(void);
static bool apply_assist_candidate(void);
static void discard_assist_candidate(void);
static void start_rendering_track(Track *track);
static bool save_project_as(Track *track);
static bool save_project(Track *track, bool show_success);
static bool open_project_dialog(void);
static void poll_project_autosave(Track *track);
static void track_set_analysis_lane(Track *track, Musi_Analysis_Lane_Kind kind,
                                    const char *path, const char *model);

static int text_button(uint64_t id, Rectangle boundary, const char *label, bool selected)
{
    int state = button_with_id(id, boundary);
    Color background = selected ? COLOR_TRACK_BUTTON_SELECTED : COLOR_TRACK_BUTTON_BACKGROUND;
    if (state & BS_HOVEROVER) {
        background = selected ? ColorBrightness(COLOR_TRACK_BUTTON_SELECTED, 0.12f) :
                                COLOR_TRACK_BUTTON_HOVEROVER;
    }
    DrawRectangleRec(boundary, background);
    DrawRectangleLinesEx(boundary, 1.0f,
                         selected ? COLOR_TRACK_BUTTON_SELECTED : COLOR_UI_RULE);
    float font_size = fminf(boundary.height*0.52f, 22.0f);
    Vector2 size = MeasureTextEx(ui_font(), label, font_size, 0.0f);
    float available_width = boundary.width - 12.0f;
    if (size.x > available_width && size.x > 0.0f) {
        font_size *= available_width/size.x;
        size = MeasureTextEx(ui_font(), label, font_size, 0.0f);
    }
    DrawTextEx(ui_font(), label,
               (Vector2){boundary.x + (boundary.width - size.x)*0.5f,
                         boundary.y + (boundary.height - size.y)*0.5f},
               font_size, 0.0f, selected ? WHITE : COLOR_UI_INK);
    return state;
}

static Color event_type_color(uint32_t type)
{
    switch (type) {
    case EVENT_TYPE_LYRIC: return (Color){236, 89, 190, 255};
    case EVENT_TYPE_SEMANTIC: return (Color){242, 190, 66, 255};
    case EVENT_TYPE_CUE: return (Color){63, 220, 171, 255};
    case EVENT_TYPE_CUSTOM: return (Color){151, 111, 241, 255};
    default: return GRAY;
    }
}

static void lyric_editor_clear_draft(void)
{
    p->selected_lyric_id = 0;
    p->lyric_draft_new = false;
    p->lyric_text_active = false;
    p->lyric_draft_start = 0.0;
    p->lyric_draft_end = 0.0;
    p->lyric_draft_text[0] = '\0';
}

static bool lyric_editor_has_unsaved_draft(const Track *track)
{
    if (track == NULL) return false;
    const Lyric_Cue *cue = lyrics_find(&track->lyrics, p->selected_lyric_id);
    Editor_Lyric_Draft_State state = {
        .is_new = p->lyric_draft_new,
        .selected_id = p->selected_lyric_id,
        .canonical_exists = cue != NULL,
        .canonical_start_seconds = cue != NULL ? cue->start_seconds : 0.0,
        .canonical_end_seconds = cue != NULL ? cue->end_seconds : 0.0,
        .canonical_text = cue != NULL ? cue->text : NULL,
        .draft_start_seconds = p->lyric_draft_start,
        .draft_end_seconds = p->lyric_draft_end,
        .draft_text = p->lyric_draft_text,
    };
    return editor_lyric_draft_is_dirty(&state);
}

static bool lyric_editor_allow_context_change(Track *track)
{
    if (!lyric_editor_has_unsaved_draft(track)) return true;
    notice_push(UI_NOTICE_WARNING, "Finish the lyric edit first",
                "Apply or discard the current draft before changing tracks or panels.",
                NULL, false);
    return false;
}

static void lyric_editor_select(Track *track, uint64_t id)
{
    const Lyric_Cue *cue = lyrics_find(&track->lyrics, id);
    if (cue == NULL) return;
    p->selected_lyric_id = cue->id;
    p->lyric_draft_new = false;
    p->lyric_draft_start = cue->start_seconds;
    p->lyric_draft_end = cue->end_seconds;
    snprintf(p->lyric_draft_text, sizeof(p->lyric_draft_text), "%s", cue->text);
    p->lyric_list_follow_selection = true;
}

static void lyric_editor_begin_new(Track *track)
{
    double duration = track->lyrics.duration_seconds;
    double start = GetMusicTimePlayed(track->music);
    if (start + 0.05 > duration) start = fmax(0.0, duration - 2.0);
    p->selected_lyric_id = 0;
    p->lyric_draft_new = true;
    p->lyric_draft_start = start;
    p->lyric_draft_end = fmin(duration, start + 2.0);
    p->lyric_draft_text[0] = '\0';
    p->lyric_text_active = true;
}

static bool lyric_editor_apply(Track *track)
{
    Lyrics_Result result;
    if (p->lyric_draft_new) {
        Lyric_Cue cue = {
            .start_seconds = p->lyric_draft_start,
            .end_seconds = p->lyric_draft_end,
        };
        snprintf(cue.text, sizeof(cue.text), "%s", p->lyric_draft_text);
        uint64_t id = 0;
        result = lyrics_insert(&track->lyrics, &cue, &id);
        if (result == LYRICS_OK) lyric_editor_select(track, id);
    } else {
        result = lyrics_update(&track->lyrics, p->selected_lyric_id,
                                p->lyric_draft_start, p->lyric_draft_end,
                                p->lyric_draft_text);
        if (result == LYRICS_OK) {
            lyric_editor_select(track, p->selected_lyric_id);
        }
    }
    if (result != LYRICS_OK) {
        TraceLog(LOG_WARNING, "LYRICS: could not apply edit: %s",
                 lyrics_result_string(result));
        notice_push(UI_NOTICE_ERROR, "Lyric edit was not applied",
                    lyrics_result_string(result), NULL, true);
        return false;
    }
    p->lyric_text_active = false;
    mark_project_dirty(track);
    return true;
}

static void lyric_text_backspace(char *text)
{
    size_t length = strlen(text);
    if (length == 0) return;
    length -= 1;
    while (length > 0 && (((unsigned char)text[length] & 0xC0u) == 0x80u)) {
        length -= 1;
    }
    text[length] = '\0';
}

static void lyric_text_input_update(void)
{
    if (!p->lyric_text_active) return;
    if (IsKeyPressed(KEY_BACKSPACE)) lyric_text_backspace(p->lyric_draft_text);
    if (IsKeyPressed(KEY_ESCAPE)) p->lyric_text_active = false;
    for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
        if (codepoint < 0x20 || codepoint == 0x7F) continue;
        int encoded_size = 0;
        const char *encoded = CodepointToUTF8(codepoint, &encoded_size);
        size_t length = strlen(p->lyric_draft_text);
        if (encoded != NULL && encoded_size > 0 &&
            length + (size_t)encoded_size < sizeof(p->lyric_draft_text)) {
            memcpy(p->lyric_draft_text + length, encoded, (size_t)encoded_size);
            p->lyric_draft_text[length + (size_t)encoded_size] = '\0';
        }
    }
}

static void format_timestamp(double seconds, char *output, size_t capacity)
{
    if (seconds < 0.0) seconds = 0.0;
    unsigned minutes = (unsigned)(seconds/60.0);
    double within_minute = seconds - (double)minutes*60.0;
    snprintf(output, capacity, "%02u:%06.3f", minutes, within_minute);
}

static void lyric_time_row(Rectangle boundary, const char *label, double *value,
                           double other, bool is_start, double playhead,
                           double duration, uint64_t id_base)
{
    const float gap = 4.0f;
    DrawTextEx(ui_font(), label, (Vector2){boundary.x, boundary.y + 8.0f},
               16.0f, 1.0f, COLOR_UI_MUTED);
    char timestamp[32];
    format_timestamp(*value, timestamp, sizeof(timestamp));
    DrawTextEx(ui_font(), timestamp, (Vector2){boundary.x + 58.0f, boundary.y + 7.0f},
               18.0f, 1.0f, COLOR_UI_INK);
    Rectangle minus = {boundary.x + 154.0f, boundary.y, 42.0f, boundary.height};
    Rectangle plus = {minus.x + minus.width + gap, boundary.y, 42.0f, boundary.height};
    Rectangle set = {plus.x + plus.width + gap, boundary.y, 78.0f, boundary.height};
    if (text_button(id_base, minus, "-0.1", false) & BS_CLICKED) *value -= 0.1;
    if (text_button(id_base + 1, plus, "+0.1", false) & BS_CLICKED) *value += 0.1;
    if (text_button(id_base + 2, set, "Set here", false) & BS_CLICKED) *value = playhead;
    if (is_start) {
        if (*value < 0.0) *value = 0.0;
        if (*value > other - 0.001) *value = other - 0.001;
    } else {
        if (*value < other + 0.001) *value = other + 0.001;
        if (*value > duration) *value = duration;
    }
}

static void record_timeline_event(Track *track, uint32_t type)
{
    if (track == NULL || track->next_manual_event_id == UINT64_MAX) {
        notice_push(UI_NOTICE_ERROR, "Manual event was not added",
                    "The event ID space is exhausted.", NULL, true);
        return;
    }
    Event_Record event = {
        .timestamp_seconds = GetMusicTimePlayed(track->music),
        .id = track->next_manual_event_id,
        .type = type,
        .value_count = 1,
        .values = {1.0f},
    };
    if (!plug_record_event(event)) {
        notice_push(UI_NOTICE_ERROR, "Manual event was not added",
                    "The event is invalid or the 1024-event lane is full.", NULL, true);
        return;
    }
    if (p->scene.id != SCENE_CONSTELLATION) {
        scene_instance_select(&p->scene, SCENE_CONSTELLATION, track->scene_seed);
        track->base_scene = SCENE_CONSTELLATION;
    }
}

static void draw_lyric_lane(Rectangle lane, Track *track, float track_length)
{
    DrawRectangleRec(lane, COLOR_UI_RAISED);
    DrawLineEx((Vector2){lane.x, lane.y}, (Vector2){lane.x + lane.width, lane.y},
               1.0f, COLOR_UI_RULE);
    const Color lyric_color = (Color){242, 190, 66, 255};
    for (size_t i = 0; i < track->scene_switches.count; ++i) {
        const Scene_Switch_Cue *cue = &track->scene_switches.cues[i];
        float x = lane.x + (float)(cue->start_seconds/track_length)*lane.width;
        DrawLineEx((Vector2){x, lane.y}, (Vector2){x, lane.y + lane.height},
                   1.0f + cue->strength*2.0f, ColorAlpha((Color){0, 230, 118, 255}, 0.58f));
        if (lane.height >= 28.0f && i + 1 < track->scene_switches.count) {
            DrawTextEx(ui_font(), scene_stable_name((Scene_Id)cue->scene_index),
                       (Vector2){x + 3.0f, lane.y + 7.0f}, 12.0f, 1.0f,
                       COLOR_UI_MUTED);
        }
    }
    for (size_t i = 0; i < track->lyrics.count; ++i) {
        const Lyric_Cue *cue = &track->lyrics.cues[i];
        float left = lane.x + (float)(cue->start_seconds/track_length)*lane.width;
        float right = lane.x + (float)(cue->end_seconds/track_length)*lane.width;
        if (right - left < 3.0f) right = left + 3.0f;
        Rectangle block = {left, lane.y + 3.0f, right - left, lane.height - 6.0f};
        bool selected = cue->id == p->selected_lyric_id;
        Color fill = ColorAlpha(lyric_color, selected ? 0.82f : 0.38f);
        if (CheckCollisionPointRec(GetMousePosition(), block)) fill = ColorAlpha(lyric_color, 0.68f);
        DrawRectangleRec(block, fill);
        DrawRectangleLinesEx(block, 1.0f, lyric_color);
        if (CheckCollisionPointRec(GetMousePosition(), block) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (cue->id == p->selected_lyric_id || lyric_editor_allow_context_change(track)) {
                lyric_editor_select(track, cue->id);
                p->lyrics_editor_open = true;
            }
        }
    }
}

static bool export_lyrics_document(const Lyrics_Document *document)
{
    const char *filters[] = {"*.lyrics.tsv"};
    char *path = tinyfd_saveFileDialog("Export timed lyrics", "lyrics.lyrics.tsv",
                                       NOB_ARRAY_LEN(filters), filters,
                                       "Musializer timed lyrics");
    if (path == NULL) return true;
    size_t required = 0;
    Lyrics_Result measured = lyrics_bridge_export(document, NULL, 0, &required);
    if (measured != LYRICS_ERROR_BUFFER_TOO_SMALL || required == 0 || required > INT_MAX) {
        return false;
    }
    char *output = malloc(required);
    if (output == NULL) return false;
    Lyrics_Result exported = lyrics_bridge_export(document, output, required, &required);
    bool saved = exported == LYRICS_OK &&
                 SaveFileData(path, output, (int)(required - 1));
    free(output);
    return saved;
}

// 1 imported, 0 cancelled, -1 failed.
static int import_lyrics_document(Lyrics_Document *document)
{
    const char *filters[] = {"*.lyrics.tsv", "*.tsv"};
    char *path = tinyfd_openFileDialog("Import timed lyrics", "./",
                                       NOB_ARRAY_LEN(filters), filters,
                                       "Musializer timed lyrics", 0);
    if (path == NULL) return 0;
    int file_size = GetFileLength(path);
    if (file_size <= 0 || (size_t)file_size > LYRICS_BRIDGE_MAX_BYTES) return -1;
    int input_size = 0;
    unsigned char *input = LoadFileData(path, &input_size);
    if (input == NULL || input_size <= 0) {
        if (input != NULL) UnloadFileData(input);
        return -1;
    }
    Lyrics_Document *candidate = malloc(sizeof(*candidate));
    if (candidate == NULL) {
        UnloadFileData(input);
        return -1;
    }
    lyrics_document_init(candidate, document->duration_seconds);
    Lyrics_Result imported = lyrics_bridge_import(
        candidate, (const char *)input, (size_t)input_size);
    UnloadFileData(input);
    bool matches = imported == LYRICS_OK &&
                   fabs(candidate->duration_seconds - document->duration_seconds) <= 0.25;
    if (matches) {
        matches = lyrics_document_normalize_duration(
            candidate, candidate, document->duration_seconds) == LYRICS_OK;
    }
    bool replaced = matches &&
                    lyrics_document_replace(document, candidate) == LYRICS_OK;
    free(candidate);
    return replaced ? 1 : -1;
}

static void draw_lyrics_editor(Rectangle boundary, Track *track, double playhead)
{
    const Color signal = COLOR_ACCENT;
    const float padding = 8.0f;
    const float gap = 8.0f;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);

    Rectangle list = {
        boundary.x + padding, boundary.y + padding,
        boundary.width*0.48f - padding - gap*0.5f,
        boundary.height - padding*2.0f,
    };
    Rectangle form = {
        list.x + list.width + gap, list.y,
        boundary.x + boundary.width - padding - (list.x + list.width + gap),
        list.height,
    };

    DrawTextEx(ui_font(), "LYRIC CUES", (Vector2){list.x, list.y},
               18.0f, 1.0f, signal);
    char cue_count[48];
    snprintf(cue_count, sizeof(cue_count), "%zu / %u", track->lyrics.count,
             (unsigned)LYRICS_CUE_CAPACITY);
    Vector2 count_size = MeasureTextEx(ui_font(), cue_count, 15.0f, 1.0f);
    DrawTextEx(ui_font(), cue_count,
               (Vector2){list.x + list.width - count_size.x, list.y + 2.0f},
               15.0f, 1.0f, COLOR_UI_MUTED);

    const float row_height = 36.0f;
    size_t visible = list.height > 36.0f ? (size_t)((list.height - 32.0f)/row_height) : 0;
    size_t focus = 0;
    for (size_t i = 0; i < track->lyrics.count; ++i) {
        const Lyric_Cue *cue = &track->lyrics.cues[i];
        if (cue->id == p->selected_lyric_id ||
            (p->selected_lyric_id == 0 && cue->start_seconds <= playhead && playhead < cue->end_seconds)) {
            focus = i;
            break;
        }
    }
    size_t first = p->lyric_list_first;
    if (p->lyric_list_follow_selection) {
        first = focus > visible/2 ? focus - visible/2 : 0;
    }
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            p->lyric_list_follow_selection = false;
            int64_t moved = (int64_t)first - (int64_t)(wheel*3.0f);
            if (moved < 0) moved = 0;
            first = (size_t)moved;
        }
    }
    if (first + visible > track->lyrics.count) {
        first = track->lyrics.count > visible ? track->lyrics.count - visible : 0;
    }
    p->lyric_list_first = first;
    if (track->lyrics.count == 0) {
        DrawTextEx(ui_font(), "No lyric cues. Add one at the playhead.",
                   (Vector2){list.x, list.y + 38.0f}, 16.0f, 1.0f,
                   COLOR_UI_MUTED);
    }
    for (size_t row = 0; row < visible && first + row < track->lyrics.count; ++row) {
        const Lyric_Cue *cue = &track->lyrics.cues[first + row];
        Rectangle row_boundary = {
            list.x, list.y + 28.0f + row*row_height, list.width, row_height - 2.0f,
        };
        bool selected = cue->id == p->selected_lyric_id;
        bool current = cue->start_seconds <= playhead && playhead < cue->end_seconds;
        int state = button_with_id(UINT64_C(0x4C59524943000000) + cue->id, row_boundary);
        Color background = selected ? GetColor(0xE7ECFAFF) : COLOR_UI_RAISED;
        if (state & BS_HOVEROVER) background = COLOR_TRACK_BUTTON_HOVEROVER;
        DrawRectangleRec(row_boundary, background);
        DrawRectangleLinesEx(row_boundary, 1.0f, COLOR_UI_RULE);
        if (current) DrawRectangle((int)row_boundary.x, (int)row_boundary.y, 3,
                                   (int)row_boundary.height, signal);
        char time[24];
        format_timestamp(cue->start_seconds, time, sizeof(time));
        DrawTextEx(ui_font(), time,
                   (Vector2){row_boundary.x + 8.0f, row_boundary.y + 5.0f},
                   15.0f, 1.0f, COLOR_UI_MUTED);
        BeginScissorMode((int)(row_boundary.x + 90.0f), (int)row_boundary.y,
                         (int)(row_boundary.width - 94.0f), (int)row_boundary.height);
        DrawTextEx(ui_font(), cue->text,
                   (Vector2){row_boundary.x + 94.0f, row_boundary.y + 5.0f},
                   15.0f, 1.0f, COLOR_UI_INK);
        EndScissorMode();
        if (state & BS_CLICKED) {
            if (cue->id == p->selected_lyric_id || lyric_editor_allow_context_change(track)) {
                lyric_editor_select(track, cue->id);
            }
        }
    }
    if (track->lyrics.count > visible && visible > 0) {
        float track_height = fmaxf(24.0f, (list.height - 32.0f)*
                                   (float)visible/(float)track->lyrics.count);
        float travel = list.height - 32.0f - track_height;
        float amount = (float)first/(float)(track->lyrics.count - visible);
        Rectangle scrollbar = {
            list.x + list.width - 3.0f, list.y + 28.0f + travel*amount,
            3.0f, track_height,
        };
        DrawRectangleRec(scrollbar, COLOR_ACCENT);
    }

    DrawTextEx(ui_font(), p->lyric_draft_new ? "NEW CUE" : "SELECTED CUE",
               (Vector2){form.x, form.y}, 18.0f, 1.0f, signal);
    Rectangle add = {form.x + form.width - 92.0f, form.y - 3.0f, 92.0f, 34.0f};
    Rectangle import_button = {add.x - 83.0f, add.y, 77.0f, add.height};
    Rectangle export_button = {import_button.x - 83.0f, add.y, 77.0f, add.height};
    if (text_button(UINT64_C(0x4C59524943494D50), import_button,
                    "Import", false) & BS_CLICKED) {
        int imported = lyric_editor_allow_context_change(track) ?
                       import_lyrics_document(&track->lyrics) : 0;
        if (imported < 0) {
            notice_push(UI_NOTICE_ERROR, "Lyrics were not imported",
                        "The selected file is invalid, too large, or could not be read.",
                        NULL, true);
        }
        if (imported > 0) {
            lyric_editor_clear_draft();
            mark_project_dirty(track);
        }
    }
    if (text_button(UINT64_C(0x4C59524943455850), export_button,
                    "Export", false) & BS_CLICKED) {
        if (!export_lyrics_document(&track->lyrics)) {
            notice_push(UI_NOTICE_ERROR, "Lyrics were not exported",
                        "Choose a writable destination and try again.", NULL, true);
        }
    }
    if (text_button(UINT64_C(0x4C59524943414444), add, "Add cue", false) & BS_CLICKED) {
        if (lyric_editor_allow_context_change(track)) lyric_editor_begin_new(track);
    }

    bool has_draft = p->lyric_draft_new || p->selected_lyric_id != 0;
    if (!has_draft) {
        DrawTextEx(ui_font(), "Select a cue or add one at the current playhead.",
                   (Vector2){form.x, form.y + 42.0f}, 16.0f, 1.0f,
                   COLOR_UI_MUTED);
        return;
    }

    Rectangle start_row = {form.x, form.y + 30.0f, form.width, 30.0f};
    Rectangle end_row = {form.x, form.y + 64.0f, form.width, 30.0f};
    lyric_time_row(start_row, "START", &p->lyric_draft_start, p->lyric_draft_end,
                   true, playhead, track->lyrics.duration_seconds,
                   UINT64_C(0x4C59525300000000));
    lyric_time_row(end_row, "END", &p->lyric_draft_end, p->lyric_draft_start,
                   false, playhead, track->lyrics.duration_seconds,
                   UINT64_C(0x4C59524500000000));

    Rectangle text_field = {form.x, form.y + 101.0f, form.width, 37.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        p->lyric_text_active = CheckCollisionPointRec(GetMousePosition(), text_field);
    }
    DrawRectangleRec(text_field, COLOR_UI_RAISED);
    DrawRectangleLinesEx(text_field, p->lyric_text_active ? 2.0f : 1.0f,
                         p->lyric_text_active ? signal : COLOR_UI_RULE);
    const char *display_text = p->lyric_draft_text[0] != '\0' ?
                               p->lyric_draft_text : "Type lyric content";
    Color text_color = p->lyric_draft_text[0] != '\0' ? COLOR_UI_INK : COLOR_UI_MUTED;
    BeginScissorMode((int)text_field.x + 7, (int)text_field.y,
                     (int)text_field.width - 14, (int)text_field.height);
    DrawTextEx(ui_font(), display_text,
               (Vector2){text_field.x + 8.0f, text_field.y + 9.0f},
               17.0f, 1.0f, text_color);
    if (p->lyric_text_active && ((int)(GetTime()*2.0) & 1) == 0) {
        Vector2 measured = MeasureTextEx(ui_font(), p->lyric_draft_text, 17.0f, 1.0f);
        DrawLineEx((Vector2){text_field.x + 9.0f + measured.x, text_field.y + 8.0f},
                   (Vector2){text_field.x + 9.0f + measured.x, text_field.y + 29.0f},
                   1.0f, signal);
    }
    EndScissorMode();
    lyric_text_input_update();

    Rectangle apply = {form.x, form.y + 146.0f, 92.0f, 36.0f};
    if (text_button(UINT64_C(0x4C59524943415050), apply, "Apply", false) & BS_CLICKED) {
        lyric_editor_apply(track);
    }
    Rectangle discard_button = {apply.x + apply.width + gap, apply.y, 104.0f, apply.height};
    if (text_button(UINT64_C(0x4C59524943444953), discard_button,
                    p->lyric_draft_new ? "Cancel edit" : "Discard edit", false) &
        BS_CLICKED) {
        lyric_editor_clear_draft();
    }
    Rectangle delete_button = {
        discard_button.x + discard_button.width + gap, apply.y, 92.0f, apply.height,
    };
    if (!p->lyric_draft_new &&
        (text_button(UINT64_C(0x4C5952494344454C), delete_button, "Delete", false) & BS_CLICKED)) {
        if (lyrics_delete(&track->lyrics, p->selected_lyric_id) == LYRICS_OK) {
            lyric_editor_clear_draft();
            mark_project_dirty(track);
        }
    }
    DrawTextEx(ui_font(), "Ctrl+Enter applies the edit",
               (Vector2){form.x, apply.y + apply.height + 7.0f}, 14.0f, 1.0f,
               COLOR_UI_MUTED);
    if (p->lyric_text_active &&
        (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_ENTER)) {
        lyric_editor_apply(track);
    }
}

static const char *assist_mode_display_name(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS: return "Timed lyrics";
    case ASSIST_MODE_SECTIONS: return "Scene changes";
    case ASSIST_MODE_MIMO: return "MiMo feelings";
    case ASSIST_MODE_ALL: return "Full assist";
    }
    return "Analysis";
}

static bool assist_mode_needs_confirmation(Assist_Mode mode)
{
    return mode == ASSIST_MODE_LYRICS || mode == ASSIST_MODE_MIMO ||
           mode == ASSIST_MODE_ALL;
}

static bool find_assist_helper(char *path, size_t capacity)
{
    if (path == NULL || capacity == 0) return false;
    const char *override = getenv("MUSIALIZER_ASSIST_HELPER");
    if (override != NULL && override[0] != '\0') {
        int length = snprintf(path, capacity, "%s", override);
        return length > 0 && (size_t)length < capacity && FileExists(path);
    }
    const char *application = GetApplicationDirectory();
    const char *patterns[] = {
        "%s/tools/external_analysis.py",    // extracted distribution
        "%s/../tools/external_analysis.py", // source build in ./build
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(patterns); ++i) {
        int length = snprintf(path, capacity, patterns[i], application);
        if (length > 0 && (size_t)length < capacity && FileExists(path)) return true;
    }
    int length = snprintf(path, capacity, "./tools/external_analysis.py");
    return length > 0 && (size_t)length < capacity && FileExists(path);
}

static void draw_assist_panel(Rectangle boundary, Track *track)
{
    const Color signal = COLOR_ACCENT;
    const float padding = 10.0f;
    const float gap = 8.0f;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);
    DrawTextEx(ui_font(), "ASSISTED ANALYSIS",
               (Vector2){boundary.x + padding, boundary.y + padding},
               19.0f, 1.0f, signal);
    DrawTextEx(ui_font(),
               "Every result is validated and staged. Nothing replaces editor content until you apply it.",
               (Vector2){boundary.x + padding, boundary.y + 36.0f},
               15.0f, 1.0f, COLOR_UI_MUTED);

    const char *labels[] = {"Timed lyrics", "Scene changes", "MiMo feelings", "Full assist"};
    const char *badges[] = {"WHISPER + CODEX", "LOCAL", "OPENROUTER AUDIO", "LOCAL + REMOTE"};
    const Assist_Mode modes[] = {
        ASSIST_MODE_LYRICS, ASSIST_MODE_SECTIONS, ASSIST_MODE_MIMO, ASSIST_MODE_ALL,
    };
    float button_width = (boundary.width - padding*2.0f - gap*3.0f)/4.0f;
    char helper_path[PLUG_RELOAD_PATH_CAPACITY];
    bool helpers_available = find_assist_helper(helper_path, sizeof(helper_path));
    bool busy = p->assist_job_state == ASSIST_JOB_RUNNING;
    for (size_t i = 0; i < NOB_ARRAY_LEN(labels); ++i) {
        Rectangle button_boundary = {
            boundary.x + padding + i*(button_width + gap),
            boundary.y + 60.0f,
            button_width,
            39.0f,
        };
        bool selected = (busy && p->assist_mode == modes[i]) ||
                        (p->assist_confirmation_pending && p->assist_mode == modes[i]);
        int state = text_button(UINT64_C(0x4153534953540000) + i,
                                button_boundary, labels[i], selected);
        DrawTextEx(ui_font(), badges[i],
                   (Vector2){button_boundary.x + 3.0f, button_boundary.y + 42.0f},
                   11.0f, 1.0f, busy ? ColorAlpha(COLOR_UI_MUTED, 0.55f) : COLOR_UI_MUTED);
        if ((state & BS_CLICKED) && helpers_available && !busy) {
            if (p->assist_candidate != NULL) {
                notice_push(UI_NOTICE_INFO, "Review pending suggestions first",
                            "Apply or discard the staged result before starting another job.",
                            NULL, false);
            } else if (assist_mode_needs_confirmation(modes[i])) {
                p->assist_mode = modes[i];
                p->assist_confirmation_pending = true;
            } else if (!start_assist_job(modes[i], track)) {
                notice_push(UI_NOTICE_ERROR, "Analysis could not start",
                            "Check the helper installation and the application log.",
                            p->assist_log_path, true);
            }
        }
    }

    float status_y = boundary.y + 120.0f;
    char status[256];
    if (!helpers_available) {
        snprintf(status, sizeof(status), "Assist helper is unavailable in this installation.");
    } else if (busy) {
        double elapsed = fmax(0.0, GetTime() - p->assist_started_at);
        const char *track_name = p->assist_track_index < p->tracks.count ?
                                 GetFileName(p->tracks.items[p->assist_track_index].file_path) :
                                 "unknown track";
        snprintf(status, sizeof(status), "%s is running for %s  |  %02u:%02u elapsed",
                 assist_mode_display_name(p->assist_mode), track_name,
                 (unsigned)(elapsed/60.0), (unsigned)fmod(elapsed, 60.0));
    } else if (p->assist_confirmation_pending) {
        snprintf(status, sizeof(status), "%s needs your confirmation",
                 assist_mode_display_name(p->assist_mode));
    } else if (p->assist_candidate != NULL) {
        const char *track_name = p->assist_candidate_track_index < p->tracks.count ?
                                 GetFileName(p->tracks.items[p->assist_candidate_track_index].file_path) :
                                 "unknown track";
        snprintf(status, sizeof(status), "Validated suggestions are ready for %s", track_name);
    } else if (p->assist_job_state == ASSIST_JOB_CANCELLED) {
        snprintf(status, sizeof(status), "Cancelled. Previous editor content is unchanged.");
    } else if (p->assist_job_state == ASSIST_JOB_FAILED) {
        snprintf(status, sizeof(status), "Analysis failed. Previous editor content is unchanged.");
    } else {
        snprintf(status, sizeof(status), "Helper found. Run the product doctor for capability preflight.");
    }
    DrawTextEx(ui_font(), status, (Vector2){boundary.x + padding, status_y},
               16.0f, 1.0f, busy ? signal : COLOR_UI_INK);

    float action_y = status_y + 28.0f;
    if (p->assist_confirmation_pending && !busy) {
        const char *privacy = "";
        const char *start_label = "Start";
        if (p->assist_mode == ASSIST_MODE_LYRICS) {
            privacy = "Whisper runs locally. Transcript evidence is sent to headless Codex; audio is not.";
            start_label = "Run Whisper + Codex";
        } else if (p->assist_mode == ASSIST_MODE_MIMO) {
            privacy = "The track audio is sent to OpenRouter for MiMo. Zero Data Retention routing is requested.";
            start_label = "Send audio + run MiMo";
        } else {
            privacy = "Runs local analysis and sends transcript evidence to Codex and track audio to OpenRouter MiMo.";
            start_label = "Start full assist";
        }
        DrawTextEx(ui_font(), privacy, (Vector2){boundary.x + padding, action_y},
                   14.0f, 1.0f, COLOR_UI_MUTED);
        Rectangle start = {boundary.x + padding, action_y + 24.0f, 190.0f, 36.0f};
        Rectangle cancel = {start.x + start.width + gap, start.y, 94.0f, start.height};
        if (text_button(UINT64_C(0x4153534953544346), start, start_label, false) & BS_CLICKED) {
            p->assist_confirmation_pending = false;
            if (!start_assist_job(p->assist_mode, track)) {
                notice_push(UI_NOTICE_ERROR, "Analysis could not start",
                            "Check the helper installation and required credentials.",
                            p->assist_log_path, true);
            }
        }
        if (text_button(UINT64_C(0x4153534953544343), cancel, "Cancel", false) & BS_CLICKED) {
            p->assist_confirmation_pending = false;
        }
    } else if (busy) {
        Rectangle cancel = {boundary.x + padding, action_y, 130.0f, 36.0f};
        if (text_button(UINT64_C(0x4153534953545354), cancel, "Cancel job", false) & BS_CLICKED) {
            cancel_assist_job();
        }
        DrawTextEx(ui_font(), "Playback remains available while the helper runs.",
                   (Vector2){cancel.x + cancel.width + gap, cancel.y + 9.0f},
                   14.0f, 1.0f, COLOR_UI_MUTED);
    } else if (p->assist_candidate != NULL) {
        Analysis_Candidate *candidate = p->assist_candidate;
        Track *candidate_track = p->assist_candidate_track_index < p->tracks.count ?
                                 &p->tracks.items[p->assist_candidate_track_index] : NULL;
        char summary[256];
        snprintf(summary, sizeof(summary),
                 "%zu lyrics (%zu uncertain)  |  %zu scene sections  |  %zu feeling cues",
                 candidate->lyrics.count, candidate->uncertain_lyric_count,
                 candidate->sections.count, candidate->semantic_events.count);
        DrawTextEx(ui_font(), summary, (Vector2){boundary.x + padding, action_y},
                   14.0f, 1.0f, COLOR_UI_MUTED);
        char replacement[320];
        snprintf(replacement, sizeof(replacement),
                 "Apply will replace: lyrics %zu -> %zu  |  sections %zu -> %zu  |  feelings %zu -> %zu",
                 candidate_track != NULL ? candidate_track->lyrics.count : 0,
                 (candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0 ?
                    candidate->lyrics.count :
                    (candidate_track != NULL ? candidate_track->lyrics.count : 0),
                 candidate_track != NULL ? candidate_track->scene_switches.count : 0,
                 (candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0 ?
                    candidate->sections.count :
                    (candidate_track != NULL ? candidate_track->scene_switches.count : 0),
                 candidate_track != NULL ? candidate_track->semantic_events.count : 0,
                 (candidate->available_lanes & ANALYSIS_CANDIDATE_SEMANTICS) != 0 ?
                    candidate->semantic_events.count :
                    (candidate_track != NULL ? candidate_track->semantic_events.count : 0));
        DrawTextEx(ui_font(), replacement,
                   (Vector2){boundary.x + padding, action_y + 21.0f},
                   13.0f, 1.0f,
                   p->assist_apply_confirmation_pending ? COLOR_ACCENT : COLOR_UI_MUTED);
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0 &&
            candidate->lyrics.count > 0) {
            char first_lyric[180];
            snprintf(first_lyric, sizeof(first_lyric), "First staged lyric: %.140s",
                     candidate->lyrics.cues[0].text);
            DrawTextEx(ui_font(), first_lyric,
                       (Vector2){boundary.x + padding, action_y + 41.0f},
                       13.0f, 1.0f, COLOR_UI_INK);
        }
        Rectangle apply = {boundary.x + padding, action_y + 62.0f, 176.0f, 36.0f};
        Rectangle discard = {apply.x + apply.width + gap, apply.y, 100.0f, apply.height};
        if (text_button(UINT64_C(0x4153534953544150), apply,
                        p->assist_apply_confirmation_pending ?
                            "Confirm replacement" : "Review + apply",
                        p->assist_apply_confirmation_pending) & BS_CLICKED) {
            if (p->assist_apply_confirmation_pending) {
                (void)apply_assist_candidate();
            } else {
                p->assist_apply_confirmation_pending = true;
                notice_push(UI_NOTICE_WARNING, "Confirm lane replacement",
                            replacement, NULL, false);
            }
        }
        if (text_button(UINT64_C(0x4153534953544449), discard,
                        "Discard", false) & BS_CLICKED) {
            discard_assist_candidate();
        }
    }

    if (track->scene_switches.count > 0) {
        char auto_label[96];
        snprintf(auto_label, sizeof(auto_label), "Auto scenes: %s (%zu)",
                 track->scene_switches.enabled ? "On" : "Off",
                 track->scene_switches.count);
        Rectangle toggle = {
            boundary.x + boundary.width - padding - 190.0f,
            boundary.y + boundary.height - 38.0f,
            190.0f,
            36.0f,
        };
        if (text_button(UINT64_C(0x4155544F5343454E), toggle, auto_label,
                        track->scene_switches.enabled) & BS_CLICKED) {
            track->scene_switches.enabled = !track->scene_switches.enabled;
            scene_switch_reset(&track->scene_switches);
            if (!track->scene_switches.enabled) {
                (void)scene_instance_select(&p->scene, track->base_scene,
                                            track->scene_seed);
            }
            mark_project_dirty(track);
        }
    }
}

static void draw_export_panel(Rectangle boundary, Track *track)
{
    const float padding = 10.0f;
    const float gap = 8.0f;
    const Color signal = COLOR_ACCENT;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);
    DrawTextEx(ui_font(), "EXPORT",
               (Vector2){boundary.x + padding, boundary.y + padding},
               19.0f, 1.0f, signal);
    DrawTextEx(ui_font(),
               "One deterministic scene path. The destination is replaced only after the encoder succeeds.",
               (Vector2){boundary.x + padding, boundary.y + 36.0f},
               15.0f, 1.0f, COLOR_UI_MUTED);

    float y = boundary.y + 62.0f;
    DrawTextEx(ui_font(), "SIZE", (Vector2){boundary.x + padding, y + 10.0f},
               13.0f, 1.0f, COLOR_UI_MUTED);
    float x = boundary.x + 68.0f;
    const float size_width = 76.0f;
    for (int i = 0; i < RENDER_RESOLUTION_COUNT; ++i) {
        Rectangle button = {x, y, size_width, 36.0f};
        if (text_button(UINT64_C(0x4558504F52545300) + (uint64_t)i, button,
                        render_export_resolution_name((Render_Resolution)i),
                        p->render_resolution == (Render_Resolution)i) & BS_CLICKED) {
            p->render_resolution = (Render_Resolution)i;
            (void)render_export_config_set_resolution(&p->render_config,
                                                       p->render_resolution);
            track->render_config = p->render_config;
            mark_project_dirty(track);
        }
        x += size_width + gap;
    }

    DrawTextEx(ui_font(), "FPS", (Vector2){x + 8.0f, y + 10.0f},
               13.0f, 1.0f, COLOR_UI_MUTED);
    x += 48.0f;
    for (int i = 0; i < RENDER_FRAME_RATE_COUNT; ++i) {
        Rectangle button = {x, y, 72.0f, 36.0f};
        if (text_button(UINT64_C(0x4558504F52544600) + (uint64_t)i, button,
                        render_export_frame_rate_name((Render_Frame_Rate)i),
                        p->render_frame_rate == (Render_Frame_Rate)i) & BS_CLICKED) {
            p->render_frame_rate = (Render_Frame_Rate)i;
            (void)render_export_config_set_frame_rate(&p->render_config,
                                                       p->render_frame_rate);
            track->render_config = p->render_config;
            mark_project_dirty(track);
        }
        x += 72.0f + gap;
    }

    y += 46.0f;
    DrawTextEx(ui_font(), "QUALITY", (Vector2){boundary.x + padding, y + 10.0f},
               13.0f, 1.0f, COLOR_UI_MUTED);
    x = boundary.x + 86.0f;
    for (int i = 0; i < RENDER_QUALITY_COUNT; ++i) {
        Rectangle button = {x, y, 106.0f, 36.0f};
        if (text_button(UINT64_C(0x4558504F52545100) + (uint64_t)i, button,
                        render_export_quality_name((Render_Quality)i),
                        p->render_config.quality == (Render_Quality)i) & BS_CLICKED) {
            (void)render_export_config_set_quality(&p->render_config,
                                                    (Render_Quality)i);
            track->render_config = p->render_config;
            mark_project_dirty(track);
        }
        x += 106.0f + gap;
    }

    double duration = GetMusicTimeLength(track->music);
    uint64_t approximate_frames = (uint64_t)ceil(duration*p->render_config.fps);
    char summary[320];
    snprintf(summary, sizeof(summary),
             "%s  |  %ux%u at %u fps  |  %s  |  est. %llu frames  |  %s",
             GetFileName(track->file_path), p->render_config.width,
             p->render_config.height, p->render_config.fps,
             render_export_quality_name(p->render_config.quality),
             (unsigned long long)approximate_frames,
             track->scene_switches.enabled ? "automatic scene plan" :
                                             scene_name(track->base_scene));
    DrawTextEx(ui_font(), summary,
               (Vector2){boundary.x + padding, y + 50.0f},
               15.0f, 1.0f, COLOR_UI_INK);
    const char *quality_detail = p->render_config.quality == RENDER_QUALITY_BALANCED ?
        "Balanced uses native resolution and CRF 20." :
        p->render_config.quality == RENDER_QUALITY_HIGH ?
        "High uses 2x spatial supersampling and CRF 16." :
        "Master uses 2x spatial supersampling and CRF 12.";
    DrawTextEx(ui_font(), quality_detail,
               (Vector2){boundary.x + padding, y + 73.0f},
               14.0f, 1.0f, COLOR_UI_MUTED);

    Rectangle render = {
        boundary.x + boundary.width - padding - 212.0f,
        boundary.y + boundary.height - 44.0f, 212.0f, 36.0f,
    };
    Rectangle close = {render.x - 98.0f - gap, render.y, 98.0f, render.height};
    if (text_button(UINT64_C(0x4558504F5254474F), render,
                    "Choose output and render", false) & BS_CLICKED) {
        start_rendering_track(track);
    }
    if (text_button(UINT64_C(0x4558504F5254434C), close, "Close", false) & BS_CLICKED) {
        p->export_panel_open = false;
    }
}

static void seek_track_to(Track *track, double seconds)
{
    if (track == NULL) return;
    double duration = track->duration_seconds;
    double target = track_timeline_seek_relative(0.0, seconds, duration);
    SeekMusicStream(track->music, (float)target);
    p->scene_clock_initialized = false;
    scene_switch_reset(&track->scene_switches);
}

static void seek_track_by(Track *track, double delta_seconds)
{
    if (track == NULL) return;
    double target = track_timeline_seek_relative(
        GetMusicTimePlayed(track->music), delta_seconds,
        track->duration_seconds);
    seek_track_to(track, target);
}

static void draw_track_waveform(Rectangle boundary, const Track *track)
{
    DrawRectangleRec(boundary, COLOR_UI_RAISED);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);
    float center = boundary.y + boundary.height*0.5f;
    DrawLineEx((Vector2){boundary.x, center},
               (Vector2){boundary.x + boundary.width, center},
               1.0f, ColorAlpha(COLOR_UI_MUTED, 0.28f));

    size_t bin_count = track->timeline_waveform.count;
    if (bin_count == 0 || boundary.width < 1.0f || boundary.height < 4.0f) {
        const char *message = "Waveform unavailable";
        Vector2 size = MeasureTextEx(ui_font(), message, 12.0f, 1.0f);
        DrawTextEx(ui_font(), message,
                   (Vector2){boundary.x + (boundary.width - size.x)*0.5f,
                             center - size.y*0.5f},
                   12.0f, 1.0f, COLOR_UI_MUTED);
        return;
    }

    size_t columns = (size_t)floorf(boundary.width);
    if (columns > 4096u) columns = 4096u;
    float amplitude = fmaxf(1.0f, boundary.height*0.43f);
    Color waveform = ColorAlpha(COLOR_ACCENT, 0.58f);
    for (size_t column = 0; column < columns; ++column) {
        size_t first = column*bin_count/columns;
        size_t end = (column + 1u)*bin_count/columns;
        if (end <= first) end = first + 1u;
        if (end > bin_count) end = bin_count;
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (size_t i = first; i < end; ++i) {
            if (track->timeline_waveform.bins[i].minimum < minimum) {
                minimum = track->timeline_waveform.bins[i].minimum;
            }
            if (track->timeline_waveform.bins[i].maximum > maximum) {
                maximum = track->timeline_waveform.bins[i].maximum;
            }
        }
        float x = boundary.x + ((float)column + 0.5f)*boundary.width/(float)columns;
        DrawLineEx((Vector2){x, center - maximum*amplitude},
                   (Vector2){x, center - minimum*amplitude},
                   1.0f, waveform);
    }
}

static void update_transport_shortcuts(Track *track)
{
    if (track == NULL || p->lyric_text_active) return;
    int direction = IsKeyPressed(KEY_LEFT) ? -1 :
                    IsKeyPressed(KEY_RIGHT) ? 1 : 0;
    if (direction == 0) return;
    bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    double step = control ? 0.1 : shift ? 10.0 : 1.0;
    seek_track_by(track, (double)direction*step);
}

static void timeline(Rectangle timeline_boundary, Track *track)
{
    DrawRectangleRec(timeline_boundary, COLOR_TIMELINE_BACKGROUND);

    float played = GetMusicTimePlayed(track->music);
    float len = GetMusicTimeLength(track->music);
    if (len <= 0.0f) return;

    const float controls_height = 38.0f;
    const float transport_height = 32.0f;
    const float margin = 6.0f;
    const float event_button_width = 74.0f;
    Rectangle controls = {
        timeline_boundary.x + margin,
        timeline_boundary.y + margin,
        event_button_width*6.0f + margin*6.0f + 112.0f,
        controls_height,
    };
    const char *labels[6] = {"Lyrics", "Assist", "Export", "+ Feel", "+ Cue", "+ Custom"};
    const uint32_t types[6] = {
        EVENT_TYPE_LYRIC, EVENT_TYPE_LYRIC, EVENT_TYPE_LYRIC, EVENT_TYPE_SEMANTIC,
        EVENT_TYPE_CUE, EVENT_TYPE_CUSTOM
    };
    float control_x = controls.x;
    for (size_t i = 0; i < 6; ++i) {
        Rectangle boundary = {control_x, controls.y, event_button_width, controls.height};
        int state = text_button(UINT64_C(0x45564E5400000000) + i, boundary, labels[i],
                                (i == 0 && p->lyrics_editor_open) ||
                                (i == 1 && p->assist_panel_open) ||
                                (i == 2 && p->export_panel_open));
        DrawRectangleLinesEx(boundary, 1.0f,
                             ColorAlpha(i < 3 ? (Color){242, 190, 66, 255}
                                               : event_type_color(types[i]), 0.8f));
        if (state & BS_CLICKED) {
            bool blocked = p->lyrics_editor_open &&
                           !lyric_editor_allow_context_change(track);
            if (!blocked && i == 0) {
                p->lyrics_editor_open = !p->lyrics_editor_open;
                if (p->lyrics_editor_open) p->assist_panel_open = false;
                if (p->lyrics_editor_open) p->export_panel_open = false;
                if (!p->lyrics_editor_open) p->lyric_text_active = false;
            } else if (!blocked && i == 1) {
                p->assist_panel_open = !p->assist_panel_open;
                if (p->assist_panel_open) {
                    p->lyrics_editor_open = false;
                    p->export_panel_open = false;
                    p->lyric_text_active = false;
                }
            } else if (!blocked && i == 2) {
                p->export_panel_open = !p->export_panel_open;
                if (p->export_panel_open) {
                    p->lyrics_editor_open = false;
                    p->assist_panel_open = false;
                    p->lyric_text_active = false;
                }
            } else if (!blocked) {
                record_timeline_event(track, types[i]);
            }
        }
        control_x += event_button_width + margin;
    }
    Rectangle clear_boundary = {control_x, controls.y, 112.0f, controls.height};
    const char *clear_label = p->event_undo_available ? "Undo clear" :
                              p->clear_events_confirmation ? "Confirm clear" :
                              "Clear manual";
    if (text_button(UINT64_C(0x45564E54FFFFFFFF), clear_boundary,
                    clear_label, p->clear_events_confirmation) & BS_CLICKED) {
        if (p->event_undo_available) {
            if (event_timeline_replace(&track->manual_events, &p->event_undo) ==
                EVENT_TIMELINE_OK) {
                p->event_undo_available = false;
                track->next_manual_event_id = timeline_next_id(&track->manual_events);
                mark_project_dirty(track);
                notice_push(UI_NOTICE_SUCCESS, "Manual events restored",
                            "The last clear operation was undone.", NULL, false);
            }
        } else if (p->clear_events_confirmation) {
            p->event_undo = track->manual_events;
            event_timeline_clear(&track->manual_events);
            track->next_manual_event_id = 1;
            p->event_undo_available = true;
            p->clear_events_confirmation = false;
            mark_project_dirty(track);
            notice_push(UI_NOTICE_SUCCESS, "Manual events cleared",
                        "Lyrics, semantic cues, and scene suggestions were not changed.",
                        NULL, false);
        } else {
            p->clear_events_confirmation = true;
            notice_push(UI_NOTICE_WARNING, "Confirm manual-event clear",
                        "Click Confirm clear. Lyrics and imported lanes will remain.",
                        NULL, false);
        }
    }

    char played_time[32];
    char total_time[32];
    char timecode[72];
    format_timestamp(played, played_time, sizeof(played_time));
    format_timestamp(len, total_time, sizeof(total_time));
    snprintf(timecode, sizeof(timecode), "%s / %s", played_time, total_time);
    Vector2 timecode_size = MeasureTextEx(ui_font(), timecode, 22.0f, 1.0f);
    DrawTextEx(ui_font(), timecode,
               (Vector2){timeline_boundary.x + timeline_boundary.width -
                         timecode_size.x - margin,
                         timeline_boundary.y + 13.0f},
               22.0f, 1.0f, COLOR_ACCENT);

    Rectangle transport = {
        timeline_boundary.x + margin,
        timeline_boundary.y + controls_height + margin*2.0f,
        timeline_boundary.width - margin*2.0f,
        transport_height,
    };
    const char *seek_labels[] = {
        "Start", "-10 s", "-1 s", "-0.1 s", "+0.1 s", "+1 s", "+10 s",
    };
    const double seek_deltas[] = {0.0, -10.0, -1.0, -0.1, 0.1, 1.0, 10.0};
    float seek_x = transport.x;
    for (size_t i = 0; i < NOB_ARRAY_LEN(seek_labels); ++i) {
        float width = i == 0 ? 62.0f : 58.0f;
        Rectangle button = {seek_x, transport.y, width, transport.height};
        if (text_button(UINT64_C(0x5345454B00000000) + i, button,
                        seek_labels[i], false) & BS_CLICKED) {
            if (i == 0) seek_track_to(track, 0.0);
            else seek_track_by(track, seek_deltas[i]);
            played = GetMusicTimePlayed(track->music);
        }
        seek_x += width + margin;
    }
    const char *shortcut = "Arrow keys: 1 s  |  Ctrl: 0.1 s  |  Shift: 10 s";
    Vector2 shortcut_size = MeasureTextEx(ui_font(), shortcut, 12.0f, 1.0f);
    if (seek_x + margin + shortcut_size.x < transport.x + transport.width) {
        DrawTextEx(ui_font(), shortcut,
                   (Vector2){seek_x + margin,
                             transport.y + (transport.height - shortcut_size.y)*0.5f},
                   12.0f, 1.0f, COLOR_UI_MUTED);
    }

    const bool expanded_panel = p->lyrics_editor_open || p->assist_panel_open ||
                                p->export_panel_open;
    const float lane_top = transport.y + transport.height + margin;
    const float lane_height = expanded_panel ? 58.0f :
                              timeline_boundary.y + timeline_boundary.height -
                                  lane_top - margin;
    Rectangle waveform_lane = {
        timeline_boundary.x + margin, lane_top,
        timeline_boundary.width - margin*2.0f, fmaxf(24.0f, lane_height),
    };
    Rectangle lyric_lane = {
        waveform_lane.x,
        waveform_lane.y + fmaxf(0.0f, waveform_lane.height - 22.0f),
        waveform_lane.width,
        fminf(22.0f, waveform_lane.height),
    };

    BeginScissorMode((int)waveform_lane.x, (int)waveform_lane.y,
                     (int)waveform_lane.width, (int)waveform_lane.height);
    draw_track_waveform(waveform_lane, track);

    double tick_step = len > 600.0f ? 60.0 : len > 180.0f ? 30.0 : 10.0;
    for (double seconds = 0.0; seconds < len; seconds += tick_step) {
        float tick_x = waveform_lane.x + (float)(seconds/len)*waveform_lane.width;
        DrawLineEx((Vector2){tick_x, waveform_lane.y},
                   (Vector2){tick_x, waveform_lane.y + waveform_lane.height},
                   1.0f, COLOR_UI_RULE);
        if (seconds > 0.0 && waveform_lane.height >= 48.0f) {
            char tick_label[24];
            format_timestamp(seconds, tick_label, sizeof(tick_label));
            DrawTextEx(ui_font(), tick_label,
                       (Vector2){tick_x + 4.0f, waveform_lane.y + 4.0f},
                       12.0f, 1.0f, COLOR_UI_MUTED);
        }
    }

    Event_Timeline_View events = combined_scene_events();
    for (size_t i = 0; i < events.count; ++i) {
        const Event_Record *event = &events.events[i];
        float t = (float)(event->timestamp_seconds/len);
        if (t < 0.0f || t > 1.0f) continue;
        float marker_x = waveform_lane.x + t*waveform_lane.width;
        Color color = event_type_color(event->type);
        DrawLineEx((Vector2){marker_x, waveform_lane.y},
                   (Vector2){marker_x, waveform_lane.y + waveform_lane.height},
                   3.0f, ColorAlpha(color, 0.75f));
        DrawCircleV((Vector2){marker_x, waveform_lane.y},
                    5.0f, color);
    }

    draw_lyric_lane(lyric_lane, track, len);
    float x = waveform_lane.x + played/len*waveform_lane.width;
    DrawLineEx((Vector2){x, waveform_lane.y},
               (Vector2){x, waveform_lane.y + waveform_lane.height},
               2.0f, COLOR_TIMELINE_CURSOR);
    EndScissorMode();

    if (p->lyrics_editor_open) {
        Rectangle editor = {
            timeline_boundary.x + margin,
            waveform_lane.y + waveform_lane.height + margin,
            timeline_boundary.width - margin*2.0f,
            timeline_boundary.y + timeline_boundary.height -
                (waveform_lane.y + waveform_lane.height + margin) - margin,
        };
        if (editor.height > 80.0f) draw_lyrics_editor(editor, track, played);
    } else if (p->assist_panel_open) {
        Rectangle panel = {
            timeline_boundary.x + margin,
            waveform_lane.y + waveform_lane.height + margin,
            timeline_boundary.width - margin*2.0f,
            timeline_boundary.y + timeline_boundary.height -
                (waveform_lane.y + waveform_lane.height + margin) - margin,
        };
        if (panel.height > 80.0f) draw_assist_panel(panel, track);
    } else if (p->export_panel_open) {
        Rectangle panel = {
            timeline_boundary.x + margin,
            waveform_lane.y + waveform_lane.height + margin,
            timeline_boundary.width - margin*2.0f,
            timeline_boundary.y + timeline_boundary.height -
                (waveform_lane.y + waveform_lane.height + margin) - margin,
        };
        if (panel.height > 80.0f) draw_export_panel(panel, track);
    }

    Vector2 mouse = GetMousePosition();
    const uint64_t drag_id = UINT64_C(0x5345454B44524147);
    if (p->active_button_id == 0 && CheckCollisionPointRec(mouse, waveform_lane) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        p->active_button_id = drag_id;
    }
    if (p->active_button_id == drag_id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            double target = track_timeline_seek_from_x(
                GetMusicTimePlayed(track->music), mouse.x, waveform_lane.x,
                waveform_lane.width, len);
            seek_track_to(track, target);
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) p->active_button_id = 0;
    }

    // TODO: enable the user to render a specific region instead of the whole song.
}

static int button_with_id(uint64_t id, Rectangle boundary)
{
    Vector2 mouse = GetMousePosition();
    int hoverover = CheckCollisionPointRec(mouse, boundary);

    int clicked = 0;
    if (p->active_button_id == 0) {
        if (hoverover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            p->active_button_id = id;
        }
    } else if (p->active_button_id == id) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            p->active_button_id = 0;
            if (hoverover) clicked = 1;
        }
    }

    return (clicked<<1) | hoverover;
}

#define DJB2_INIT 5381

static uint64_t djb2(uint64_t hash, const void *buf, size_t buf_sz)
{
    const uint8_t *bytes = buf;
    for (size_t i = 0; i < buf_sz; ++i) {
        hash = hash*33 + bytes[i];
    }
    return hash;
}

static const char *assist_mode_argument(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS: return "lyrics";
    case ASSIST_MODE_SECTIONS: return "sections";
    case ASSIST_MODE_MIMO: return "mimo";
    case ASSIST_MODE_ALL: return "all";
    }
    return "sections";
}

static bool start_assist_job(Assist_Mode mode, Track *track)
{
    if (track == NULL || p->assist_job_state == ASSIST_JOB_RUNNING ||
        p->assist_candidate != NULL || assist_mode_lanes(mode) == 0) return false;
    size_t track_index = (size_t)(track - p->tracks.items);
    if (track_index >= p->tracks.count) return false;

    uint64_t path_hash = djb2(DJB2_INIT, track->file_path, strlen(track->file_path));
    path_hash = djb2(path_hash, &track->lyrics.duration_seconds,
                     sizeof(track->lyrics.duration_seconds));
    if (!nob_mkdir_if_not_exists("./build/analysis")) return false;
    snprintf(p->assist_output_dir, sizeof(p->assist_output_dir),
             "./build/analysis/%016llx", (unsigned long long)path_hash);
    if (!nob_mkdir_if_not_exists(p->assist_output_dir)) return false;
    // The output directory remains stable so the Python orchestration layer can
    // reuse measured/model caches. Each accepted bridge and its diagnostic log
    // are immutable job artifacts: a later Lyrics run must not invalidate the
    // provenance of an earlier MiMo lane.
    bool unique_artifacts = false;
    uint64_t process_id = musi_project_process_id();
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        p->assist_job_nonce += 1;
        if (p->assist_job_nonce == 0) p->assist_job_nonce = 1;
        int bridge_length = snprintf(
            p->assist_bridge_path, sizeof(p->assist_bridge_path),
            "%s/%s-%llu-%016llx.bridge.tsv", p->assist_output_dir,
            assist_mode_argument(mode),
            (unsigned long long)process_id,
            (unsigned long long)p->assist_job_nonce);
        int log_length = snprintf(
            p->assist_log_path, sizeof(p->assist_log_path),
            "%s/%s-%llu-%016llx.log", p->assist_output_dir,
            assist_mode_argument(mode),
            (unsigned long long)process_id,
            (unsigned long long)p->assist_job_nonce);
        if (bridge_length <= 0 || log_length <= 0 ||
            (size_t)bridge_length >= sizeof(p->assist_bridge_path) ||
            (size_t)log_length >= sizeof(p->assist_log_path)) return false;
        if (!FileExists(p->assist_bridge_path) && !FileExists(p->assist_log_path)) {
            unique_artifacts = true;
            break;
        }
    }
    if (!unique_artifacts) return false;

    char duration_text[64];
    snprintf(duration_text, sizeof(duration_text), "%.9f", track->lyrics.duration_seconds);
    char helper_path[PLUG_RELOAD_PATH_CAPACITY];
    if (!find_assist_helper(helper_path, sizeof(helper_path))) return false;
    Nob_Cmd command = {0};
    Nob_Procs processes = {0};
#ifdef _WIN32
    nob_cmd_append(&command, "py", "-3");
#else
    nob_cmd_append(&command, "python3");
#endif
    nob_cmd_append(&command,
        helper_path, "assist",
        track->file_path, p->assist_output_dir,
        "--duration", duration_text,
        "--mode", assist_mode_argument(mode),
        "--bridge", p->assist_bridge_path,
        "--timeout", "2400", "--new-process-group");
    if (mode == ASSIST_MODE_MIMO || mode == ASSIST_MODE_ALL) {
        nob_cmd_append(&command, "--zdr");
    }
    bool started = nob_cmd_run(&command, .async = &processes, .max_procs = 1,
                               .stderr_path = p->assist_log_path);
    nob_cmd_free(command);
    if (!started || processes.count != 1) {
        nob_da_free(processes);
        p->assist_process = NOB_INVALID_PROC;
        p->assist_job_state = ASSIST_JOB_FAILED;
        return false;
    }
    p->assist_process = processes.items[0];
    nob_da_free(processes);
    p->assist_mode = mode;
    p->assist_track_index = track_index;
    p->assist_job_state = ASSIST_JOB_RUNNING;
    p->assist_started_at = GetTime();
    p->assist_confirmation_pending = false;
    TraceLog(LOG_INFO, "ASSIST: started %s analysis for %s",
             assist_mode_argument(mode), track->file_path);
    return true;
}

static uint32_t assist_mode_lanes(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS: return ANALYSIS_CANDIDATE_LYRICS;
    case ASSIST_MODE_SECTIONS: return ANALYSIS_CANDIDATE_SECTIONS;
    case ASSIST_MODE_MIMO: return ANALYSIS_CANDIDATE_SEMANTICS;
    case ASSIST_MODE_ALL: return ANALYSIS_CANDIDATE_ALL;
    }
    return 0;
}

static Analysis_Candidate *load_analysis_candidate_for_track(
    const char *path, size_t track_index, Assist_Mode mode)
{
    if (path == NULL || track_index >= p->tracks.count) return NULL;
    int file_size = GetFileLength(path);
    if (file_size <= 0 || (size_t)file_size > ANALYSIS_BRIDGE_INPUT_MAX_BYTES) {
        notice_push(UI_NOTICE_ERROR, "Analysis result was rejected",
                    "The bridge is empty or exceeds the 4 MiB input limit.", path, true);
        return NULL;
    }
    int input_size = 0;
    unsigned char *input = LoadFileData(path, &input_size);
    if (input == NULL || input_size <= 0) {
        if (input != NULL) UnloadFileData(input);
        notice_push(UI_NOTICE_ERROR, "Analysis result could not be read",
                    "The validated bridge file is unavailable.", path, true);
        return NULL;
    }
    Analysis_Bridge *bridge = malloc(sizeof(*bridge));
    if (bridge == NULL) {
        UnloadFileData(input);
        notice_push(UI_NOTICE_ERROR, "Analysis result could not be staged",
                    "There is not enough memory for the bounded bridge.", path, true);
        return NULL;
    }
    analysis_bridge_init(bridge);
    Track *track = &p->tracks.items[track_index];
    char expected_audio_sha256[SHA256_HEX_SIZE];
    if (track->audio_sha256[0] != '\0') {
        snprintf(expected_audio_sha256, sizeof(expected_audio_sha256), "%s",
                 track->audio_sha256);
    } else if (sha256_file_hex(track->file_path, expected_audio_sha256)) {
        snprintf(track->audio_sha256, sizeof(track->audio_sha256), "%s",
                 expected_audio_sha256);
    } else {
        UnloadFileData(input);
        free(bridge);
        notice_push(UI_NOTICE_ERROR, "Track identity could not be verified",
                    "The source audio could not be hashed.", track->file_path, true);
        return NULL;
    }
    Analysis_Bridge_Result parsed = analysis_bridge_parse(
        bridge, (const char *)input, (size_t)input_size,
        expected_audio_sha256, 0);
    UnloadFileData(input);
    if (parsed != ANALYSIS_BRIDGE_OK) {
        TraceLog(LOG_WARNING, "ASSIST: rejected bridge: %s",
                 analysis_bridge_result_string(parsed));
        notice_push(UI_NOTICE_ERROR, "Analysis result was rejected",
                    analysis_bridge_result_string(parsed), path, true);
        free(bridge);
        return NULL;
    }
    // MP3 container duration and raylib's decoded frame count may differ by a
    // small encoder-padding tail. SHA-256 establishes identity; accept only a
    // narrow timing discrepancy and use measured duration for imported lanes.
    double bridge_duration = (double)bridge->duration_ms/1000.0;
    if (fabs(bridge_duration - track->lyrics.duration_seconds) > 0.25) {
        TraceLog(LOG_WARNING, "ASSIST: bridge duration does not match the active track");
        notice_push(UI_NOTICE_ERROR, "Analysis result was rejected",
                    "The result duration does not match this track.", path, true);
        free(bridge);
        return NULL;
    }

    Analysis_Candidate *candidate = malloc(sizeof(*candidate));
    if (candidate == NULL) {
        free(bridge);
        notice_push(UI_NOTICE_ERROR, "Analysis result could not be staged",
                    "There is not enough memory for editable suggestions.", path, true);
        return NULL;
    }
    Analysis_Candidate_Result prepared = analysis_candidate_prepare(
        candidate, bridge, assist_mode_lanes(mode), bridge_duration, COUNT_SCENES);
    free(bridge);
    if (prepared != ANALYSIS_CANDIDATE_OK) {
        notice_push(UI_NOTICE_ERROR, "Analysis result was rejected",
                    analysis_candidate_result_string(prepared), path, true);
        free(candidate);
        return NULL;
    }
    return candidate;
}

static bool apply_candidate_to_track(Analysis_Candidate *candidate, size_t track_index)
{
    if (candidate == NULL || track_index >= p->tracks.count) return false;
    Track *track = &p->tracks.items[track_index];
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0 &&
        lyrics_document_normalize_duration(
            &candidate->lyrics, &candidate->lyrics,
            track->duration_seconds) != LYRICS_OK) {
        notice_push(UI_NOTICE_ERROR, "Suggestions were not applied",
                    "Lyric timing could not be normalized to the decoded track duration.",
                    NULL, true);
        return false;
    }
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0 &&
        candidate->sections.count > 0) {
        // The bridge may differ from the decoder by a small MP3 padding tail.
        // Normalize through the public validator instead of mutating a
        // previously validated candidate into a potentially negative last cue.
        if (analysis_candidate_normalize_sections(
                candidate, track->duration_seconds, COUNT_SCENES) !=
            ANALYSIS_CANDIDATE_OK) {
            notice_push(UI_NOTICE_ERROR, "Suggestions were not applied",
                        "Section timing could not be normalized to the decoded track duration.",
                        NULL, true);
            return false;
        }
    }
    Analysis_Candidate_Result applied = analysis_candidate_apply(
        candidate, &track->lyrics, &track->scene_switches, &track->semantic_events);
    if (applied != ANALYSIS_CANDIDATE_OK) {
        notice_push(UI_NOTICE_ERROR, "Suggestions were not applied",
                    analysis_candidate_result_string(applied), NULL, true);
        return false;
    }
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0 &&
        track_index == (size_t)p->current_track) {
        lyric_editor_clear_draft();
    }
    mark_project_dirty(track);
    TraceLog(LOG_INFO, "ASSIST: applied %zu lyrics, %zu scene sections, %zu semantic cues",
             candidate->lyrics.count, candidate->sections.count,
             candidate->semantic_events.count);
    return true;
}

static bool apply_assist_candidate(void)
{
    if (p->assist_candidate == NULL) return false;
    Track *track = p->assist_candidate_track_index < p->tracks.count ?
                   &p->tracks.items[p->assist_candidate_track_index] : NULL;
    if (!apply_candidate_to_track(p->assist_candidate,
                                  p->assist_candidate_track_index)) return false;
    if (track != NULL) {
        uint32_t lanes = p->assist_candidate->available_lanes;
        if ((lanes & ANALYSIS_CANDIDATE_LYRICS) != 0) {
            track_set_analysis_lane(track, MUSI_LANE_LYRIC_TIMING,
                                    p->assist_bridge_path, "whisper-codex");
        }
        if ((lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0) {
            track_set_analysis_lane(track, MUSI_LANE_MEASURED_SIGNAL,
                                    p->assist_bridge_path, "measured-sections");
        }
        if ((lanes & ANALYSIS_CANDIDATE_SEMANTICS) != 0) {
            track_set_analysis_lane(track, MUSI_LANE_SEMANTIC_SCORE,
                                    p->assist_bridge_path, "xiaomi-mimo-v2.5");
        }
    }
    free(p->assist_candidate);
    p->assist_candidate = NULL;
    p->assist_apply_confirmation_pending = false;
    notice_push(UI_NOTICE_SUCCESS, "Suggestions applied",
                "The selected validated lanes are now in the editor.", NULL, false);
    return true;
}

static void discard_assist_candidate(void)
{
    if (p->assist_candidate == NULL) return;
    free(p->assist_candidate);
    p->assist_candidate = NULL;
    p->assist_apply_confirmation_pending = false;
    notice_push(UI_NOTICE_INFO, "Suggestions discarded",
                "Previous editor content is unchanged.", NULL, false);
}

MUSIALIZER_PLUG bool plug_load_analysis_bridge(const char *file_path)
{
    if (p == NULL || p->current_track < 0) return false;
    Analysis_Candidate *candidate = load_analysis_candidate_for_track(
        file_path, (size_t)p->current_track, ASSIST_MODE_ALL);
    if (candidate == NULL) return false;
    bool applied = apply_candidate_to_track(candidate, (size_t)p->current_track);
    if (applied) {
        Track *track = current_track();
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0) {
            track_set_analysis_lane(track, MUSI_LANE_LYRIC_TIMING,
                                    file_path, "imported-bridge");
        }
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0) {
            track_set_analysis_lane(track, MUSI_LANE_MEASURED_SIGNAL,
                                    file_path, "imported-bridge");
        }
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SEMANTICS) != 0) {
            track_set_analysis_lane(track, MUSI_LANE_SEMANTIC_SCORE,
                                    file_path, "imported-bridge");
        }
    }
    free(candidate);
    return applied;
}

MUSIALIZER_PLUG bool plug_set_auto_scenes(bool enabled)
{
    Track *track = current_track();
    if (track == NULL || (enabled && track->scene_switches.count == 0)) return false;
    track->scene_switches.enabled = enabled;
    scene_switch_reset(&track->scene_switches);
    if (!enabled) {
        (void)scene_instance_select(&p->scene, track->base_scene,
                                    track->scene_seed);
    }
    mark_project_dirty(track);
    return true;
}

static void poll_assist_job(void)
{
    if (p->assist_job_state != ASSIST_JOB_RUNNING ||
        p->assist_process == NOB_INVALID_PROC) return;
    Nob_Proc process = p->assist_process;
    int status = nob__proc_wait_async(process, 0);
    if (status == 0) return;
    p->assist_process = NOB_INVALID_PROC;
    if (status < 0) {
#ifdef _WIN32
        // nob's nonzero-exit polling path reports failure without closing the
        // process handle; ownership remains ours on that branch.
        CloseHandle(process);
#endif
        p->assist_job_state = ASSIST_JOB_FAILED;
        TraceLog(LOG_WARNING, "ASSIST: analysis failed; see %s", p->assist_log_path);
        notice_push(UI_NOTICE_ERROR, "Analysis failed",
                    "The helper exited before producing a validated result.",
                    p->assist_log_path, true);
        return;
    }
    TraceLog(LOG_INFO, "ASSIST: analysis completed; staging %s",
             p->assist_bridge_path);
    Analysis_Candidate *candidate = load_analysis_candidate_for_track(
        p->assist_bridge_path, p->assist_track_index, p->assist_mode);
    if (candidate == NULL) {
        p->assist_job_state = ASSIST_JOB_FAILED;
        return;
    }
    free(p->assist_candidate);
    p->assist_candidate = candidate;
    p->assist_apply_confirmation_pending = false;
    p->assist_candidate_track_index = p->assist_track_index;
    p->assist_candidate_mode = p->assist_mode;
    p->assist_job_state = ASSIST_JOB_SUCCEEDED;
    notice_push(UI_NOTICE_SUCCESS, "Analysis ready for review",
                "Validated suggestions are staged in the Assist panel.",
                p->assist_bridge_path, false);
}

static void cancel_assist_job(void)
{
    if (p == NULL || p->assist_job_state != ASSIST_JOB_RUNNING ||
        p->assist_process == NOB_INVALID_PROC) return;
#ifdef _WIN32
    DWORD process_id = GetProcessId(p->assist_process);
    if (process_id != 0) {
        char process_id_text[32];
        snprintf(process_id_text, sizeof(process_id_text), "%lu",
                 (unsigned long)process_id);
        Nob_Cmd terminate = {0};
        nob_cmd_append(&terminate, "taskkill", "/PID", process_id_text, "/T", "/F");
        (void)nob_cmd_run(&terminate);
        nob_cmd_free(terminate);
    }
    if (WaitForSingleObject(p->assist_process, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(p->assist_process, 1);
        WaitForSingleObject(p->assist_process, 2000);
    }
    CloseHandle(p->assist_process);
#else
    pid_t process = p->assist_process;
    if (kill(-process, SIGTERM) < 0 && errno == ESRCH) kill(process, SIGTERM);
    bool finished = false;
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        pid_t waited = waitpid(process, NULL, WNOHANG);
        if (waited == process || (waited < 0 && errno == ECHILD)) {
            finished = true;
            break;
        }
        if (waited < 0 && errno != EINTR) break;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 10*1000*1000};
        nanosleep(&pause, NULL);
    }
    if (!finished) {
        if (kill(-process, SIGKILL) < 0 && errno == ESRCH) kill(process, SIGKILL);
        for (unsigned attempt = 0; attempt < 200; ++attempt) {
            pid_t waited = waitpid(process, NULL, WNOHANG);
            if (waited == process || (waited < 0 && errno == ECHILD)) {
                finished = true;
                break;
            }
            if (waited < 0 && errno != EINTR) break;
            struct timespec pause = {.tv_sec = 0, .tv_nsec = 10*1000*1000};
            nanosleep(&pause, NULL);
        }
        if (!finished) {
            TraceLog(LOG_WARNING, "ASSIST: killed worker could not be reaped promptly");
        }
    }
#endif
    p->assist_process = NOB_INVALID_PROC;
    p->assist_job_state = ASSIST_JOB_CANCELLED;
    p->assist_confirmation_pending = false;
    notice_push(UI_NOTICE_INFO, "Analysis cancelled",
                "Previous editor content is unchanged.", p->assist_log_path, false);
}

static void track_set_analysis_lane(Track *track, Musi_Analysis_Lane_Kind kind,
                                    const char *path, const char *model)
{
    if (track == NULL || path == NULL || path[0] == '\0' ||
        kind >= MUSI_LANE_KIND_COUNT) return;
    if (track->audio_sha256[0] == '\0' &&
        !sha256_file_hex(track->file_path, track->audio_sha256)) return;
    char artifact_sha256[SHA256_HEX_SIZE];
    if (!sha256_file_hex(path, artifact_sha256)) return;

    size_t index = track->analysis_lane_count;
    for (size_t i = 0; i < track->analysis_lane_count; ++i) {
        if (track->analysis_lanes[i].kind == kind) {
            index = i;
            break;
        }
    }
    if (index == track->analysis_lane_count) {
        if (track->analysis_lane_count >= MUSI_PROJECT_MAX_ANALYSIS_LANES) return;
        ++track->analysis_lane_count;
    }
    Musi_Analysis_Lane_Reference *lane = &track->analysis_lanes[index];
    memset(lane, 0, sizeof(*lane));
    lane->kind = kind;
    snprintf(lane->path, sizeof(lane->path), "%s", path);
    snprintf(lane->sha256, sizeof(lane->sha256), "%s", artifact_sha256);
    snprintf(lane->audio_sha256, sizeof(lane->audio_sha256), "%s",
             track->audio_sha256);
    snprintf(lane->provenance.adapter, sizeof(lane->provenance.adapter),
             "external-analysis");
    snprintf(lane->provenance.adapter_version,
             sizeof(lane->provenance.adapter_version), "1");
    snprintf(lane->provenance.schema_version,
             sizeof(lane->provenance.schema_version), "analysis-bridge-v1");
    snprintf(lane->provenance.model, sizeof(lane->provenance.model), "%s",
             model ? model : "");
    snprintf(lane->provenance.prompt_version,
             sizeof(lane->provenance.prompt_version), "v1");
}

static bool build_project(Track *track, const char *project_path,
                          Musi_Project *project)
{
    if (track == NULL || project_path == NULL || project_path[0] == '\0' ||
        project == NULL) return false;
    musi_project_init(project);
    if (track->audio_sha256[0] == '\0' &&
        !sha256_file_hex(track->file_path, track->audio_sha256)) return false;

    const double duration = track->duration_seconds;
    if (track->project_metadata_initialized) {
        project->metadata = track->project_metadata;
    } else {
        const char *title = GetFileNameWithoutExt(track->file_path);
        snprintf(project->metadata.project_id, sizeof(project->metadata.project_id),
                 "track-%.16s", track->audio_sha256);
        snprintf(project->metadata.title, sizeof(project->metadata.title), "%s",
                 title && title[0] ? title : "Untitled visualization");
    }
    snprintf(project->metadata.application_version,
             sizeof(project->metadata.application_version), "musializer-2026.07");

    project->audio.mode = MUSI_ASSET_REFERENCED;
    Musi_Project_Stored_Path_Result stored_path =
        musi_project_asset_path_for_storage(
            project_path, track->file_path, project->audio.path,
            sizeof(project->audio.path));
    if (stored_path != MUSI_PROJECT_STORED_PATH_RELATIVE &&
        stored_path != MUSI_PROJECT_STORED_PATH_ABSOLUTE) return false;
    snprintf(project->audio.sha256, sizeof(project->audio.sha256), "%s",
             track->audio_sha256);
    project->audio.duration_seconds = duration;
    project->audio.sample_rate = track->music.stream.sampleRate;
    project->audio.channels = (uint16_t)track->music.stream.channels;

    project->output.width = track->render_config.width;
    project->output.height = track->render_config.height;
    project->output.fps_numerator = track->render_config.fps;
    project->output.fps_denominator = 1;
    project->output.start_seconds = 0.0;
    project->output.end_seconds = duration;
    project->output.format = MUSI_OUTPUT_MP4_H264;
    project->output.quality = (Musi_Output_Quality)track->render_config.quality;
    project->deterministic_seed = track->scene_seed;

    project->scene_count = 1;
    project->scenes[0] = (Musi_Scene_Entry) {
        .instance_id = track->scene_instance_id != 0 ? track->scene_instance_id : 1,
        .enabled = true,
        .start_seconds = 0.0,
        .end_seconds = duration,
        .opacity = 1.0,
        .blend_mode = MUSI_BLEND_NORMAL,
    };
    snprintf(project->scenes[0].scene_type,
             sizeof(project->scenes[0].scene_type), "%s",
             scene_stable_name(track->base_scene));

    project->lyrics = track->lyrics;
    project->semantic_events = track->semantic_events;
    project->manual_events = track->manual_events;
    project->scene_switches.enabled = track->scene_switches.enabled;
    project->scene_switches.count = track->scene_switches.count;
    for (size_t i = 0; i < track->scene_switches.count; ++i) {
        const Scene_Switch_Cue *source = &track->scene_switches.cues[i];
        Musi_Scene_Switch_Suggestion *destination = &project->scene_switches.cues[i];
        destination->id = source->id;
        destination->start_seconds = source->start_seconds;
        destination->end_seconds = source->end_seconds;
        destination->strength = source->strength;
        snprintf(destination->scene_name, sizeof(destination->scene_name), "%s",
                 scene_stable_name((Scene_Id)source->scene_index));
    }
    project->analysis_lane_count = track->analysis_lane_count;
    memcpy(project->analysis_lanes, track->analysis_lanes,
           track->analysis_lane_count*sizeof(track->analysis_lanes[0]));

    return musi_project_validate(project).error == MUSI_PROJECT_VALID;
}

static bool save_project_to_path(Track *track, const char *path, bool show_success)
{
    if (track == NULL || path == NULL || path[0] == '\0') return false;
    if (strlen(path) >= sizeof(track->project_path)) {
        notice_push(UI_NOTICE_ERROR, "Project path is too long",
                    "Choose a destination with a shorter absolute path.",
                    path, true);
        return false;
    }
    if (!IsFileExtension(path, ".musi")) {
        notice_push(UI_NOTICE_ERROR, "Project path must end in .musi",
                    "Use the Musializer project extension so launchers and drag-and-drop can identify the file.",
                    path, true);
        return false;
    }
    if (musi_project_existing_files_alias(track->file_path, path)) {
        notice_push(UI_NOTICE_ERROR, "Project path aliases the source audio",
                    "Choose another .musi path. The source track was not modified.",
                    path, true);
        return false;
    }
    if (ascii_art_grid_is_populated(track->ascii_columns, track->ascii_rows)) {
        notice_push(UI_NOTICE_ERROR, "Imported ASCII image is not project-portable yet",
                    "Export video now, or use Clear image in the Scenes panel before saving. Empty ASCII scenes are safe to save.",
                    path, true);
        return false;
    }
    Musi_Project *project = malloc(sizeof(*project));
    if (project == NULL) return false;
    if (!build_project(track, path, project)) {
        free(project);
        notice_push(UI_NOTICE_ERROR, "Project could not be built",
                    "A track path, authored lane, or output setting is outside the project contract.",
                    path, true);
        return false;
    }
    size_t required = 0;
    Musi_Project_Io_Result measured = musi_project_json_serialize(
        project, NULL, 0, &required);
    if (measured != MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL ||
        required == 0 || required > INT_MAX) {
        free(project);
        return false;
    }
    char *json = malloc(required);
    if (json == NULL) {
        free(project);
        return false;
    }
    Musi_Project_Io_Result encoded = musi_project_json_serialize(
        project, json, required, &required);
    Musi_Project_Metadata saved_metadata = project->metadata;
    free(project);
    if (encoded != MUSI_PROJECT_IO_OK) {
        free(json);
        return false;
    }

    Musi_Project_File_Result file_result = musi_project_atomic_write(
        path, json, required - 1);
    free(json);
    if (file_result != MUSI_PROJECT_FILE_OK) {
        char detail[UI_NOTICE_DETAIL_CAPACITY];
        snprintf(detail, sizeof(detail),
                 "The previous project file was preserved: %s.",
                 musi_project_file_result_string(file_result));
        track->project_autosave_failed = true;
        notice_push(UI_NOTICE_ERROR, "Project could not be saved",
                    detail, path, true);
        return false;
    }

    snprintf(track->project_path, sizeof(track->project_path), "%s", path);
    track->project_metadata = saved_metadata;
    track->project_metadata_initialized = true;
    track->project_dirty = false;
    track->project_autosave_failed = false;
    if (show_success) {
        notice_push(UI_NOTICE_SUCCESS, "Project saved",
                    "Lyrics, semantic cues, manual events, scene suggestions, and output settings are durable.",
                    path, false);
    }
    return true;
}

static bool save_project_as(Track *track)
{
    if (track == NULL) return false;
    const char *filters[] = {"*.musi"};
    char suggested[PLUG_RELOAD_PATH_CAPACITY];
    const char *name = track->file_path;
    const char *dot = strrchr(name, '.');
    const char *slash = strrchr(name, '/');
    const char *backslash = strrchr(name, '\\');
    const char *separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
    if (dot != NULL && separator != NULL && dot < separator) dot = NULL;
    size_t prefix = dot != NULL ? (size_t)(dot - name) : strlen(name);
    if (prefix > sizeof(suggested) - 6) prefix = sizeof(suggested) - 6;
    snprintf(suggested, sizeof(suggested), "%.*s.musi", (int)prefix, name);
    char *path = tinyfd_saveFileDialog("Save Musializer project", suggested,
                                       NOB_ARRAY_LEN(filters), filters,
                                       "Musializer project");
    if (path == NULL) return false;
    return save_project_to_path(track, path, true);
}

static bool save_project(Track *track, bool show_success)
{
    if (track == NULL) return false;
    if (track->project_path[0] == '\0') return show_success ? save_project_as(track) : false;
    return save_project_to_path(track, track->project_path, show_success);
}

static bool project_open_allowed(void)
{
    if (!lyric_editor_has_unsaved_draft(current_track())) return true;
    notice_push(UI_NOTICE_WARNING, "Project was not opened",
                "Apply or discard the current lyric draft before replacing the workspace.",
                NULL, false);
    return false;
}

static bool open_project_path(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    if (!project_open_allowed()) return false;
    int file_size = GetFileLength(path);
    if (file_size <= 0 || (size_t)file_size > MUSI_PROJECT_JSON_MAX_INPUT) {
        notice_push(UI_NOTICE_ERROR, "Project could not be opened",
                    "The file is empty or exceeds the 4 MiB project limit.", path, true);
        return false;
    }
    int input_size = 0;
    unsigned char *input = LoadFileData(path, &input_size);
    Musi_Project *project = malloc(sizeof(*project));
    if (input == NULL || input_size <= 0 || project == NULL) {
        if (input != NULL) UnloadFileData(input);
        free(project);
        notice_push(UI_NOTICE_ERROR, "Project could not be opened",
                    "The project file could not be read or allocated.", path, true);
        return false;
    }
    Musi_Project_Io_Result parsed = musi_project_json_deserialize(
        project, (const char *)input, (size_t)input_size);
    UnloadFileData(input);
    if (parsed != MUSI_PROJECT_IO_OK) {
        notice_push(UI_NOTICE_ERROR, "Project was rejected",
                    musi_project_io_result_string(parsed), path, true);
        free(project);
        return false;
    }
    Musi_Project_Editor_Support editor_support =
        musi_project_editor_support(project);
    if (editor_support != MUSI_PROJECT_EDITOR_SUPPORTED) {
        notice_push(UI_NOTICE_ERROR, "Project uses unsupported editor features",
                    musi_project_editor_support_string(editor_support),
                    path, true);
        free(project);
        return false;
    }

    Scene_Id scene_id;
    if (!scene_id_from_name(project->scenes[0].scene_type, &scene_id)) {
        notice_push(UI_NOTICE_ERROR, "Project scene is unavailable",
                    project->scenes[0].scene_type, path, true);
        free(project);
        return false;
    }
    Scene_Switch_Cue cues[SCENE_SWITCH_CAPACITY];
    for (size_t i = 0; i < project->scene_switches.count; ++i) {
        Scene_Id cue_scene;
        if (!scene_id_from_name(project->scene_switches.cues[i].scene_name, &cue_scene)) {
            notice_push(UI_NOTICE_ERROR, "Project scene suggestion is unavailable",
                        project->scene_switches.cues[i].scene_name, path, true);
            free(project);
            return false;
        }
        cues[i] = (Scene_Switch_Cue) {
            .id = project->scene_switches.cues[i].id,
            .start_seconds = project->scene_switches.cues[i].start_seconds,
            .end_seconds = project->scene_switches.cues[i].end_seconds,
            .scene_index = (uint32_t)cue_scene,
            .strength = project->scene_switches.cues[i].strength,
        };
    }
    Scene_Switch_Timeline scene_switches;
    scene_switch_init(&scene_switches);
    if (project->scene_switches.count > 0 &&
        scene_switch_replace(&scene_switches, cues, project->scene_switches.count,
                             project->audio.duration_seconds, COUNT_SCENES) !=
        SCENE_SWITCH_OK) {
        notice_push(UI_NOTICE_ERROR, "Project scene suggestions were rejected",
                    "The scene timeline is not contiguous or references an invalid scene.",
                    path, true);
        free(project);
        return false;
    }
    scene_switches.enabled = project->scene_switches.enabled;

    Render_Export_Config render_config = {
        .width = project->output.width,
        .height = project->output.height,
        .fps = project->output.fps_numerator,
        .quality = (Render_Quality)project->output.quality,
        .supersample_factor = 1,
    };
    (void)render_export_config_set_quality(
        &render_config, (Render_Quality)project->output.quality);
    if (render_export_config_validate(&render_config) != RENDER_EXPORT_OK) {
        notice_push(UI_NOTICE_ERROR, "Project output settings are unsupported",
                    "Use an even resolution up to 7680x4320 and 1 to 240 fps.", path, true);
        free(project);
        return false;
    }

    char audio_path[PLUG_RELOAD_PATH_CAPACITY];
    char audio_hash[SHA256_HEX_SIZE];
    Musi_Project_Path_Result audio_resolution = musi_project_resolve_asset_path(
        path, project->audio.path, audio_path, sizeof(audio_path));
    if (!musi_project_path_result_is_success(audio_resolution) ||
        !sha256_file_hex(audio_path, audio_hash) ||
        strcmp(audio_hash, project->audio.sha256) != 0) {
        notice_push(UI_NOTICE_ERROR, "Project audio does not match",
                    "The referenced audio is missing or its SHA-256 identity changed.",
                    project->audio.path, true);
        free(project);
        return false;
    }
    if (audio_resolution == MUSI_PROJECT_PATH_RESOLVED_LEGACY_CWD) {
        notice_push(UI_NOTICE_WARNING, "Project used a legacy asset path",
                    "Audio was found in the launch directory because it was not beside the project. Move it beside the project or use an absolute path for portability.",
                    project->audio.path, true);
    }

    Music metadata_probe = LoadMusicStream(audio_path);
    if (!IsMusicValid(metadata_probe)) {
        notice_push(UI_NOTICE_ERROR, "Project audio could not be inspected",
                    "The verified file is not decodable by this build.",
                    audio_path, true);
        free(project);
        return false;
    }
    double decoded_duration = GetMusicTimeLength(metadata_probe);
    bool metadata_matches = musi_project_audio_metadata_matches(
        project, decoded_duration, metadata_probe.stream.sampleRate,
        (uint16_t)metadata_probe.stream.channels, 0.001);
    UnloadMusicStream(metadata_probe);
    if (!metadata_matches) {
        notice_push(UI_NOTICE_ERROR, "Project audio metadata does not match",
                    "The audio hash matches, but its decoded duration, sample rate, or channel count differs from the saved editor timeline.",
                    audio_path, true);
        free(project);
        return false;
    }

    Scene_Instance hydrated_scene;
    if (!scene_instance_init(&hydrated_scene, scene_id,
                             project->deterministic_seed)) {
        notice_push(UI_NOTICE_ERROR, "Project scene could not be prepared",
                    "The scene did not have enough resources to restore its deterministic state.",
                    project->scenes[0].scene_type, true);
        free(project);
        return false;
    }

    int previous_track_index = p->current_track;
    size_t new_index = p->tracks.count;
    if (!plug_load_track(audio_path) || new_index >= p->tracks.count) {
        scene_instance_unload(&hydrated_scene);
        notice_push(UI_NOTICE_ERROR, "Project audio could not be loaded",
                    "The verified audio decoder rejected the file.", audio_path, true);
        free(project);
        return false;
    }
    Track *old_track = previous_track_index >= 0 &&
                       (size_t)previous_track_index < p->tracks.count ?
                       &p->tracks.items[previous_track_index] : NULL;
    Track *track = &p->tracks.items[new_index];
    (void)lyrics_document_replace(&track->lyrics, &project->lyrics);
    track->scene_switches = scene_switches;
    (void)event_timeline_replace(&track->semantic_events, &project->semantic_events);
    (void)event_timeline_replace(&track->manual_events, &project->manual_events);
    track->next_manual_event_id = timeline_next_id(&track->manual_events);
    track->base_scene = scene_id;
    track->scene_seed = project->deterministic_seed;
    track->scene_instance_id = project->scenes[0].instance_id;
    track->project_metadata = project->metadata;
    track->project_metadata_initialized = true;
    track->analysis_lane_count = project->analysis_lane_count;
    memcpy(track->analysis_lanes, project->analysis_lanes,
           project->analysis_lane_count*sizeof(project->analysis_lanes[0]));
    snprintf(track->project_path, sizeof(track->project_path), "%s", path);
    snprintf(track->audio_sha256, sizeof(track->audio_sha256), "%s", audio_hash);
    track->render_config = render_config;
    set_active_render_config(render_config);
    scene_instance_unload(&p->scene);
    p->scene = hydrated_scene;
    if (old_track != NULL) StopMusicStream(old_track->music);
    p->current_track = (int)new_index;
    if (old_track != NULL) start_preview_track(track);
    track->project_dirty = false;
    track->project_autosave_failed = false;
    p->event_undo_available = false;
    p->clear_events_confirmation = false;
    lyric_editor_clear_draft();
    free(project);
    notice_push(UI_NOTICE_SUCCESS, "Project opened",
                "Lyrics, embedded semantic cues, authored lanes, scene plan, and output settings were restored.",
                path, false);
    return true;
}

static bool open_project_dialog(void)
{
    if (!project_open_allowed()) return false;
    const char *filters[] = {"*.musi"};
    char *path = tinyfd_openFileDialog("Open Musializer project", "./",
                                       NOB_ARRAY_LEN(filters), filters,
                                       "Musializer project", 0);
    return path != NULL && open_project_path(path);
}

MUSIALIZER_PLUG bool plug_load_project(const char *file_path)
{
    return open_project_path(file_path);
}

MUSIALIZER_PLUG bool plug_save_project(const char *file_path)
{
    return save_project_to_path(current_track(), file_path, false);
}

static void poll_project_autosave(Track *track)
{
    if (track == NULL || track->project_path[0] == '\0' ||
        !track->project_dirty || track->project_autosave_failed ||
        (track == current_track() && lyric_editor_has_unsaved_draft(track)) ||
        GetTime() - track->project_dirty_since < 1.5) return;
    if (!save_project(track, false)) track->project_autosave_failed = true;
}

static int button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));
    return button_with_id(id, boundary);
}

#define button(boundary) button_with_location(__FILE__, __LINE__, boundary)

// NOTE: This is literally DrawTextEx() copy-pasted from Raylib itself but with the
// max_width support and without newlines
void track_label(Font font, const char *text, Vector2 position, float fontSize, Color tint)
{
    if (font.texture.id == 0) font = ui_font();  // Security check in case of not valid font

    float spacing = 0;

    int size = TextLength(text);    // Total size in bytes of the text, scanned by codepoints in loop

    int textOffsetY = 0;            // Offset between lines (on linebreak '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/font.baseSize;         // Character quad scaling factor

    for (int i = 0; i < size;)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        if (codepoint == '\n') codepoint = ' '; // Treat newlines as spaces

        if ((codepoint != ' ') && (codepoint != '\t'))
        {
            DrawTextCodepoint(font, codepoint, (Vector2){ position.x + textOffsetX, position.y + textOffsetY }, fontSize, tint);
        }

        if (font.glyphs[index].advanceX == 0) textOffsetX += ((float)font.recs[index].width*scaleFactor + spacing);
        else textOffsetX += ((float)font.glyphs[index].advanceX*scaleFactor + spacing);

        i += codepointByteCount;   // Move text bytes counter to next codepoint
    }
}

#define tracks_panel(panel_boundary) \
    tracks_panel_with_location(__FILE__, __LINE__, panel_boundary)
static void tracks_panel_with_location(const char *file, int line, Rectangle panel_boundary)
{
    DrawRectangleRec(panel_boundary, COLOR_TRACK_PANEL_BACKGROUND);
    const float header_height = 96.0f;
    DrawTextEx(ui_font(), "TRACK PROJECTS",
               (Vector2){panel_boundary.x + 10.0f, panel_boundary.y + 16.0f},
               18.0f, 1.0f, COLOR_UI_INK);
    Track *active_track = current_track();
    bool workspace_dirty = active_track != NULL &&
                           (active_track->project_dirty ||
                            lyric_editor_has_unsaved_draft(active_track));
    const char *save_status = active_track != NULL && active_track->project_path[0] != '\0' ?
                              active_track->project_autosave_failed ? "Save failed" :
                              workspace_dirty ? "Unsaved" : "Saved" :
                              "No project file";
    Vector2 status_size = MeasureTextEx(ui_font(), save_status, 13.0f, 1.0f);
    DrawTextEx(ui_font(), save_status,
               (Vector2){panel_boundary.x + panel_boundary.width - status_size.x - 10.0f,
                         panel_boundary.y + 18.0f},
               13.0f, 1.0f,
               workspace_dirty ? COLOR_ACCENT : COLOR_UI_MUTED);
    const float action_gap = 4.0f;
    const float action_width = (panel_boundary.width - 20.0f - action_gap*3.0f)/4.0f;
    Rectangle open_project = {
        panel_boundary.x + 10.0f, panel_boundary.y + 50.0f,
        action_width, 36.0f,
    };
    Rectangle save_project_button = {
        open_project.x + action_width + action_gap, open_project.y,
        action_width, open_project.height,
    };
    Rectangle add_track = {
        save_project_button.x + action_width*2.0f + action_gap*2.0f, open_project.y,
        action_width, open_project.height,
    };
    Rectangle save_as_button = {
        save_project_button.x + action_width + action_gap, open_project.y,
        action_width, open_project.height,
    };
    if (text_button(UINT64_C(0x545241434B4F504E), open_project,
                    "Open project", false) & BS_CLICKED) {
        (void)open_project_dialog();
    }
    if (text_button(UINT64_C(0x545241434B534156), save_project_button,
                    "Save", false) & BS_CLICKED) {
        Track *track = current_track();
        if (lyric_editor_has_unsaved_draft(track)) {
            notice_push(UI_NOTICE_WARNING, "Lyric draft is not saved yet",
                        "Apply or discard the lyric edit before saving the project.",
                        NULL, false);
        } else {
            (void)save_project(track, true);
        }
    }
    if (text_button(UINT64_C(0x545241434B534153), save_as_button,
                    "Save As", false) & BS_CLICKED) {
        Track *track = current_track();
        if (lyric_editor_has_unsaved_draft(track)) {
            notice_push(UI_NOTICE_WARNING, "Lyric draft is not saved yet",
                        "Apply or discard the lyric edit before saving the project.",
                        NULL, false);
        } else {
            (void)save_project_as(track);
        }
    }
    if (text_button(UINT64_C(0x545241434B414444), add_track,
                    "Add audio", false) & BS_CLICKED) {
        const char *filters[] = {"*.wav", "*.ogg", "*.mp3", "*.qoa",
                                 "*.xm", "*.mod", "*.flac"};
        char *path = tinyfd_openFileDialog("Add audio", "./",
                                           NOB_ARRAY_LEN(filters), filters,
                                           "audio files", 0);
        if (path != NULL && !plug_load_track(path)) {
            notice_push(UI_NOTICE_ERROR, "Audio could not be loaded",
                        "The file is unsupported, corrupt, or unreadable.", path, true);
        }
    }
    DrawLine((int)panel_boundary.x, (int)(panel_boundary.y + header_height),
             (int)(panel_boundary.x + panel_boundary.width),
             (int)(panel_boundary.y + header_height), COLOR_UI_RULE);

    Vector2 mouse = GetMousePosition();

    float scroll_bar_width = panel_boundary.width*0.03;
    float item_size = panel_boundary.width*0.2;
    float visible_area_size = fmaxf(1.0f, panel_boundary.height - header_height);
    float entire_scrollable_area = item_size*p->tracks.count;

    static float panel_scroll = 0;
    static float panel_velocity = 0;
    panel_velocity *= 0.9;
    if (CheckCollisionPointRec(mouse, panel_boundary)) {
        panel_velocity += GetMouseWheelMove()*item_size*8;
    }
    panel_scroll -= panel_velocity*GetFrameTime();

    static bool scrolling = false;
    static float scrolling_mouse_offset = 0.0f;
    if (scrolling) {
        panel_scroll = (mouse.y - panel_boundary.y - header_height - scrolling_mouse_offset)/
                       visible_area_size*entire_scrollable_area;
    }

    float min_scroll = 0;
    if (panel_scroll < min_scroll) panel_scroll = min_scroll;
    float max_scroll = entire_scrollable_area - visible_area_size;
    if (max_scroll < 0) max_scroll = 0;
    if (panel_scroll > max_scroll) panel_scroll = max_scroll;
    float panel_padding = item_size*0.1;

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    for (size_t i = 0; i < p->tracks.count; ++i) {
        Rectangle item_boundary = {
            .x = panel_boundary.x + panel_padding,
            .y = i*item_size + panel_boundary.y + header_height + panel_padding - panel_scroll,
            .width = panel_boundary.width - panel_padding*2 - scroll_bar_width,
            .height = item_size - panel_padding*2,
        };
        Color color;
        if (((int) i != p->current_track)) {
            uint64_t item_id = djb2(id, &i, sizeof(i));

            int state = button_with_id(item_id, GetCollisionRec(panel_boundary, item_boundary));
            if (state & BS_HOVEROVER) {
                color = COLOR_TRACK_BUTTON_HOVEROVER;
            } else {
                color = COLOR_TRACK_BUTTON_BACKGROUND;
            }
            if (state & BS_CLICKED) {
                Track *track = current_track();
                if (!lyric_editor_allow_context_change(track)) continue;
                Track *next_track = &p->tracks.items[i];
                if (!scene_instance_select(&p->scene, next_track->base_scene,
                                           next_track->scene_seed)) {
                    notice_push(UI_NOTICE_ERROR, "Track scene could not be prepared",
                                "The current track remains active.",
                                next_track->file_path, true);
                    continue;
                }
                if (track) StopMusicStream(track->music);
                lyric_editor_clear_draft();
                set_active_render_config(next_track->render_config);
                p->current_track = i;
                start_preview_track(next_track);
                p->event_undo_available = false;
                p->clear_events_confirmation = false;
            }
        } else {
            color = COLOR_TRACK_BUTTON_SELECTED;
        }
        DrawRectangleRec(item_boundary, color);
        DrawRectangleLinesEx(item_boundary, 1.0f,
                             (int)i == p->current_track ? COLOR_ACCENT : COLOR_UI_RULE);
        Color label_color = (int)i == p->current_track ? WHITE : COLOR_UI_INK;

        const char *text = GetFileName(p->tracks.items[i].file_path);
        float fontSize = item_boundary.height*0.5;
        float text_padding = item_boundary.width*0.05;
        Vector2 size = MeasureTextEx(ui_font(), text, fontSize, 0);
        Vector2 position = {
            .x = item_boundary.x + text_padding,
            .y = item_boundary.y + item_boundary.height*0.5 - size.y*0.5,
        };
        // TODO: use SDF fonts
        // Label overflow scroll handler
        float max_width = item_boundary.width - text_padding*2;
        uint64_t item_id = djb2(id, &i, sizeof(i));
        int state = button_with_id(item_id, GetCollisionRec(panel_boundary, item_boundary));

        if ((size.x > max_width)) { // <-- Item needs ScissorMode
            BeginScissorMode(position.x, position.y, max_width, item_boundary.height);

            if (state & BS_HOVEROVER) { // <-- Current item is being hovered on and needs scrolling
                static float dt = 0;
                static uint64_t hovered_label_id = 0;
                static int px_shift = 0;
                static bool scroll_left = true;

                dt += GetFrameTime();
                if (item_id != hovered_label_id) { // <-- But it is not same as the last hovered item, so reset the shift
                    px_shift = 0;
                    scroll_left = true;
                    hovered_label_id = item_id;
                } else { // <-- it is same as the last hovered item, so count the shift
                    if (dt > TRACKLABEL_SCROLL_SECS) {
                        dt = 0.0f;
                        if ((abs(px_shift) >= size.x - max_width + 10) || (px_shift == 10)) { // <-- End of scroll (with 10 padding)
                            scroll_left = !scroll_left; // <-- flip direction
                        }
                        scroll_left ? --px_shift : ++px_shift;
                    }
                }
                position.x += px_shift; // <-- Apply the shift
            }
            track_label(ui_font(), text, position, fontSize, label_color);
            EndScissorMode();

        } else { // <-- No need for ScissorMode
            track_label(ui_font(), text, position, fontSize, label_color);
        }
    }

    // TODO: up and down clickable buttons on the scrollbar

    if (entire_scrollable_area > visible_area_size) { // Is scrolling needed
        float t = visible_area_size/entire_scrollable_area;
        float q = panel_scroll/entire_scrollable_area;
        Rectangle scroll_bar_area = {
            .x = panel_boundary.x + panel_boundary.width - scroll_bar_width,
            .y = panel_boundary.y + header_height,
            .width = scroll_bar_width,
            .height = visible_area_size,
        };
        // TODO: some sort of color for the scroll bar background
        //DrawRectangleRounded(scroll_bar_area, 0.8, 20, RED);
        Rectangle scroll_bar_boundary = {
            .x = panel_boundary.x + panel_boundary.width - scroll_bar_width,
            .y = panel_boundary.y + header_height + visible_area_size*q,
            .width = scroll_bar_width,
            .height = visible_area_size*t,
        };
        DrawRectangleRec(scroll_bar_boundary, COLOR_UI_RULE);

        if (scrolling) {
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                scrolling = false;
            }
        } else {
            if (CheckCollisionPointRec(mouse, scroll_bar_boundary)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    scrolling = true;
                    scrolling_mouse_offset = mouse.y - scroll_bar_boundary.y;
                }
            } else if (CheckCollisionPointRec(mouse, scroll_bar_area)) {
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                    if (mouse.y < scroll_bar_boundary.y) {
                        panel_velocity += item_size*16;
                    } else if (scroll_bar_boundary.y + scroll_bar_boundary.height < mouse.y){
                        panel_velocity += -item_size*16;
                    }
                }
            }
        }
    }

}

static void scene_browser(Rectangle boundary)
{
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawLineEx((Vector2){boundary.x, boundary.y},
               (Vector2){boundary.x + boundary.width, boundary.y},
               2.0f, ColorAlpha(COLOR_ACCENT, 0.45f));

    const float padding = 8.0f;
    const float header_height = 27.0f;
    float header_font = 18.0f;
    DrawTextEx(ui_font(), "SCENES", (Vector2){boundary.x + padding, boundary.y + 5.0f},
               header_font, 0.0f, COLOR_UI_INK);
    char status[64];
    Track *track = current_track();
    if (track != NULL && track->scene_switches.enabled) {
        snprintf(status, sizeof(status), "AUTO  |  %zu events",
                 combined_scene_events().count);
    } else {
        snprintf(status, sizeof(status), "%zu events", combined_scene_events().count);
    }
    Vector2 status_size = MeasureTextEx(ui_font(), status, 14.0f, 0.0f);
    DrawTextEx(ui_font(), status,
               (Vector2){boundary.x + boundary.width - status_size.x - padding,
                         boundary.y + 8.0f},
               14.0f, 0.0f, COLOR_UI_MUTED);

    const float footer_height = 36.0f;
    const float gap = 4.0f;
    const size_t columns = 2;
    const size_t rows = (COUNT_SCENES + columns - 1)/columns;
    float row_height = (boundary.height - header_height - footer_height - padding*2.0f
                      - gap*(rows - 1))/(float)rows;
    if (row_height > 38.0f) row_height = 38.0f;
    if (row_height < 30.0f) return;
    float column_width = (boundary.width - padding*2.0f - gap)/(float)columns;
    for (Scene_Id id = 0; id < COUNT_SCENES; ++id) {
        size_t column = (size_t)id%columns;
        size_t row_index = (size_t)id/columns;
        Rectangle row = {
            boundary.x + padding + column*(column_width + gap),
            boundary.y + header_height + row_index*(row_height + gap),
            column_width,
            row_height,
        };
        if (text_button(UINT64_C(0x5343454E45000000) + (uint64_t)id,
                        row, scene_name(id), p->scene.id == id) & BS_CLICKED) {
            if (scene_instance_select(&p->scene, id, scene_seed_for_track(track))) {
                if (track != NULL) {
                    track->base_scene = id;
                    track->scene_switches.enabled = false;
                    scene_switch_reset(&track->scene_switches);
                }
                mark_project_dirty(track);
            }
        }
    }

    Rectangle import_button = {
        boundary.x + padding,
        boundary.y + boundary.height - footer_height,
        boundary.width - padding*2.0f,
        footer_height - padding*0.5f,
    };
    Rectangle clear_button = {0};
    bool has_ascii_image = track != NULL &&
        ascii_art_grid_is_populated(track->ascii_columns, track->ascii_rows);
    if (has_ascii_image) {
        float clear_width = fminf(108.0f, import_button.width*0.38f);
        clear_button = (Rectangle){
            import_button.x + import_button.width - clear_width,
            import_button.y,
            clear_width,
            import_button.height,
        };
        import_button.width -= clear_width + gap;
    }
    if (text_button(UINT64_C(0x41534349494D504F), import_button,
                    "Import image -> ASCII", false) & BS_CLICKED) {
        const char *filters[] = {"*.png", "*.jpg", "*.jpeg", "*.bmp"};
        char *path = tinyfd_openFileDialog("Image for ASCII Field", "./",
                                           NOB_ARRAY_LEN(filters), filters,
                                           "image files", 0);
        if (path == NULL) return;
        if (!plug_load_ascii_image(path) || !plug_select_scene("ascii")) {
            notice_push(UI_NOTICE_ERROR, "Image could not be imported",
                        "Use a valid PNG, JPEG, or BMP image.", path, true);
        }
    }
    if (has_ascii_image &&
        (text_button(UINT64_C(0x4153434949434C52), clear_button,
                     "Clear image", false) & BS_CLICKED)) {
        if (ascii_art_grid_clear(track->ascii_cells, ASCII_GRID_MAX_CELLS,
                                 &track->ascii_columns, &track->ascii_rows)) {
            mark_project_dirty(track);
            notice_push(UI_NOTICE_INFO, "ASCII image cleared",
                        "The track can now be saved as a portable empty ASCII scene.",
                        NULL, false);
        }
    }
}

#define fullscreen_button(preview_boundary) \
    fullscreen_button_with_loc(__FILE__, __LINE__, preview_boundary)
static int fullscreen_button_with_loc(const char *file, int line, Rectangle fullscreen_button_boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, fullscreen_button_boundary);

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        fullscreen_button_boundary.x + fullscreen_button_boundary.width/2 - icon_size*scale/2,
        fullscreen_button_boundary.y + fullscreen_button_boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };
    size_t icon_index;
    if (!p->fullscreen) {
        if (!(state & BS_HOVEROVER)) {
            icon_index = 0;
        } else {
            icon_index = 1;
        }
    } else {
        if (!(state & BS_HOVEROVER)) {
            icon_index = 2;
        } else {
            icon_index = 3;
        }
    }
    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_FULLSCREEN], source, dest,
                   CLITERAL(Vector2){0}, 0, COLOR_UI_INK);

    if (p->fullscreen) {
        tooltip(fullscreen_button_boundary, "Collapse [F]", SIDE_TOP, false);
    } else {
        tooltip(fullscreen_button_boundary, "Expand [F]", SIDE_TOP, false);
    }

    return state;
}

static float slider_get_value(float x, float lox, float hix)
{
    if (x < lox) x = lox;
    if (x > hix) x = hix;
    x -= lox;
    x /= hix - lox;
    return x;
}

static bool horz_slider(Rectangle boundary, float *value, bool *dragging)
{
    bool updated = false;

    Vector2 mouse = GetMousePosition();

    Vector2 startPos = {
        .x = boundary.x + boundary.height/2,
        .y = boundary.y + boundary.height/2,
    };
    Vector2 endPos = {
        .x = boundary.x + boundary.width - boundary.height/2,
        .y = boundary.y + boundary.height/2,
    };
    Color color = COLOR_ACCENT;
    DrawLineEx(startPos, endPos, boundary.height*0.10, color);
    Vector2 center = {
        .x = startPos.x + (endPos.x - startPos.x)*(*value),
        .y = startPos.y,
    };
    float radius = boundary.height/4;
    {
        Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        SetShaderValue(p->circle, p->circle_radius_location, (float[1]){ 0.43f }, SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->circle, p->circle_power_location, (float[1]){ 2.0f }, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(p->circle);
        Rectangle source = {0, 0, 1, 1};
        Rectangle dest = { center.x - radius, center.y - radius, radius*2, radius*2 };
        Vector2 origin = {0};
        DrawTexturePro(texture, source, dest, origin, 0, color);
        EndShaderMode();
    }

    if (!*dragging) {
        if (CheckCollisionPointCircle(mouse, center, radius)) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                *dragging = true;
            }
        } else {
            if (CheckCollisionPointRec(mouse, boundary)) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    *value = slider_get_value(mouse.x, startPos.x, endPos.x);
                    updated = true;
                    *dragging = true;
                }
            }
        }
    } else {
        *value = slider_get_value(mouse.x, startPos.x, endPos.x);
        updated = true;

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            *dragging = false;
        }
    }
    return updated;
}

#define volume_slider(preview_boundary) \
    volume_slider_with_location(__FILE__, __LINE__, (preview_boundary))
static bool volume_slider_with_location(const char *file, int line, Rectangle volume_icon_boundary)
{
    Vector2 mouse = GetMousePosition();

    static int expanded = false;
    static bool dragging = false;
    static float saved_volume = 0.0f;

    Rectangle volume_slider_boundary = volume_icon_boundary;

    size_t expanded_slots = 6;
    if (expanded) volume_slider_boundary.width = expanded_slots*HUD_BUTTON_SIZE;

    expanded = dragging || CheckCollisionPointRec(mouse, volume_slider_boundary);

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        volume_slider_boundary.x + HUD_BUTTON_SIZE/2 - icon_size*scale/2,
        volume_slider_boundary.y + HUD_BUTTON_SIZE/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    // TODO: animate volume slider expansion
    float volume = GetMasterVolume();

    size_t icon_index;
    if (volume <= 0) {
        icon_index = 0;
    } else {
        size_t phases = 2;
        icon_index = volume*phases;
        if (icon_index >= phases) icon_index = phases - 1;
        icon_index += 1;
    }

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};

    DrawTexturePro(p->icon_textures[UI_ICON_VOLUME], source, dest,
                   CLITERAL(Vector2){0}, 0, COLOR_UI_INK);

    bool updated = false;

    if (expanded) {
        Rectangle slider_boundary = {
            .x = volume_slider_boundary.x + HUD_BUTTON_SIZE,
            .y = volume_slider_boundary.y,
            .width = (expanded_slots - 1)*HUD_BUTTON_SIZE,
            .height = HUD_BUTTON_SIZE,
        };
        updated = horz_slider(slider_boundary, &volume, &dragging);
        float mouse_wheel_step = 0.05;
        float wheel_delta = GetMouseWheelMove();
        volume += wheel_delta*mouse_wheel_step;
        if (volume < 0) volume = 0;
        if (volume > 1) volume = 1;
        SetMasterVolume(volume);

        tooltip(slider_boundary, TextFormat("Volume %d%%", (int)floorf(volume*100.0f)), SIDE_TOP, dragging);
    }

    // Toggle mute

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));
    int volume_icon_state = button_with_id(id, volume_icon_boundary);
    if (
        IsKeyPressed(KEY_TOGGLE_MUTE) ||
        (volume_icon_state & BS_CLICKED)
    ) {
        if (volume > 0) {
            saved_volume = volume;
            volume = 0;
        } else {
            volume = saved_volume;
        }
        SetMasterVolume(volume);
        updated = true;
    }

    if (volume <= 0.0) {
        tooltip(volume_icon_boundary, "Unmute [M]", SIDE_TOP, false);
    } else {
        tooltip(volume_icon_boundary, "Mute [M]", SIDE_TOP, false);
    }

    return dragging || updated;
}

static const char *notice_severity_label(Ui_Notice_Severity severity)
{
    switch (severity) {
    case UI_NOTICE_INFO: return "INFO";
    case UI_NOTICE_SUCCESS: return "DONE";
    case UI_NOTICE_WARNING: return "WARNING";
    case UI_NOTICE_ERROR: return "ERROR";
    case UI_NOTICE_SEVERITY_COUNT: break;
    }
    return "NOTICE";
}

static void notice_tray(Rectangle preview_boundary)
{
    (void)ui_notice_tick(&p->notices, GetFrameTime());
    const float width = fminf(390.0f, preview_boundary.width - 32.0f);
    const float height = 92.0f;
    const float gap = 10.0f;
    const float margin = 16.0f;
    const size_t visible_count = p->notices.count < 3 ? p->notices.count : 3;
    uint64_t dismiss_id = 0;
    for (size_t row = 0; row < visible_count; ++row) {
        size_t index = p->notices.count - 1 - row;
        const Ui_Notice *notice = &p->notices.notices[index];
        Rectangle card = {
            .x = preview_boundary.x + preview_boundary.width - width - margin,
            .y = preview_boundary.y + margin + row*(height + gap),
            .width = width,
            .height = height,
        };
        DrawRectangleRec(card, (Color){247, 247, 248, 248});
        DrawRectangleLinesEx(card, 1.0f, (Color){20, 20, 20, 255});
        DrawRectangle((int)card.x, (int)card.y, 5, (int)card.height, COLOR_ACCENT);
        DrawTextEx(ui_font(), notice_severity_label(notice->severity),
                   (Vector2){card.x + 16.0f, card.y + 11.0f}, 14.0f, 1.0f,
                   COLOR_ACCENT);
        DrawTextEx(ui_font(), notice->title,
                   (Vector2){card.x + 82.0f, card.y + 9.0f}, 18.0f, 1.0f,
                   (Color){20, 20, 20, 255});
        BeginScissorMode((int)card.x + 16, (int)card.y + 34,
                         (int)card.width - 102, (int)card.height - 40);
        DrawTextEx(ui_font(), notice->detail,
                   (Vector2){card.x + 16.0f, card.y + 38.0f}, 15.0f, 1.0f,
                   (Color){70, 70, 72, 255});
        if (notice->path[0] != '\0') {
            DrawTextEx(ui_font(), notice->path,
                       (Vector2){card.x + 16.0f, card.y + 62.0f}, 13.0f, 1.0f,
                       (Color){92, 92, 96, 255});
        }
        EndScissorMode();
        Rectangle dismiss = {
            card.x + card.width - 76.0f, card.y + card.height - 32.0f, 64.0f, 23.0f,
        };
        int state = button_with_id(UINT64_C(0x4E4F544943450000) ^ notice->id, dismiss);
        DrawRectangleLinesEx(dismiss, state & BS_HOVEROVER ? 2.0f : 1.0f,
                             (Color){20, 20, 20, 255});
        DrawTextEx(ui_font(), "Dismiss",
                   (Vector2){dismiss.x + 8.0f, dismiss.y + 5.0f}, 13.0f, 1.0f,
                   (Color){20, 20, 20, 255});
        if (state & BS_CLICKED) dismiss_id = notice->id;
    }
    if (dismiss_id != 0) (void)ui_notice_dismiss(&p->notices, dismiss_id);
}

#define play_button(track, boundary) \
    play_button_with_location(__FILE__, __LINE__, (track), (boundary))
static int play_button_with_location(const char *file, int line, Track *track, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);
    size_t icon_index = IsMusicStreamPlaying(track->music) ? 1 : 0;

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        boundary.x + boundary.width/2 - icon_size*scale/2,
        boundary.y + boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_PLAY], source, dest,
                   CLITERAL(Vector2){0}, 0, COLOR_UI_INK);

    if (IsMusicStreamPlaying(track->music)) {
        tooltip(boundary, "Pause [SPACE]", SIDE_TOP, false);
    } else {
        tooltip(boundary, "Play [SPACE]", SIDE_TOP, false);
    }

    return state;
}

#define render_button(boundary) \
    render_button_with_location(__FILE__, __LINE__, (boundary))
static int render_button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);
    size_t icon_index = 0;

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        boundary.x + boundary.width/2 - icon_size*scale/2,
        boundary.y + boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_RENDER], source, dest,
                   CLITERAL(Vector2){0}, 0, COLOR_UI_INK);

    tooltip(boundary, "Render [R]", SIDE_TOP, false);

    return state;
}

#ifdef MUSIALIZER_MICROPHONE
#define microphone_button(boundary) \
    microphone_button_with_location(__FILE__, __LINE__, (boundary))
static int microphone_button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);
    size_t icon_index = 0;

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        boundary.x + boundary.width/2 - icon_size*scale/2,
        boundary.y + boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_MICROPHONE], source, dest,
                   CLITERAL(Vector2){0}, 0, COLOR_UI_INK);

    tooltip(boundary, "Microphone [C]", SIDE_TOP, false);

    return state;
}
#endif // MUSIALIZER_MICROPHONE

static void toggle_track_playing(Track *track)
{
    if (IsMusicStreamPlaying(track->music)) {
        PauseMusicStream(track->music);
    } else {
        ResumeMusicStream(track->music);
    }
}

static void finish_rendering_track(Track *track);

static bool remove_render_staging_file(const char *path)
{
    if (path == NULL || path[0] == '\0') return true;
    errno = 0;
    if (remove(path) == 0 || errno == ENOENT) return true;
    TraceLog(LOG_ERROR, "RENDER: could not remove decoded audio %s: %s",
             path, strerror(errno));
    return false;
}

static void warn_render_staging_file_retained(const char *path)
{
    notice_push(UI_NOTICE_WARNING, "Temporary decoded audio was retained",
                "Remove this staging WAV after confirming no export process still uses it.",
                path, true);
}

static bool start_rendering_track_to(Track *track, const char *output_path)
{
    if (track == NULL || output_path == NULL || output_path[0] == '\0') return false;
    if (p->render_audio_path[0] != '\0') {
        if (remove_render_staging_file(p->render_audio_path)) {
            p->render_audio_path[0] = '\0';
        } else {
            warn_render_staging_file_retained(p->render_audio_path);
            return false;
        }
    }
    if (strlen(output_path) >= sizeof(p->render_output_path)) {
        notice_push(UI_NOTICE_ERROR, "Export path is too long",
                    "Choose a destination with a shorter absolute path.",
                    output_path, true);
        return false;
    }
    if (!IsFileExtension(output_path, ".mp4")) {
        notice_push(UI_NOTICE_ERROR, "Export path must end in .mp4",
                    "This build exports H.264/AAC MP4 video.", output_path, true);
        return false;
    }
    if (musi_project_existing_files_alias(track->file_path, output_path)) {
        notice_push(UI_NOTICE_ERROR, "Export path aliases the source audio",
                    "Choose another MP4 path. The source track was not modified.",
                    output_path, true);
        return false;
    }
    if (track->project_path[0] != '\0' &&
        musi_project_existing_files_alias(track->project_path, output_path)) {
        notice_push(UI_NOTICE_ERROR, "Export path aliases the project",
                    "Choose another MP4 path. The .musi project was not modified.",
                    output_path, true);
        return false;
    }
    Render_Export_Result config_result = render_export_config_validate(&p->render_config);
    if (config_result != RENDER_EXPORT_OK) {
        notice_push(UI_NOTICE_ERROR, "Export settings are invalid",
                    render_export_result_string(config_result), NULL, true);
        p->render_failed = true;
        return false;
    }

    float restore_position = GetMusicTimePlayed(track->music);
    bool restore_playing = IsMusicStreamPlaying(track->music);
    StopMusicStream(track->music);

    // TODO: LoadWave is pretty slow on big files
    Wave wave = LoadWave(track->file_path);
    if (!IsWaveValid(wave)) {
        start_preview_track(track);
        if (restore_position > 0.0f) SeekMusicStream(track->music, restore_position);
        if (!restore_playing) PauseMusicStream(track->music);
        notice_push(UI_NOTICE_ERROR, "Track could not be decoded for export",
                    "The source audio decoder rejected the file.", track->file_path, true);
        p->render_failed = true;
        return false;
    }
    float *wave_samples = LoadWaveSamples(wave);
    if (wave_samples == NULL) {
        UnloadWave(wave);
        start_preview_track(track);
        if (restore_position > 0.0f) SeekMusicStream(track->music, restore_position);
        if (!restore_playing) PauseMusicStream(track->music);
        notice_push(UI_NOTICE_ERROR, "Track could not be decoded for export",
                    "Decoded audio samples could not be allocated.", track->file_path, true);
        p->render_failed = true;
        return false;
    }
    uint64_t total_frames = 0;
    if (render_export_total_frames(wave.frameCount, wave.sampleRate,
                                   p->render_config.fps, &total_frames) !=
        RENDER_EXPORT_OK) {
        UnloadWaveSamples(wave_samples);
        UnloadWave(wave);
        start_preview_track(track);
        if (restore_position > 0.0f) SeekMusicStream(track->music, restore_position);
        if (!restore_playing) PauseMusicStream(track->music);
        notice_push(UI_NOTICE_ERROR, "Export timeline could not be created",
                    "The decoded duration or frame rate is outside supported bounds.",
                    NULL, true);
        p->render_failed = true;
        return false;
    }
    RenderTexture2D target = load_offline_render_target(&p->render_config);
    if (!IsRenderTextureValid(target) || !rlFramebufferComplete(target.id)) {
        if (target.id != 0) UnloadRenderTexture(target);
        UnloadWaveSamples(wave_samples);
        UnloadWave(wave);
        start_preview_track(track);
        if (restore_position > 0.0f) SeekMusicStream(track->music, restore_position);
        if (!restore_playing) PauseMusicStream(track->music);
        notice_push(UI_NOTICE_ERROR, "Export surface could not be created",
                    "Try a lower resolution or Balanced quality.", NULL, true);
        p->render_failed = true;
        return false;
    }
    Scene_Id scene_id = track->base_scene;
    if (!scene_instance_select(&p->scene, scene_id, track->scene_seed)) {
        UnloadRenderTexture(target);
        UnloadWaveSamples(wave_samples);
        UnloadWave(wave);
        start_preview_track(track);
        if (restore_position > 0.0f) SeekMusicStream(track->music, restore_position);
        if (!restore_playing) PauseMusicStream(track->music);
        notice_push(UI_NOTICE_ERROR, "Scene could not be reset for export",
                    "The selected scene could not allocate deterministic state.", NULL, true);
        p->render_failed = true;
        return false;
    }

    bool audio_path_ready = false;
    char decoded_audio_path[PLUG_RELOAD_PATH_CAPACITY];
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        p->render_job_nonce += 1;
        if (p->render_job_nonce == 0) p->render_job_nonce = 1;
        if (!render_export_decoded_audio_path(
                output_path, musi_project_process_id(), p->render_job_nonce,
                decoded_audio_path, sizeof(decoded_audio_path))) break;
        char *temporary_video_path = NULL;
        bool video_path_ready = render_export_temporary_path(
            output_path, musi_project_process_id(), p->render_job_nonce,
            &temporary_video_path) && !FileExists(temporary_video_path);
        free(temporary_video_path);
        if (!FileExists(decoded_audio_path) && video_path_ready) {
            audio_path_ready = true;
            break;
        }
    }
    if (!audio_path_ready || !ExportWave(wave, decoded_audio_path)) {
        bool staging_cleaned = !audio_path_ready ||
                               remove_render_staging_file(decoded_audio_path);
        UnloadRenderTexture(target);
        UnloadWaveSamples(wave_samples);
        UnloadWave(wave);
        start_preview_track(track);
        if (restore_position > 0.0f) SeekMusicStream(track->music, restore_position);
        if (!restore_playing) PauseMusicStream(track->music);
        notice_push(UI_NOTICE_ERROR, "Decoded export audio could not be staged",
                    "Choose a writable output directory. The source and previous destination were preserved.",
                    output_path, true);
        if (!staging_cleaned) {
            warn_render_staging_file_retained(decoded_audio_path);
        }
        p->render_failed = true;
        return false;
    }

    fft_clean();
    p->wave = wave;
    p->wave_cursor = 0;
    p->wave_samples = wave_samples;
    p->screen = target;
    p->render_total_frames = total_frames;
    p->render_restore_position = restore_position;
    p->render_restore_playing = restore_playing;
    p->render_started_at = GetTime();
    snprintf(p->render_output_path, sizeof(p->render_output_path), "%s", output_path);
    snprintf(p->render_audio_path, sizeof(p->render_audio_path), "%s",
             decoded_audio_path);
    analyzer_configure(p->wave.sampleRate, p->wave.channels);
    p->scene_frame_index = 0;
    p->scene_clock_initialized = false;
    scene_switch_reset(&track->scene_switches);
    p->ffmpeg = ffmpeg_start_rendering(output_path, &p->render_config,
                                       p->render_audio_path, total_frames,
                                       p->render_job_nonce);
    p->render_failed = p->ffmpeg == NULL;
    SetTargetFPS(0);
    p->rendering = true;
    p->cancel_rendering = false;
    p->render_finishing = false;
    p->export_panel_open = false;
    SetTraceLogLevel(LOG_WARNING);
    if (p->ffmpeg == NULL) {
        notice_push(UI_NOTICE_ERROR, "FFmpeg encoder could not start",
                    "Run the product doctor with `--require export`. The previous destination was preserved.",
                    output_path, true);
        finish_rendering_track(track);
        return false;
    }
    return true;
}

static void start_rendering_track(Track *track)
{
    char const * filter_params[] = { "*.mp4" };
    char suggested[PLUG_RELOAD_PATH_CAPACITY];
    const char *default_path = "./musializer-render.mp4";
    const char *export_scene_name = track->scene_switches.enabled ? "scene-plan" :
                                    scene_stable_name(track->base_scene);
    if (render_export_suggest_path(track->file_path, export_scene_name,
                                   &p->render_config, suggested, sizeof(suggested)) ==
        RENDER_EXPORT_OK) {
        default_path = suggested;
    }
    char *output_path = tinyfd_saveFileDialog("Export video", default_path,
                                               NOB_ARRAY_LEN(filter_params),
                                               filter_params, "MP4 video");
    if (output_path != NULL && !start_rendering_track_to(track, output_path)) {
        TraceLog(LOG_WARNING, "RENDER: could not start export to %s", output_path);
    }
}

MUSIALIZER_PLUG bool plug_start_render(const char *output_path)
{
    return start_rendering_track_to(current_track(), output_path);
}

MUSIALIZER_PLUG bool plug_configure_render(uint32_t width, uint32_t height,
                                           uint32_t fps, const char *quality_name)
{
    Render_Export_Config config = p->render_config;
    if ((width == 0) != (height == 0)) return false;
    if (width != 0) {
        config.width = width;
        config.height = height;
    }
    if (fps != 0) config.fps = fps;
    if (quality_name != NULL) {
        Render_Quality quality;
        if (strcmp(quality_name, "balanced") == 0) quality = RENDER_QUALITY_BALANCED;
        else if (strcmp(quality_name, "high") == 0) quality = RENDER_QUALITY_HIGH;
        else if (strcmp(quality_name, "master") == 0) quality = RENDER_QUALITY_MASTER;
        else return false;
        if (render_export_config_set_quality(&config, quality) != RENDER_EXPORT_OK) return false;
    }
    if (render_export_config_validate(&config) != RENDER_EXPORT_OK) return false;
    Track *track = current_track();
    if (track != NULL) track->render_config = config;
    set_active_render_config(config);
    mark_project_dirty(track);
    return true;
}

MUSIALIZER_PLUG bool plug_render_active(void)
{
    return p->rendering && p->ffmpeg != NULL;
}

MUSIALIZER_PLUG bool plug_render_failed(void)
{
    return p->render_failed;
}

MUSIALIZER_PLUG bool plug_confirm_close(void)
{
    // Existing named projects can be flushed without asking. A draft belongs
    // only to the active track and is never auto-promoted into canonical data.
    Track *active = current_track();
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *track = &p->tracks.items[i];
        if (track->project_dirty && track->project_path[0] != '\0' &&
            !(track == active && lyric_editor_has_unsaved_draft(track))) {
            (void)save_project(track, false);
        }
    }

    size_t dirty_projects = 0;
    for (size_t i = 0; i < p->tracks.count; ++i) {
        if (p->tracks.items[i].project_dirty) ++dirty_projects;
    }
    bool dirty_draft = lyric_editor_has_unsaved_draft(active);
    bool staged_suggestions = p->assist_candidate != NULL;
    bool analysis_running = p->assist_job_state == ASSIST_JOB_RUNNING;
    bool export_running = p->rendering;
    if (dirty_projects == 0 && !dirty_draft && !staged_suggestions &&
        !analysis_running && !export_running) return true;

    char message[512];
    snprintf(message, sizeof(message),
             "%zu open track project%s still have unsaved canonical edits.%s%s%s%s\n\nQuit, cancel active jobs, and discard any unsaved work?",
             dirty_projects, dirty_projects == 1 ? "" : "s",
             dirty_draft ? "\nThe active lyric draft has not been applied." : "",
             staged_suggestions ? "\nValidated Assist suggestions have not been applied." : "",
             analysis_running ? "\nAn external analysis job is still running and will be cancelled." : "",
             export_running ? "\nA video export is still running and will be cancelled." : "");
    return tinyfd_messageBox("Unsaved Musializer work", message,
                             "yesno", "warning", 0) == 1;
}

static void finish_rendering_track(Track *track)
{
    SetTraceLogLevel(LOG_INFO);
    if (IsWaveValid(p->wave)) UnloadWave(p->wave);
    if (p->wave_samples != NULL) UnloadWaveSamples(p->wave_samples);
    if (IsRenderTextureValid(p->screen)) UnloadRenderTexture(p->screen);
    if (p->render_audio_path[0] != '\0') {
        if (remove_render_staging_file(p->render_audio_path)) {
            p->render_audio_path[0] = '\0';
        } else {
            warn_render_staging_file_retained(p->render_audio_path);
        }
    }
    memset(&p->wave, 0, sizeof(p->wave));
    memset(&p->screen, 0, sizeof(p->screen));
    p->wave_samples = NULL;
    p->wave_cursor = 0;
    SetTargetFPS(PREVIEW_FPS);
    p->rendering = false;
    p->render_finishing = false;
    start_preview_track(track);
    if (p->render_restore_position > 0.0f) {
        SeekMusicStream(track->music, p->render_restore_position);
    }
    if (!p->render_restore_playing) PauseMusicStream(track->music);
}

#ifdef MUSIALIZER_MICROPHONE
static void start_capture(void)
{
    ma_result result = MA_SUCCESS;

    assert(!p->capturing);
    assert(!p->microphone_working);

    p->capturing = true;

    const char *recording_file_path = "recording.wav";

    drwav_data_format format = {0};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 2;
    format.sampleRate = 44100;
    format.bitsPerSample = 32;
    if (!drwav_init_file_write(&p->wav, recording_file_path, &format, NULL)) {
        TraceLog(LOG_ERROR, "DRWAVE: Failed to initialize output file %s", recording_file_path);
        return;
    }

    // TODO: let the user choose their mic
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 2;
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = ma_callback;
    deviceConfig.pUserData = NULL;
    result = ma_device_init(NULL, &deviceConfig, &p->microphone);
    if (result != MA_SUCCESS) {
        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to initialize capture device: %s", ma_result_description(result));
        drwav_uninit(&p->wav);
        return;
    }

    // sample_ring is SPSC. Ensure the raylib playback callback cannot become a
    // second producer while the microphone owns the producer side.
    Track *track = current_track();
    if (track != NULL) StopMusicStream(track->music);
    analyzer_configure(deviceConfig.sampleRate, deviceConfig.capture.channels);
    p->scene_frame_index = 0;

    result = ma_device_start(&p->microphone);
    if (result != MA_SUCCESS) {
        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to start device: %s", ma_result_description(result));
        ma_device_uninit(&p->microphone);
        drwav_uninit(&p->wav);
        return;
    }

    p->microphone_working = true;
}
#endif // MUSIALIZER_MICROPHONE

// TODO: adapt toolbar to narrow widths
static bool toolbar(Track *track, Rectangle boundary)
{
    bool interacted = false;
    int state = 0;

#ifdef MUSIALIZER_MICROPHONE
    size_t buttons_count = 5;
#else
    size_t buttons_count = 4;
#endif // MUSIALIZER_MICROPHONE

    if (boundary.width < HUD_BUTTON_SIZE*buttons_count) return interacted;

    DrawRectangleRec(boundary, COLOR_TRACK_PANEL_BACKGROUND);

    float x = boundary.x;

    state = play_button(track, (CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        toggle_track_playing(track);
    }

    state = render_button((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        if (lyric_editor_allow_context_change(track)) {
            p->export_panel_open = true;
            p->lyrics_editor_open = false;
            p->assist_panel_open = false;
            p->lyric_text_active = false;
            p->fullscreen = false;
        }
    }

#ifdef MUSIALIZER_MICROPHONE
    state = microphone_button((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        start_capture();
    }
#endif // MUSIALIZER_MICROPHONE

    // TODO: implement "add new track" button that uses tinyfiledialogs

    bool volume_slider_interacted = volume_slider((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    interacted = interacted || volume_slider_interacted;

    state = fullscreen_button((CLITERAL(Rectangle) {
        boundary.x + boundary.width - HUD_BUTTON_SIZE,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    if (state & BS_CLICKED) {
        interacted = true;
        p->fullscreen = !p->fullscreen;
    }

    return interacted;
}

static void preview_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    poll_assist_job();

    if (IsFileDropped()) {
        FilePathList droppedFiles = LoadDroppedFiles();
        // TODO: loading files synchronously like that actually blocks the UI thread
        // Maybe we should do that in a separate thread.
        for (size_t i = 0; i < droppedFiles.count; ++i) {
            const char *path = droppedFiles.paths[i];
            if (IsFileExtension(path, ".musi")) {
                if (!open_project_path(path)) {
                    TraceLog(LOG_WARNING, "PROJECT: could not open dropped project %s", path);
                }
                continue;
            }
            bool image = IsFileExtension(path, ".png") || IsFileExtension(path, ".jpg") ||
                         IsFileExtension(path, ".jpeg") || IsFileExtension(path, ".bmp");
            if (image) {
                if (!plug_load_ascii_image(path) || !plug_select_scene("ascii")) {
                    notice_push(UI_NOTICE_ERROR, "Image could not be imported",
                                "Use a valid PNG, JPEG, or BMP image.", path, true);
                } else if (current_track() == NULL) {
                    notice_push(UI_NOTICE_SUCCESS, "ASCII image staged",
                                "Open an audio track when you are ready to preview it.",
                                path, false);
                }
            } else if (!plug_load_track(path)) {
                notice_push(UI_NOTICE_ERROR, "Audio could not be loaded",
                            "The file is unsupported, corrupt, or unreadable.", path, true);
            }
        }
        UnloadDroppedFiles(droppedFiles);
    }

#ifdef MUSIALIZER_MICROPHONE
    if (IsKeyPressed(KEY_CAPTURE)) start_capture();
#endif // MUSIALIZER_MICROPHONE

    Track *track = current_track();
    if (track) { // The music is loaded and ready
        UpdateMusicStream(track->music);
        for (size_t i = 0; i < p->tracks.count; ++i) {
            poll_project_autosave(&p->tracks.items[i]);
        }

        bool control_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (control_down && IsKeyPressed(KEY_S)) {
            if (lyric_editor_has_unsaved_draft(track)) {
                notice_push(UI_NOTICE_WARNING, "Lyric draft is not saved yet",
                            "Apply or discard the lyric edit before saving the project.",
                            NULL, false);
            } else {
                bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) ||
                                  IsKeyDown(KEY_RIGHT_SHIFT);
                if (shift_down) (void)save_project_as(track);
                else (void)save_project(track, true);
            }
        }

        if (!p->lyric_text_active && IsKeyPressed(KEY_TOGGLE_PLAY)) {
            toggle_track_playing(track);
        }

        if (!p->lyric_text_active && IsKeyPressed(KEY_RENDER)) {
            if (lyric_editor_allow_context_change(track)) {
                p->export_panel_open = true;
                p->lyrics_editor_open = false;
                p->assist_panel_open = false;
                p->fullscreen = false;
            }
        }

        if (!p->lyric_text_active && IsKeyPressed(KEY_FULLSCREEN)) {
            p->fullscreen = !p->fullscreen;
        }

        update_transport_shortcuts(track);
        double scene_time = GetMusicTimePlayed(track->music);
        float scene_dt = scene_clock_delta(scene_time);
        AudioSpectrumView spectrum = fft_analyze(scene_dt);
        if (!p->lyric_text_active) update_scene_shortcuts();
        apply_auto_scene_switch(track, scene_time);

        float toolbar_height = HUD_BUTTON_SIZE;
        if (p->fullscreen) {
            // TODO: make timeline somehow visible in fullscreen mode (maybe miniversion of it on the toolbar)
            static float hud_timer = HUD_TIMER_SECS;

            Rectangle preview_boundary = {
                .x = 0,
                .y = 0,
                .width = w,
                .height = h,
            };

            if (hud_timer > 0.0) {
                hud_timer -= GetFrameTime();

                preview_boundary.height -= toolbar_height;
                bool interacted = toolbar(track, CLITERAL(Rectangle) {
                    .x = 0,
                    .y = preview_boundary.height,
                    .width = preview_boundary.width,
                    .height = toolbar_height,
                });

                if (interacted) hud_timer = HUD_TIMER_SECS;
            }

            Vector2 delta = GetMouseDelta();
            bool moved = fabsf(delta.x) + fabsf(delta.y) > 0.0;
            if (moved) hud_timer = HUD_TIMER_SECS;

            scene_render(preview_boundary, spectrum, scene_time, scene_dt);

#if 0
            // TODO: toggle track playing on right mouse click on the preview
            if (button(preview_boundary) & BS_CLICKED) {
                toggle_track_playing(track);
            }
#else
            (void) button_with_location;
#endif

            notice_tray(preview_boundary);
        } else {
            float tracks_panel_width = 320.0f;
            float timeline_height = (p->lyrics_editor_open || p->assist_panel_open ||
                                     p->export_panel_open) ?
                                    330.0f : 180.0f;
            if (timeline_height > h - toolbar_height - 180.0f) {
                timeline_height = fmaxf(150.0f, h - toolbar_height - 180.0f);
            }
            Rectangle preview_boundary = {
                .x = tracks_panel_width,
                .y = 0,
                .width = w - tracks_panel_width,
                .height = h - timeline_height - toolbar_height,
            };

#if 0
            // TODO: toggle track playing on right mouse click on the preview
            if (button(preview_boundary) & BS_CLICKED) {
                toggle_track_playing(track);
            }
#else
            (void) button_with_location;
#endif

            BeginScissorMode(preview_boundary.x, preview_boundary.y, preview_boundary.width, preview_boundary.height);
            scene_render(preview_boundary, spectrum, scene_time, scene_dt);
            notice_tray(preview_boundary);
            EndScissorMode();

            float sidebar_height = h - timeline_height;
            float scene_panel_height = fminf(292.0f, fmaxf(0.0f, sidebar_height - 120.0f));
            float tracks_panel_height = sidebar_height - scene_panel_height;
            tracks_panel((CLITERAL(Rectangle) {
                .x = 0,
                .y = 0,
                .width = tracks_panel_width,
                .height = tracks_panel_height,
            }));

            scene_browser((CLITERAL(Rectangle) {
                .x = 0,
                .y = tracks_panel_height,
                .width = tracks_panel_width,
                .height = scene_panel_height,
            }));

            timeline(CLITERAL(Rectangle) {
                .x = 0,
                .y = h - timeline_height,
                .width = w,
                .height = timeline_height,
            }, track);

            toolbar(track, CLITERAL(Rectangle) {
                .x = tracks_panel_width,
                .y = preview_boundary.height,
                .width = preview_boundary.width,
                .height = toolbar_height,
            });
        }
    } else {
        DrawRectangle(0, 0, w, h, COLOR_UI_SURFACE);
        DrawLine(32, 72, w - 32, 72, COLOR_UI_RULE);
        DrawLine((int)(w*0.72f), 32, (int)(w*0.72f), h - 32, COLOR_UI_RULE);
        DrawTextEx(ui_font(), "MUSIALIZER", (Vector2){32.0f, 30.0f},
                   24.0f, 2.0f, COLOR_UI_INK);
        DrawTextEx(ui_font(), "01", (Vector2){w - 150.0f, 82.0f},
                   84.0f, 1.0f, COLOR_ACCENT);

        float left = fmaxf(48.0f, w*0.10f);
        float top = fmaxf(120.0f, h*0.20f);
        DrawTextEx(ui_font(), "Turn one track into a",
                   (Vector2){left, top}, 38.0f, 1.0f, COLOR_UI_INK);
        DrawTextEx(ui_font(), "finished visual score.",
                   (Vector2){left, top + 46.0f}, 38.0f, 1.0f, COLOR_UI_INK);
        DrawTextEx(ui_font(),
                   "Open an audio file, choose a scene, refine timing, then export a deterministic MP4.",
                   (Vector2){left, top + 112.0f}, 17.0f, 1.0f, COLOR_UI_MUTED);

        Rectangle open = {left, top + 158.0f, 176.0f, 44.0f};
        if (text_button(UINT64_C(0x454D5054594F504E), open, "Open audio", true) & BS_CLICKED) {
            int allow_multiple_selects = 0; // TODO: enable multiple selects
            char const *filter_params[] = {"*.wav", "*.ogg", "*.mp3", "*.qoa", "*.xm", "*.mod", "*.flac"};
            char *input_path = tinyfd_openFileDialog("Open audio", "./",
                                                      NOB_ARRAY_LEN(filter_params),
                                                      filter_params, "audio files",
                                                      allow_multiple_selects);
            if (input_path && !plug_load_track(input_path)) {
                notice_push(UI_NOTICE_ERROR, "Audio could not be loaded",
                            "The file is unsupported, corrupt, or unreadable.",
                            input_path, true);
            }
        }
        Rectangle open_project = {open.x + open.width + 10.0f, open.y, 176.0f, open.height};
        if (text_button(UINT64_C(0x454D50545950524A), open_project,
                        "Open project", false) & BS_CLICKED) {
            (void)open_project_dialog();
        }
        DrawTextEx(ui_font(), "or drop audio anywhere in this window",
                   (Vector2){open.x, open.y + open.height + 14.0f},
                   15.0f, 1.0f, COLOR_UI_MUTED);

        float steps_y = top + 250.0f;
        DrawLine((int)left, (int)steps_y - 16, (int)(w*0.66f), (int)steps_y - 16,
                 COLOR_UI_RULE);
        const char *numbers[] = {"1", "2", "3"};
        const char *steps[] = {"Choose or automate scenes", "Edit lyrics and timing",
                               "Review settings and export"};
        for (size_t i = 0; i < 3; ++i) {
            float x = left + i*fminf(250.0f, (w*0.60f)/3.0f);
            DrawTextEx(ui_font(), numbers[i], (Vector2){x, steps_y},
                       28.0f, 1.0f, COLOR_ACCENT);
            DrawTextEx(ui_font(), steps[i], (Vector2){x, steps_y + 40.0f},
                       15.0f, 1.0f, COLOR_UI_INK);
        }
        DrawTextEx(ui_font(), "WAV  OGG  MP3  QOA  XM  MOD  FLAC",
                   (Vector2){32.0f, h - 48.0f}, 14.0f, 2.0f, COLOR_UI_MUTED);

        notice_tray((Rectangle){0, 0, (float)w, (float)h});
    }
}

#ifdef MUSIALIZER_MICROPHONE
static void capture_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    if (p->microphone_working) {
        if (IsKeyPressed(KEY_CAPTURE) || IsKeyPressed(KEY_ESCAPE)) {
            // Microphone is working, so it needs to be uninited
            ma_device_uninit(&p->microphone);
            analyzer_drain_realtime_samples();
            drwav_uninit(&p->wav);
            p->microphone_working = false;
            p->capturing = false;

            const char *recording_file_path = "recording.wav";
            if (!plug_load_track(recording_file_path)) {
                notice_push(UI_NOTICE_ERROR, "Recording could not be opened",
                            "The recorded WAV file is invalid or unreadable.",
                            recording_file_path, true);
            }
        }


        float scene_dt = GetFrameTime();
        AudioSpectrumView spectrum = fft_analyze(scene_dt);
        update_scene_shortcuts();
        scene_render(CLITERAL(Rectangle) {
            0, 0, GetScreenWidth(), GetScreenHeight()
        }, spectrum, GetTime(), scene_dt);
    } else {
        if (IsKeyPressed(KEY_ESCAPE)) {
            // Microphone is not working, so it does no need to be uninited
            p->capturing = false;
        }

        // TODO: report capture device error via the popup mechanism.
        const char *label = "Capture Device Error: Check the Logs";
        Color color = RED;
        int fontSize = p->font.baseSize;
        Vector2 size = MeasureTextEx(p->font, label, fontSize, 0);
        Vector2 position = {
            w/2 - size.x/2,
            h/2 - size.y/2,
        };
        DrawTextEx(p->font, label, position, fontSize, 0, color);

        label = "(Press ESC to Continue)";
        fontSize = p->font.baseSize*2/3;
        size = MeasureTextEx(p->font, label, fontSize, 0);
        position.x = w/2 - size.x/2,
        position.y = h/2 - size.y/2 + fontSize,
        DrawTextEx(p->font, label, position, fontSize, 0, color);
    }
}
#endif // MUSIALIZER_MICROPHONE

static void rendering_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    Track *track = current_track();
    NOB_ASSERT(track != NULL);
    if (p->ffmpeg == NULL) {
        p->render_failed = true;
        notice_push(UI_NOTICE_ERROR, "Export failed",
                    "FFmpeg could not start or stopped before the video was complete.",
                    p->render_output_path, true);
        finish_rendering_track(track);
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE) || p->cancel_rendering) {
        bool cancelled = ffmpeg_end_rendering(p->ffmpeg, true);
        p->ffmpeg = NULL;
        p->render_failed = !cancelled;
        notice_push(cancelled ? UI_NOTICE_INFO : UI_NOTICE_ERROR,
                    cancelled ? "Export cancelled" : "Export cancellation failed",
                    cancelled ? "The partial file was removed; any previous output is unchanged."
                              : "The encoder or staging cleanup did not finish cleanly; inspect the log for the retained path.",
                    p->render_output_path, !cancelled);
        finish_rendering_track(track);
        return;
    }

    if (p->scene_frame_index >= p->render_total_frames) {
        if (!p->render_finishing) {
            p->render_finishing = true;
        } else {
            bool succeeded = ffmpeg_end_rendering(p->ffmpeg, false);
            p->ffmpeg = NULL;
            p->render_failed = !succeeded;
            notice_push(succeeded ? UI_NOTICE_SUCCESS : UI_NOTICE_ERROR,
                        succeeded ? "Export complete" : "Export failed while finishing",
                        succeeded ? "The video was encoded and published transactionally."
                                  : "The previous destination was preserved.",
                        p->render_output_path, !succeeded);
            finish_rendering_track(track);
            return;
        }
    }

    double elapsed = fmax(0.0, GetTime() - p->render_started_at);
    double progress = p->render_total_frames > 0 ?
                      (double)p->scene_frame_index/p->render_total_frames : 0.0;
    if (progress > 1.0) progress = 1.0;
    double remaining = progress > 0.001 ? elapsed*(1.0 - progress)/progress : 0.0;
    const char *label = p->render_finishing ? "Finishing encoder" : "Exporting video";
    Vector2 title_size = MeasureTextEx(ui_font(), label, 34.0f, 1.0f);
    DrawTextEx(ui_font(), label,
               (Vector2){w/2.0f - title_size.x/2.0f, h/2.0f - 92.0f},
               34.0f, 1.0f, WHITE);

    char detail[256];
    if (p->render_finishing) {
        snprintf(detail, sizeof(detail), "%llu frames encoded  |  finalizing MP4",
                 (unsigned long long)p->render_total_frames);
    } else {
        snprintf(detail, sizeof(detail),
                 "%llu / %llu frames  |  %02u:%02u elapsed  |  about %02u:%02u remaining",
                 (unsigned long long)p->scene_frame_index,
                 (unsigned long long)p->render_total_frames,
                 (unsigned)(elapsed/60.0), (unsigned)fmod(elapsed, 60.0),
                 (unsigned)(remaining/60.0), (unsigned)fmod(remaining, 60.0));
    }
    Vector2 detail_size = MeasureTextEx(ui_font(), detail, 18.0f, 1.0f);
    DrawTextEx(ui_font(), detail,
               (Vector2){w/2.0f - detail_size.x/2.0f, h/2.0f - 43.0f},
               18.0f, 1.0f, ColorAlpha(WHITE, 0.72f));

    float bar_width = w*2.0f/3.0f;
    Rectangle bar_box = {w/2.0f - bar_width/2.0f, h/2.0f, bar_width, 14.0f};
    DrawRectangleRec((Rectangle){bar_box.x, bar_box.y,
                                 bar_box.width*(float)progress, bar_box.height},
                     COLOR_ACCENT);
    DrawRectangleLinesEx(bar_box, 1.0f, WHITE);
    BeginScissorMode((int)bar_box.x, (int)bar_box.y + 27, (int)bar_box.width, 24);
    DrawTextEx(ui_font(), p->render_output_path,
               (Vector2){bar_box.x, bar_box.y + 29.0f},
               14.0f, 1.0f, ColorAlpha(WHITE, 0.52f));
    EndScissorMode();

    Rectangle cancel = {w - 146.0f, 24.0f, 122.0f, 38.0f};
    if (!p->render_finishing &&
        (text_button(UINT64_C(0x52454E4445524341), cancel,
                     "Cancel export", false) & BS_CLICKED)) {
        p->cancel_rendering = true;
    }

    if (p->render_finishing) return;

    if (p->scene_frame_index > 0) {
        uint64_t next_cursor = 0;
        Render_Export_Result cursor_result = render_export_sample_cursor(
            p->scene_frame_index, p->wave.sampleRate, p->render_config.fps,
            p->wave.frameCount, &next_cursor);
        if (cursor_result != RENDER_EXPORT_OK || next_cursor < p->wave_cursor) {
            (void)ffmpeg_end_rendering(p->ffmpeg, true);
            p->ffmpeg = NULL;
            p->render_failed = true;
            notice_push(UI_NOTICE_ERROR, "Export timeline failed",
                        render_export_result_string(cursor_result),
                        p->render_output_path, true);
            finish_rendering_track(track);
            return;
        }
        size_t available = (size_t)(next_cursor - p->wave_cursor);
        if (available > 0) {
            float *samples = (float *)p->wave_samples;
            audio_analyzer_push_interleaved(
                &p->analyzer,
                samples + p->wave_cursor*p->wave.channels,
                available);
            p->wave_cursor = (size_t)next_cursor;
        }
    }

    float scene_dt = p->scene_frame_index == 0 ? 0.0f :
                     1.0f/(float)p->render_config.fps;
    AudioSpectrumView spectrum = {0};
    if (audio_analyzer_analyze(&p->analyzer, scene_dt)) {
        spectrum = audio_analyzer_spectrum(&p->analyzer);
    }
    double scene_time = (double)p->scene_frame_index/p->render_config.fps;
    apply_auto_scene_switch(track, scene_time);

    BeginTextureMode(p->screen);
    ClearBackground(COLOR_BACKGROUND);
    scene_render((Rectangle){0, 0, (float)p->screen.texture.width,
                             (float)p->screen.texture.height},
                 spectrum, scene_time, scene_dt);
    EndTextureMode();

    Image image = LoadImageFromTexture(p->screen.texture);
    if (image.width != (int)p->render_config.width ||
        image.height != (int)p->render_config.height) {
        ImageResize(&image, (int)p->render_config.width, (int)p->render_config.height);
    }
    if (!ffmpeg_send_frame_flipped(p->ffmpeg, image.data,
                                   (size_t)image.width, (size_t)image.height)) {
        // A transport failure must never publish whatever partial stream the
        // encoder happened to accept, even if that child exits with status 0.
        (void)ffmpeg_end_rendering(p->ffmpeg, true);
        p->ffmpeg = NULL;
        p->render_failed = true;
        notice_push(UI_NOTICE_ERROR, "Export failed while writing a frame",
                    "The encoder pipe closed; the previous destination was preserved.",
                    p->render_output_path, true);
        UnloadImage(image);
        finish_rendering_track(track);
        return;
    }
    UnloadImage(image);
}

static void load_assets(void)
{
    size_t data_size = 0;
    void *data = NULL;

    const char *ui_font_path = "./resources/fonts/SpaceGrotesk-Regular.otf";
    data = plug_load_resource(ui_font_path, &data_size);
    if (data != NULL && data_size <= INT_MAX) {
        int curated[CAPTION_FONT_CODEPOINT_LIMIT];
        int ui_codepoints[CAPTION_FONT_CODEPOINT_LIMIT];
        size_t curated_count = 0;
        size_t ui_count = 0;
        if (caption_font_codepoints(curated, NOB_ARRAY_LEN(curated),
                                    &curated_count) == CAPTION_FONT_OK) {
            for (size_t i = 0; i < curated_count; ++i) {
                if (ui_font_codepoint(curated[i])) {
                    ui_codepoints[ui_count++] = curated[i];
                }
            }
        }
        p->ui_font = LoadFontFromMemory(
            GetFileExtension(ui_font_path), data, (int)data_size, FONT_SIZE,
            ui_count > 0 ? ui_codepoints : NULL, (int)ui_count);
    }
    if (IsFontValid(p->ui_font)) {
        GenTextureMipmaps(&p->ui_font.texture);
        SetTextureFilter(p->ui_font.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        TraceLog(LOG_WARNING,
                 "FONT: Space Grotesk UI face unavailable; using raylib default");
        memset(&p->ui_font, 0, sizeof(p->ui_font));
    }
    plug_free_resource(data);

    const char *alegreya_path = "./resources/fonts/Alegreya-Regular.ttf";
    data = plug_load_resource(alegreya_path, &data_size);
        int codepoints[CAPTION_FONT_CODEPOINT_LIMIT];
        size_t codepoint_count = caption_font_codepoint_count();
        size_t codepoints_written = 0;
        Caption_Font_Result font_result = caption_font_codepoints(
            codepoints, NOB_ARRAY_LEN(codepoints), &codepoints_written);
        if (font_result == CAPTION_FONT_OK && codepoint_count == codepoints_written &&
            codepoints_written <= INT_MAX) {
            p->font = LoadFontFromMemory(
                GetFileExtension(alegreya_path), data, data_size, FONT_SIZE,
                codepoints, (int)codepoints_written);
        } else {
            TraceLog(LOG_WARNING,
                     "FONT: curated caption glyph set unavailable; using basic Latin");
            p->font = LoadFontFromMemory(GetFileExtension(alegreya_path), data,
                                         data_size, FONT_SIZE, NULL, 0);
        }
        GenTextureMipmaps(&p->font.texture);
        SetTextureFilter(p->font.texture, TEXTURE_FILTER_BILINEAR);
    plug_free_resource(data);

    // TODO: Maybe we should try to keep compiling different versions of shaders
    // until one of them works?
    //
    // If the shader can not be compiled maybe we could fallback to software rendering
    // of the texture of a fuzzy circle? The shader does not really do anything particularly
    // special.
    data = plug_load_resource(TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION), &data_size);
        p->circle = LoadShaderFromMemory(NULL, data);
        p->circle_radius_location = GetShaderLocation(p->circle, "radius");
        p->circle_power_location = GetShaderLocation(p->circle, "power");
    plug_free_resource(data);

    for (UI_Icon icon = 0; icon < COUNT_UI_ICONS; ++icon) {
        data = plug_load_resource(icon_file_paths[icon], &data_size);
            Image image = LoadImageFromMemory(GetFileExtension(icon_file_paths[icon]), data, data_size);
                p->icon_textures[icon] = LoadTextureFromImage(image);
                GenTextureMipmaps(&p->icon_textures[icon]);
                SetTextureFilter(p->icon_textures[icon], TEXTURE_FILTER_BILINEAR);
            UnloadImage(image);
        plug_free_resource(data);
    }
}

static void unload_assets(void)
{
    if (IsFontValid(p->ui_font)) UnloadFont(p->ui_font);
    UnloadFont(p->font);
    UnloadShader(p->circle);
    for (UI_Icon icon = 0; icon < COUNT_UI_ICONS; ++icon) {
        UnloadTexture(p->icon_textures[icon]);
    }
    memset(&p->ui_font, 0, sizeof(p->ui_font));
    memset(&p->font, 0, sizeof(p->font));
    memset(&p->circle, 0, sizeof(p->circle));
    memset(p->icon_textures, 0, sizeof(p->icon_textures));
}

static void release_reload_sensitive_resources(void)
{
    // A helper may still be executing code/files from this checkout. Stop it
    // before unloading the plug that owns its process handle.
    cancel_assist_job();
#ifdef MUSIALIZER_MICROPHONE
    // miniaudio retains ma_callback, so the device must be stopped before the
    // old shared object is unmapped.
    if (p->microphone_working) {
        ma_device_uninit(&p->microphone);
        analyzer_drain_realtime_samples();
        drwav_uninit(&p->wav);
        memset(&p->microphone, 0, sizeof(p->microphone));
        memset(&p->wav, 0, sizeof(p->wav));
        p->microphone_working = false;
        p->capturing = false;
    }
#endif

    // An encoder pipe/child cannot be resumed by code with a different ABI.
    // Cancel it deterministically while the implementation that created the
    // handle is still loaded.
    if (p->ffmpeg != NULL) {
        ffmpeg_end_rendering(p->ffmpeg, true);
        p->ffmpeg = NULL;
    }
    if (p->wave_samples != NULL) {
        UnloadWaveSamples(p->wave_samples);
        p->wave_samples = NULL;
    }
    if (IsWaveValid(p->wave)) UnloadWave(p->wave);
    memset(&p->wave, 0, sizeof(p->wave));
    p->wave_cursor = 0;
    if (p->render_audio_path[0] != '\0') {
        if (remove_render_staging_file(p->render_audio_path)) {
            p->render_audio_path[0] = '\0';
        } else {
            warn_render_staging_file_retained(p->render_audio_path);
        }
    }
    p->rendering = false;
    p->cancel_rendering = false;
    p->render_finishing = false;
    SetTargetFPS(PREVIEW_FPS);

    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
        UnloadMusicStream(track->music);
        memset(&track->music, 0, sizeof(track->music));
    }

    const Scene_Descriptor *descriptor = scene_descriptor(p->scene.id);
    if (descriptor != NULL && descriptor->unload != NULL) {
        // A scene-specific destructor is code from this shared object.  Run it
        // now; post-reload will recreate the selected scene from its id/seed.
        Scene_Id id = p->scene.id;
        uint64_t seed = p->scene.seed;
        scene_instance_unload(&p->scene);
        p->scene.id = id;
        p->scene.seed = seed;
        p->scene.state_version = descriptor->state_version;
        p->scene.state_size = descriptor->state_size;
    }

    if (IsRenderTextureValid(p->screen)) UnloadRenderTexture(p->screen);
    memset(&p->screen, 0, sizeof(p->screen));
    unload_assets();
    sample_ring_reset(&p->sample_ring);
}

static void free_rejected_reload_allocations(Plug_Reload_Handoff *handoff)
{
    if (handoff == NULL) return;
    if (handoff->owned_allocation_count <= handoff->owned_allocation_capacity) {
        for (size_t i = 0; i < handoff->owned_allocation_count; ++i) {
            free(handoff->owned_allocations[i]);
        }
    }
    free(handoff->owned_allocations);
    handoff->owned_allocations = NULL;
    handoff->owned_allocation_count = 0;
    handoff->owned_allocation_capacity = 0;
}

// Exact prefix emitted by checkpoint c842841. ABI 2 appends the bounded event
// snapshot, but must still be able to release and recover an in-flight ABI-1
// handoff created before the new shared object is loaded.
typedef struct Plug_Reload_Handoff_V1 {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t struct_size;
    void *opaque_state;
    uint64_t state_magic;
    uint32_t state_version;
    uint32_t reserved;
    size_t state_size;
    void **owned_allocations;
    size_t owned_allocation_count;
    size_t owned_allocation_capacity;
    char current_track_path[PLUG_RELOAD_PATH_CAPACITY];
    char scene_name[PLUG_RELOAD_SCENE_NAME_CAPACITY];
    float current_track_position;
    uint64_t scene_seed;
    bool current_track_was_playing;
} Plug_Reload_Handoff_V1;

static void restore_incompatible_handoff(Plug_Reload_Handoff *handoff,
                                         bool includes_events)
{
    char track_path[PLUG_RELOAD_PATH_CAPACITY];
    char selected_scene[PLUG_RELOAD_SCENE_NAME_CAPACITY];
    memcpy(track_path, handoff->current_track_path, sizeof(track_path));
    memcpy(selected_scene, handoff->scene_name, sizeof(selected_scene));
    track_path[sizeof(track_path) - 1] = '\0';
    selected_scene[sizeof(selected_scene) - 1] = '\0';
    float position = handoff->current_track_position;
    bool was_playing = handoff->current_track_was_playing;
    uint64_t seed = handoff->scene_seed;

    free_rejected_reload_allocations(handoff);
    plug_init();

    Scene_Id recovered_scene;
    if (selected_scene[0] != '\0' &&
        scene_id_from_name(selected_scene, &recovered_scene)) {
        scene_instance_select(&p->scene, recovered_scene, seed);
    }
    if (includes_events && handoff->event_count <= EVENT_TIMELINE_CAPACITY) {
        for (size_t i = 0; i < handoff->event_count; ++i) {
            if (!plug_record_event(handoff->events[i])) {
                TraceLog(LOG_WARNING, "HOTRELOAD: stopped restoring invalid event timeline");
                break;
            }
        }
    }
    if (track_path[0] != '\0' && plug_load_track(track_path)) {
        Track *track = current_track();
        if (position > 0.0f) SeekMusicStream(track->music, position);
        if (!was_playing) PauseMusicStream(track->music);
    }
    free(handoff);
}

static void restore_reloaded_tracks(float current_position, bool current_was_playing)
{
    size_t write_index = 0;
    int restored_current = -1;
    for (size_t read_index = 0; read_index < p->tracks.count; ++read_index) {
        Track *source = &p->tracks.items[read_index];
        Music music = LoadMusicStream(source->file_path);
        if (!IsMusicValid(music)) {
            TraceLog(LOG_WARNING, "HOTRELOAD: could not restore track %s", source->file_path);
            free(source->file_path);
            continue;
        }
        AttachAudioStreamProcessor(music.stream, callback);
        Track *destination = &p->tracks.items[write_index];
        if (destination != source) memmove(destination, source, sizeof(*destination));
        destination->music = music;
        if ((int)read_index == p->current_track) restored_current = (int)write_index;
        write_index += 1;
    }
    p->tracks.count = write_index;
    p->current_track = restored_current >= 0 ? restored_current : (write_index > 0 ? 0 : -1);

    Track *track = current_track();
    if (track != NULL) {
        start_preview_track(track);
        if (current_position > 0.0f) SeekMusicStream(track->music, current_position);
        if (!current_was_playing) PauseMusicStream(track->music);
    } else {
        analyzer_configure(48000, 2);
    }
}

MUSIALIZER_PLUG void plug_init(void)
{
    p = malloc(sizeof(*p));
    assert(p != NULL && "Buy more RAM lol");
    memset(p, 0, sizeof(*p));

    p->state_magic = PLUG_STATE_MAGIC;
    p->state_version = PLUG_STATE_VERSION;
    p->state_size = sizeof(*p);
    NOB_ASSERT(sample_ring_init(&p->sample_ring, p->sample_ring_storage, SAMPLE_RING_CAPACITY));
    event_timeline_init(&p->event_timeline);
    event_timeline_init(&p->event_undo);
    memset(&p->scene_events, 0, sizeof(p->scene_events));
    p->scene_events_track = -1;
    p->next_event_id = 1;
    p->assist_process = NOB_INVALID_PROC;
    p->assist_job_state = ASSIST_JOB_IDLE;
    ui_notice_queue_init(&p->notices);
    p->lyric_list_follow_selection = true;
    render_export_config_init(&p->render_config);
    p->render_resolution = RENDER_RESOLUTION_1080P;
    p->render_frame_rate = RENDER_FRAME_RATE_30;
    analyzer_configure(48000, 2);
    NOB_ASSERT(scene_instance_init(&p->scene, SCENE_SPECTRUM, UINT64_C(0x4D555349414C495A)));

    load_assets();
    p->current_track = -1;

    // TODO: restore master volume between sessions
    SetMasterVolume(0.5);
    SetTargetFPS(PREVIEW_FPS);
}

MUSIALIZER_PLUG void *plug_pre_reload(void)
{
    if (p == NULL) return NULL;

    const size_t allocation_count = 4 + p->tracks.count;
    Plug_Reload_Handoff *handoff = calloc(1, sizeof(*handoff));
    void **owned_allocations = calloc(allocation_count, sizeof(*owned_allocations));
    if (handoff != NULL && owned_allocations != NULL) {
        handoff->magic = PLUG_RELOAD_HANDOFF_MAGIC;
        handoff->abi_version = PLUG_RELOAD_HANDOFF_ABI_VERSION;
        handoff->struct_size = sizeof(*handoff);
        handoff->opaque_state = p;
        handoff->state_magic = p->state_magic;
        handoff->state_version = p->state_version;
        handoff->state_size = p->state_size;
        handoff->owned_allocations = owned_allocations;
        handoff->owned_allocation_capacity = allocation_count;
        Track *active_track = current_track();
        handoff->scene_seed = scene_seed_for_track(active_track);
        Event_Timeline_View events = active_track != NULL ?
                                     event_timeline_view(&active_track->manual_events) :
                                     event_timeline_view(&p->event_timeline);
        handoff->event_count = events.count;
        if (handoff->event_count > EVENT_TIMELINE_CAPACITY) {
            handoff->event_count = EVENT_TIMELINE_CAPACITY;
        }
        memcpy(handoff->events, events.events,
               handoff->event_count*sizeof(handoff->events[0]));
        const char *selected_scene = scene_stable_name(
            active_track != NULL ? active_track->base_scene : p->scene.id);
        snprintf(handoff->scene_name, sizeof(handoff->scene_name), "%s", selected_scene);

        Track *track = current_track();
        if (track != NULL) {
            snprintf(handoff->current_track_path, sizeof(handoff->current_track_path), "%s", track->file_path);
            handoff->current_track_position = GetMusicTimePlayed(track->music);
            handoff->current_track_was_playing = IsMusicStreamPlaying(track->music);
        }

        owned_allocations[handoff->owned_allocation_count++] = p;
        if (p->tracks.items != NULL) owned_allocations[handoff->owned_allocation_count++] = p->tracks.items;
        for (size_t i = 0; i < p->tracks.count; ++i) {
            if (p->tracks.items[i].file_path != NULL) {
                owned_allocations[handoff->owned_allocation_count++] = p->tracks.items[i].file_path;
            }
        }
        if (p->assist_candidate != NULL) {
            owned_allocations[handoff->owned_allocation_count++] = p->assist_candidate;
        }
        const Scene_Descriptor *descriptor = scene_descriptor(p->scene.id);
        if (p->scene.state != NULL && (descriptor == NULL || descriptor->unload == NULL)) {
            owned_allocations[handoff->owned_allocation_count++] = p->scene.state;
        }
    }

    release_reload_sensitive_resources();
    if (handoff == NULL || owned_allocations == NULL) {
        // Allocation failure is rare, but dlclose is still imminent.  Prefer a
        // clean fresh session over leaking resources or retaining callbacks.
        free(handoff);
        free(owned_allocations);
        for (size_t i = 0; i < p->tracks.count; ++i) free(p->tracks.items[i].file_path);
        free(p->tracks.items);
        free(p->assist_candidate);
        if (p->scene.state != NULL) scene_instance_unload(&p->scene);
        free(p);
        p = NULL;
        return NULL;
    }
    p = NULL;
    return handoff;
}

MUSIALIZER_PLUG void plug_post_reload(void *pp)
{
    Plug_Reload_Handoff *handoff = pp;
    if (handoff == NULL || handoff->magic != PLUG_RELOAD_HANDOFF_MAGIC) {
        // A pre-handoff plug cannot provide a layout-independent allocation
        // inventory.  This is the one unsupported upgrade edge: restart once
        // when crossing from such a development build.
        TraceLog(LOG_WARNING, "HOTRELOAD: legacy/invalid handoff; starting a fresh session");
        plug_init();
        return;
    }
    if (handoff->abi_version == 1u &&
        handoff->struct_size == sizeof(Plug_Reload_Handoff_V1)) {
        TraceLog(LOG_INFO, "HOTRELOAD: upgrading ABI-1 handoff");
        restore_incompatible_handoff(handoff, false);
        return;
    }
    if (handoff->abi_version != PLUG_RELOAD_HANDOFF_ABI_VERSION ||
        handoff->struct_size != sizeof(*handoff)) {
        // The recovery prefix through Plug_Reload_Handoff_V1 is deliberately
        // ABI-stable. A future writer may append fields, but must retain that
        // prefix while using this magic. Reject its opaque Plug layout while
        // still releasing the allocation inventory and restoring track/scene.
        if (handoff->struct_size >= sizeof(Plug_Reload_Handoff_V1) &&
            handoff->owned_allocations != NULL &&
            handoff->owned_allocation_count <= handoff->owned_allocation_capacity) {
            TraceLog(LOG_WARNING,
                     "HOTRELOAD: recovering the stable prefix of unknown ABI %u",
                     handoff->abi_version);
            restore_incompatible_handoff(handoff, false);
        } else {
            // A malformed/truncated envelope cannot be dereferenced safely.
            // This cannot be emitted by a conforming plug; retain a fresh
            // session instead of guessing at unknown offsets.
            TraceLog(LOG_ERROR,
                     "HOTRELOAD: malformed handoff envelope; starting fresh");
            free(handoff);
            plug_init();
        }
        return;
    }

    Plug *persisted = handoff->opaque_state;
    if (persisted == NULL ||
        handoff->state_magic != PLUG_STATE_MAGIC ||
        handoff->state_version != PLUG_STATE_VERSION ||
        handoff->state_size != sizeof(*persisted) ||
        persisted->state_magic != handoff->state_magic ||
        persisted->state_version != handoff->state_version ||
        persisted->state_size != handoff->state_size ||
        handoff->owned_allocations == NULL ||
        handoff->owned_allocation_count > handoff->owned_allocation_capacity) {
        TraceLog(LOG_WARNING, "HOTRELOAD: incompatible plug state; starting a fresh session");
        restore_incompatible_handoff(handoff, true);
        return;
    }

    p = persisted;
    Scene_Id selected_scene_id = p->scene.id;
    uint64_t selected_scene_seed = p->scene.seed;
    float current_position = handoff->current_track_position;
    bool current_was_playing = handoff->current_track_was_playing;
    free(handoff->owned_allocations);
    free(handoff);

    load_assets();
    if (!scene_instance_rebind(&p->scene)) {
        TraceLog(LOG_WARNING, "HOTRELOAD: scene state is incompatible; recreating selection");
        if (!scene_instance_init(&p->scene, selected_scene_id, selected_scene_seed)) {
            NOB_ASSERT(scene_instance_init(&p->scene, SCENE_SPECTRUM, UINT64_C(0x4D555349414C495A)));
        }
    }
    restore_reloaded_tracks(current_position, current_was_playing);
}

MUSIALIZER_PLUG void plug_shutdown(void)
{
    if (p == NULL) return;
    cancel_assist_job();
#ifdef MUSIALIZER_MICROPHONE
    if (p->microphone_working) {
        ma_device_uninit(&p->microphone);
        drwav_uninit(&p->wav);
    }
#endif
    if (p->rendering) {
        if (p->ffmpeg != NULL) ffmpeg_end_rendering(p->ffmpeg, true);
        if (IsWaveValid(p->wave)) UnloadWave(p->wave);
        if (p->wave_samples != NULL) UnloadWaveSamples(p->wave_samples);
        if (p->render_audio_path[0] != '\0') {
            (void)remove_render_staging_file(p->render_audio_path);
        }
    }
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
        UnloadMusicStream(track->music);
        free(track->file_path);
    }
    free(p->tracks.items);
    free(p->assist_candidate);
    scene_instance_unload(&p->scene);
    if (IsRenderTextureValid(p->screen)) UnloadRenderTexture(p->screen);
    unload_assets();
    free(p);
    p = NULL;
}

MUSIALIZER_PLUG void plug_update(void)
{
    BeginDrawing();
    ClearBackground(COLOR_BACKGROUND);

    begin_tooltip_frame();

    if (!p->rendering) { // We are in the Preview Mode
#ifdef MUSIALIZER_MICROPHONE
        if (p->capturing) {
            capture_screen();
        } else {
            preview_screen();
        }
#else
        preview_screen();
#endif // MUSIALIZER_MICROPHONE
    } else { // We are in the Rendering Mode
        rendering_screen();
    }

    end_tooltip_frame();

    // A pressed control can disappear before release (panel switch, expiring
    // notice, render transition). Individual controls get first chance to
    // consume the click; this fallback prevents the global capture ID from
    // disabling every future button.
    if (p->active_button_id != 0 && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        p->active_button_id = 0;
    }

    EndDrawing();
}

// TODO: About Page that includes current commit, version and the platforms
// We may also include licenses and contributors there.
// TODO: Actual Fullscreen Mode
// We do have fullscreen button, but apparently there is a demand on a fullscreen fullscreen mode.
// Raylib does have ToggleFullscreen(). Let's see how we can integrate it into the current UI/UX
// TODO: Adding Files by Ctrl+C, Ctrl+V
