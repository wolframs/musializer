#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#endif

#include "build/config.h"
#include "analysis_bridge.h"
#include "analysis_candidate.h"
#include "assist_ui_state.h"
#include "caption_layout.h"
#include "audio_analyzer.h"
#include "beat_tracker.h"
#include "editor_draft.h"
#include "plug.h"
#include "ffmpeg.h"
#include "lyrics.h"
#include "lyrics_editor_ui.h"
#include "project.h"
#include "project_io.h"
#include "sample_ring.h"
#include "scene.h"
#include "scene_event_merge.h"
#include "scene_switch.h"
#include "sha256.h"
#include "track_timeline.h"
#include "track.h"
#include "ui_notice.h"
#include "ui_theme.h"
#include "ui_widgets.h"
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
#define PREVIEW_FPS 60

#define PLUG_STATE_MAGIC UINT64_C(0x4D555349504C5547)
#define PLUG_STATE_VERSION 25

typedef enum {
    UI_ICON_FULLSCREEN,
    UI_ICON_VOLUME,
    UI_ICON_PLAY,
    UI_ICON_RENDER,
    UI_ICON_MICROPHONE,
    COUNT_UI_ICONS,
} UI_Icon;

static_assert(COUNT_UI_ICONS == 5, "Amount of icons changed");
static_assert((int)COUNT_SCENES == (int)SCENE_SETTINGS_SCENE_COUNT,
              "Scene settings registry must match the scene registry");
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
    Beat_Tracker beat_tracker;
    SampleRing sample_ring;
    SampleFrame sample_ring_storage[SAMPLE_RING_CAPACITY];
    bool timeline_scrubbing;
    bool timeline_scrub_restore_playing;
    double timeline_scrub_seconds;

    // Scene engine
    Scene_Instance scene;
    Scene_Settings scene_settings;
    bool scene_settings_open;
    bool scene_settings_window_expanded;
    int scene_settings_restore_width;
    int scene_settings_expanded_width;
    float scene_settings_scroll;
    Scene_Id scene_settings_scroll_scene;
    bool scene_settings_reset_confirmation;
    bool scene_settings_reset_undo_available;
    Scene_Id scene_settings_reset_scene;
    int scene_settings_reset_track;
    Scene_Settings_Snapshot scene_settings_reset_undo;
    uint64_t scene_settings_reset_notice_id;
    bool scene_preset_delete_confirmation;
    Scene_Id scene_preset_delete_scene;
    size_t scene_preset_delete_index;
    uint64_t scene_frame_index;
    double scene_previous_time;
    bool scene_clock_initialized;
    AsciiCell ascii_cells[ASCII_GRID_MAX_CELLS];
    size_t ascii_columns;
    size_t ascii_rows;
    char ascii_image_path[PLUG_RELOAD_PATH_CAPACITY];
    char ascii_image_sha256[SHA256_HEX_SIZE];
    Event_Timeline event_timeline;
    Event_Timeline event_undo;
    bool event_undo_available;
    bool clear_events_confirmation;
    uint64_t clear_events_notice_id;
    uint64_t next_event_id;
    Scene_Event_Merge scene_events;
    uint64_t scene_events_user_revision;
    uint64_t scene_events_semantic_revision;
    int scene_events_track;

    // Lyrics content/sync editor. The canonical document lives with each
    // track; this is the current UI draft state (hot-reload safe).
    bool lyrics_editor_open;
    Lyric_Editor lyric_editor;

    // Optional analysis helpers are child processes, never realtime work.
    bool assist_panel_open;
    Assist_Mode assist_mode;
    Assist_Job_State assist_job_state;
    Nob_Proc assist_process;
#ifdef _WIN32
    HANDLE assist_job_object;
#endif
    size_t assist_track_index;
    char assist_output_dir[PLUG_RELOAD_PATH_CAPACITY];
    char assist_bridge_path[PLUG_RELOAD_PATH_CAPACITY];
    char assist_log_path[PLUG_RELOAD_PATH_CAPACITY];
    uint64_t assist_job_nonce;
    double assist_started_at;
    double assist_cancel_started_at;
    bool assist_confirmation_pending;
    bool assist_apply_confirmation_pending;
    uint64_t assist_apply_notice_id;
    char assist_failure_detail[UI_NOTICE_DETAIL_CAPACITY];
    Analysis_Candidate *assist_candidate;
    size_t assist_candidate_track_index;
    Assist_Mode assist_candidate_mode;

    uint64_t active_button_id;

    Ui_Notice_Queue notices;

    Ui_Widgets widgets;

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
    beat_tracker_reset(&p->beat_tracker);
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
    beat_tracker_reset(&p->beat_tracker);
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
    track->cue_settings_active = false;
    // Fill both processed halves before the device starts consuming them. This
    // avoids making the first rendered frame race an initially silent stream.
    UpdateMusicStream(track->music);
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
    memset(&track->song_atlas_map, 0, sizeof(track->song_atlas_map));
    track->song_atlas_map_attempted = false;

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

static bool ensure_song_atlas_map(Track *track)
{
    if (track == NULL) return false;
    if (song_atlas_map_valid(&track->song_atlas_map)) return true;
    if (track->song_atlas_map_attempted) return false;
    track->song_atlas_map_attempted = true;

    bool resume_preview = IsMusicStreamPlaying(track->music);
    if (resume_preview) PauseMusicStream(track->music);
    Wave wave = LoadWave(track->file_path);
    float *samples = IsWaveValid(wave) ? LoadWaveSamples(wave) : NULL;
    bool prepared = samples != NULL && song_atlas_map_build(
        samples, (size_t)wave.frameCount, (size_t)wave.channels,
        wave.sampleRate, &track->song_atlas_map) > 0;
    if (samples != NULL) UnloadWaveSamples(samples);
    if (IsWaveValid(wave)) UnloadWave(wave);
    if (resume_preview) {
        UpdateMusicStream(track->music);
        ResumeMusicStream(track->music);
    }
    if (!prepared) {
        TraceLog(LOG_WARNING, "ATLAS: whole-song map could not be prepared for %s",
                 track->file_path);
    }
    return prepared;
}

static bool track_uses_song_atlas(const Track *track)
{
    if (track == NULL) return false;
    if (track->base_scene == SCENE_SONG_ATLAS) return true;
    if (!track->scene_switches.enabled) return false;
    for (size_t index = 0; index < track->scene_switches.count; ++index) {
        if (track->scene_switches.cues[index].scene_index ==
            (uint32_t)SCENE_SONG_ATLAS) return true;
    }
    return false;
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
    bool resume_active_preview = active_track != NULL &&
                                 IsMusicStreamPlaying(active_track->music);
    if (resume_active_preview) {
        // Loading another track performs whole-file decode, hashing, waveform
        // reduction, and atlas analysis. Pause intentionally across that
        // bounded synchronous preparation so the device never consumes an
        // underfilled stream and turns a UI load into audible crackle.
        PauseMusicStream(active_track->music);
    }
    Scene_Id initial_scene = active_track != NULL ? active_track->base_scene : p->scene.id;
    uint64_t initial_scene_seed = scene_seed_for_track(active_track);
    new_track->file_path = owned_path;
    new_track->music = music;
    new_track->duration_seconds = decoded_duration;
    new_track->transport_seekable =
        track_timeline_path_is_seekable(canonical_path);
    if (!sha256_file_hex(canonical_path, new_track->audio_sha256)) {
        // Saving can retry this non-fatal identity calculation, but doing it
        // before first playback normally keeps whole-file I/O out of autosave.
        TraceLog(LOG_WARNING, "AUDIO: source identity was deferred for %s",
                 canonical_path);
    }
    scene_switch_init(&new_track->scene_switches);
    event_timeline_init(&new_track->semantic_events);
    event_timeline_init(&new_track->manual_events);
    new_track->next_manual_event_id = 1;
    new_track->base_scene = initial_scene;
    new_track->previous_base_scene = initial_scene;
    new_track->scene_seed = initial_scene_seed;
    new_track->scene_instance_id = 1;
    scene_settings_init(&new_track->scene_settings);
    new_track->playback_scene_settings = new_track->scene_settings;
    scene_settings_preset_library_init(&new_track->scene_presets);
    new_track->render_config = p->render_config;
    load_timeline_waveform(new_track);
    if (resume_active_preview) {
        UpdateMusicStream(active_track->music);
        ResumeMusicStream(active_track->music);
    }
    if (p->ascii_columns > 0 && p->ascii_rows > 0) {
        memcpy(new_track->ascii_cells, p->ascii_cells,
               p->ascii_columns*p->ascii_rows*sizeof(p->ascii_cells[0]));
        new_track->ascii_columns = p->ascii_columns;
        new_track->ascii_rows = p->ascii_rows;
        snprintf(new_track->ascii_image_path,
                 sizeof(new_track->ascii_image_path), "%s",
                 p->ascii_image_path);
        snprintf(new_track->ascii_image_sha256,
                 sizeof(new_track->ascii_image_sha256), "%s",
                 p->ascii_image_sha256);
        p->ascii_columns = 0;
        p->ascii_rows = 0;
        p->ascii_image_path[0] = '\0';
        p->ascii_image_sha256[0] = '\0';
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

static bool load_ascii_image_grid(const char *file_path, AsciiCell *destination,
                                  size_t capacity, size_t *columns,
                                  size_t *rows)
{
    if (file_path == NULL || destination == NULL || columns == NULL ||
        rows == NULL || capacity < ASCII_GRID_MAX_CELLS) return false;
    Image image = LoadImage(file_path);
    if (!IsImageValid(image)) return false;
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    size_t staged_columns = 0;
    size_t staged_rows = 0;
    if (!ascii_art_fit_grid_dimensions(
            (size_t)image.width, (size_t)image.height,
            ASCII_GRID_MAX_COLUMNS, ASCII_GRID_MAX_ROWS,
            &staged_columns, &staged_rows)) {
        UnloadImage(image);
        return false;
    }
    bool converted = ascii_art_convert_rgba8(
        image.data,
        (size_t)image.width,
        (size_t)image.height,
        staged_columns,
        staged_rows,
        destination,
        capacity);
    UnloadImage(image);
    if (!converted) return false;
    *columns = staged_columns;
    *rows = staged_rows;
    return true;
}

MUSIALIZER_PLUG bool plug_load_ascii_image(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') return false;

    char canonical_path[PLUG_RELOAD_PATH_CAPACITY];
    char image_sha256[SHA256_HEX_SIZE];
    if (musi_project_canonicalize_existing_file(
            file_path, canonical_path, sizeof(canonical_path)) !=
            MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE ||
        !sha256_file_hex(canonical_path, image_sha256)) return false;

    Track *track = current_track();
    AsciiCell *destination = track != NULL ? track->ascii_cells : p->ascii_cells;
    size_t columns = 0;
    size_t rows = 0;
    if (!load_ascii_image_grid(canonical_path, destination,
                               ASCII_GRID_MAX_CELLS, &columns, &rows)) return false;

    if (track != NULL) {
        track->ascii_columns = columns;
        track->ascii_rows = rows;
        snprintf(track->ascii_image_path, sizeof(track->ascii_image_path),
                 "%s", canonical_path);
        snprintf(track->ascii_image_sha256,
                 sizeof(track->ascii_image_sha256), "%s", image_sha256);
        mark_project_dirty(track);
    } else {
        p->ascii_columns = columns;
        p->ascii_rows = rows;
        snprintf(p->ascii_image_path, sizeof(p->ascii_image_path),
                 "%s", canonical_path);
        snprintf(p->ascii_image_sha256, sizeof(p->ascii_image_sha256),
                 "%s", image_sha256);
    }
    TraceLog(LOG_INFO, "ASCII: imported %s as %zux%zu glyphs",
             canonical_path, columns, rows);
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
    } else if (strcmp(name, "cadence") == 0) {
        id = SCENE_CADENCE;
    } else if (strcmp(name, "loom") == 0) {
        id = SCENE_LOOM;
    } else if (strcmp(name, "pentagram") == 0 || strcmp(name, "pentagram-orbits") == 0) {
        id = SCENE_PENTAGRAM;
    } else {
        return false;
    }

    *result = id;
    return true;
}

static void track_select_base_scene(Track *track, Scene_Id scene)
{
    if (track == NULL) return;
    if (track->base_scene != scene) {
        track->previous_base_scene = track->base_scene;
        track->scene_selection_pending = true;
    }
    track->base_scene = scene;
    track->scene_switches.enabled = false;
    scene_switch_reset(&track->scene_switches);
    track->cue_settings_active = false;
}

MUSIALIZER_PLUG bool plug_select_scene(const char *name)
{
    Scene_Id id;
    if (!scene_id_from_name(name, &id)) return false;

    Track *track = current_track();
    bool selected = scene_instance_select(&p->scene, id, scene_seed_for_track(track));
    if (selected) {
        if (track != NULL) {
            track_select_base_scene(track, id);
        }
        mark_project_dirty(track);
    }
    return selected;
}

static void apply_auto_scene_switch(Track *track, double time_seconds)
{
    if (track == NULL) return;
    uint32_t scene_index = 0;
    Scene_Switch_Result result = scene_switch_update(
        &track->scene_switches, time_seconds, &scene_index);
    if (result != SCENE_SWITCH_OK) {
        if (!track->scene_switches.enabled) track->cue_settings_active = false;
        return;
    }
    Scene_Id scene = (Scene_Id)scene_index;
    if (scene >= COUNT_SCENES ||
        track->scene_switches.active_index >= track->scene_switches.count) return;
    const Scene_Switch_Cue *cue =
        &track->scene_switches.cues[track->scene_switches.active_index];
    track->playback_scene_settings = track->scene_settings;
    track->cue_settings_active = cue->settings.captured &&
        scene_settings_apply_snapshot(
            &track->playback_scene_settings, scene, &cue->settings);
    if (p->scene.id != scene) {
        scene_instance_select(&p->scene, scene, track->scene_seed);
    }
}

static Scene_Settings *track_effective_scene_settings(Track *track)
{
    if (track == NULL) return &p->scene_settings;
    return track->cue_settings_active ? &track->playback_scene_settings :
                                        &track->scene_settings;
}

static void capture_missing_scene_cue_settings(Track *track)
{
    if (track == NULL) return;
    for (size_t index = 0; index < track->scene_switches.count; ++index) {
        Scene_Switch_Cue *cue = &track->scene_switches.cues[index];
        if (!cue->settings.captured) {
            (void)scene_settings_capture(
                &track->scene_settings, cue->scene_index, &cue->settings);
        }
    }
}

static void commit_active_cue_settings(Track *track, Scene_Id scene)
{
    if (track == NULL || !track->cue_settings_active ||
        track->scene_switches.active_index >= track->scene_switches.count) return;
    Scene_Switch_Cue *cue =
        &track->scene_switches.cues[track->scene_switches.active_index];
    if (cue->scene_index == (uint32_t)scene) {
        (void)scene_settings_capture(
            &track->playback_scene_settings, scene, &cue->settings);
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

    bool onset = flux > 0.08f;
    float beat_phase = 0.0f;
    if (!beat_tracker_update(&p->beat_tracker, time_seconds, onset, flux,
                             &beat_phase)) {
        beat_tracker_reset(&p->beat_tracker);
    }

    return (Scene_Frame) {
        .time_seconds = time_seconds,
        .duration_seconds = track != NULL ? track->duration_seconds : 0.0,
        .delta_seconds = delta_seconds,
        .frame_index = p->scene_frame_index++,
        .settings = track_effective_scene_settings(track),
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
            .beat_phase = beat_phase,
            .onset = onset,
        },
    };
}

typedef Ui_Widgets_Caption_Measurement Caption_Raylib_Measurement;

static float caption_measure_raylib(const char *text, void *user_data)
{
    return ui_widgets_caption_measure_raylib(text, user_data);
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
    if (track != NULL && p->scene.id == SCENE_SONG_ATLAS) {
        (void)ensure_song_atlas_map(track);
    }
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
        .song_atlas_map = track != NULL ? &track->song_atlas_map : NULL,
        .settings = track_effective_scene_settings(track),
        .pixel_scale = pixel_scale,
    };
    scene_instance_update(&p->scene, &frame);
    scene_instance_draw(&p->scene, &frame, &renderer, boundary);
    if (p->scene.id != SCENE_CADENCE) {
        draw_scene_lyric_overlay(boundary, frame.lyric, renderer.font,
                                 renderer.pixel_scale);
    }
}

static uint64_t notice_push(Ui_Notice_Severity severity, const char *title,
                            const char *detail, const char *path, bool persistent);

static bool select_base_scene(Scene_Id selected)
{
    Track *track = current_track();
    if (selected < 0 || selected >= COUNT_SCENES || p->scene.id == selected) return false;
    if (!scene_instance_select(&p->scene, selected, scene_seed_for_track(track))) return false;
    if (track != NULL) track_select_base_scene(track, selected);
    mark_project_dirty(track);
    char detail[UI_NOTICE_DETAIL_CAPACITY];
    snprintf(detail, sizeof(detail), "%s is now the track's base scene.",
             scene_name(selected));
    notice_push(UI_NOTICE_INFO, "Base scene changed", detail, NULL, false);
    return true;
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
    if (IsKeyPressed(KEY_EIGHT)) selected = SCENE_CADENCE;
    if (IsKeyPressed(KEY_NINE)) selected = SCENE_LOOM;
    if (IsKeyPressed(KEY_ZERO)) selected = SCENE_PENTAGRAM;
    if (selected != COUNT_SCENES) (void)select_base_scene(selected);
}


static uint64_t notice_push(Ui_Notice_Severity severity, const char *title,
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
    uint64_t notice_id = 0;
    Ui_Notice_Result result = ui_notice_push(&p->notices, &spec, &notice_id);
    if (result != UI_NOTICE_OK && result != UI_NOTICE_DROPPED) {
        TraceLog(LOG_WARNING, "UI: could not queue notice: %s",
                 ui_notice_result_string(result));
    }
    return result == UI_NOTICE_OK ? notice_id : 0;
}

static void notice_dismiss(uint64_t *notice_id)
{
    if (notice_id == NULL || *notice_id == 0) return;
    (void)ui_notice_dismiss(&p->notices, *notice_id);
    *notice_id = 0;
}

static void begin_tooltip_frame(void)
{
    ui_widgets_begin_tooltip_frame(&p->widgets);
}

static void end_tooltip_frame(void)
{
    ui_widgets_end_tooltip_frame(&p->widgets, ui_font());
}

static void tooltip(Rectangle boundary, const char *text, Side align, bool persists)
{
    ui_widgets_tooltip(&p->widgets, boundary, text, align, persists);
}

static int button_with_id(uint64_t id, Rectangle boundary);
static bool start_assist_job(Assist_Mode mode, Track *track);
static uint32_t assist_mode_lanes(Assist_Mode mode);
static void poll_assist_job(void);
static void request_assist_job_cancel(void);
static bool request_assist_job_stop(Assist_Job_State stopping_state);
static bool cancel_assist_job_blocking(void);
static bool apply_assist_candidate(void);
static void discard_assist_candidate(void);
static void start_rendering_track(Track *track);
static bool save_project_as(Track *track);
static bool save_project(Track *track, bool show_success);
static bool open_project_dialog(void);
static void poll_project_autosave(Track *track);
static bool stage_candidate_analysis_lanes(
    const Track *track, const Analysis_Candidate *candidate,
    const char *path, bool imported_bridge,
    Musi_Analysis_Lane_Reference staged[MUSI_PROJECT_MAX_ANALYSIS_LANES],
    size_t *staged_count, char staged_audio_sha256[SHA256_HEX_SIZE]);

static int text_button(uint64_t id, Rectangle boundary, const char *label, bool selected)
{
    return ui_widgets_text_button(&p->active_button_id, ui_font(), id, boundary,
                                  label, selected);
}

static int danger_text_button(uint64_t id, Rectangle boundary,
                              const char *label, bool armed)
{
    return ui_widgets_danger_text_button(&p->active_button_id, ui_font(), id,
                                         boundary, label, armed);
}

static void disabled_text_button(Rectangle boundary, const char *label, bool selected)
{
    ui_widgets_disabled_text_button(ui_font(), boundary, label, selected);
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
    lyric_editor_ui_clear_draft(&p->lyric_editor);
}

static bool lyric_editor_has_unsaved_draft(const Track *track)
{
    return lyric_editor_ui_has_unsaved_draft(&p->lyric_editor, track);
}

static Lyric_Editor_Services lyric_editor_services(void)
{
    return (Lyric_Editor_Services){
        .notice_push = notice_push,
        .mark_project_dirty = mark_project_dirty,
        .font = ui_font,
        .active_button_id = &p->active_button_id,
    };
}

static bool lyric_editor_allow_context_change(Track *track)
{
    Lyric_Editor_Services services = lyric_editor_services();
    return lyric_editor_ui_allow_context_change(&p->lyric_editor, track, &services);
}

static void format_timestamp(double seconds, char *output, size_t capacity)
{
    ui_widgets_format_timestamp(seconds, output, capacity);
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
}

static void record_scene_cue(Track *track)
{
    if (track == NULL || p->scene.id >= COUNT_SCENES) return;
    double time_seconds = GetMusicTimePlayed(track->music);
    if (!isfinite(time_seconds) || time_seconds < 0.0 ||
        time_seconds >= track->duration_seconds) {
        notice_push(UI_NOTICE_WARNING, "Scene cue was not added",
                    "Move the playhead before the end of the track.", NULL, false);
        return;
    }
    Scene_Settings_Snapshot snapshot;
    Scene_Settings *settings = track_effective_scene_settings(track);
    if (!scene_settings_capture(settings, p->scene.id, &snapshot)) {
        notice_push(UI_NOTICE_ERROR, "Scene cue was not added",
                    "The selected scene settings are invalid.", NULL, true);
        return;
    }
    Scene_Switch_Timeline staged = track->scene_switches;
    Scene_Switch_Result result = SCENE_SWITCH_OK;
    if (staged.count == 0 && time_seconds > 0.001 &&
        track->scene_selection_pending &&
        track->previous_base_scene < COUNT_SCENES) {
        Scene_Settings_Snapshot previous_snapshot;
        if (!scene_settings_capture(&track->scene_settings,
                                    track->previous_base_scene,
                                    &previous_snapshot)) {
            result = SCENE_SWITCH_ERROR_CUE;
        } else {
            result = scene_switch_cue_at(
                &staged, 0.0, track->duration_seconds,
                (uint32_t)track->previous_base_scene, COUNT_SCENES,
                1.0f, &previous_snapshot);
        }
    }
    if (result == SCENE_SWITCH_OK) {
        result = scene_switch_cue_at(
            &staged, time_seconds, track->duration_seconds,
            (uint32_t)p->scene.id, COUNT_SCENES, 1.0f, &snapshot);
    }
    if (result != SCENE_SWITCH_OK) {
        notice_push(UI_NOTICE_ERROR, "Scene cue was not added",
                    scene_switch_result_string(result), NULL, true);
        return;
    }
    track->scene_switches = staged;
    track->scene_selection_pending = false;
    track->cue_settings_active = false;
    apply_auto_scene_switch(track, time_seconds);
    mark_project_dirty(track);
    notice_push(UI_NOTICE_SUCCESS, "Scene cue captured",
                "The scene and its current tuning will load at this playhead position.",
                NULL, false);
}

static void draw_lyric_lane(Rectangle lane, Track *track, float track_length)
{
    Lyric_Editor_Services services = lyric_editor_services();
    lyric_editor_ui_draw_lane(&p->lyric_editor, track, track_length, lane, ui_font(),
                           p->lyrics_editor_open, &services);
}

static void draw_lyrics_editor(Rectangle boundary, Track *track, double playhead)
{
    Lyric_Editor_Services services = lyric_editor_services();
    lyric_editor_ui_draw(&p->lyric_editor, track, playhead, boundary, &services);
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
    const float padding = UI_PANEL_PADDING;
    const float gap = UI_CONTROL_GAP;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);
    BeginScissorMode((int)boundary.x + 1, (int)boundary.y + 1,
                     (int)fmaxf(0.0f, boundary.width - 2.0f),
                     (int)fmaxf(0.0f, boundary.height - 2.0f));
    DrawTextEx(ui_font(), "ASSISTED ANALYSIS",
               (Vector2){boundary.x + padding, boundary.y + padding},
               UI_FONT_HEADER, 1.0f, signal);
    DrawTextEx(ui_font(),
               "Validated results stay staged until you apply them.",
               (Vector2){boundary.x + padding, boundary.y + 36.0f},
               UI_FONT_VALUE, 1.0f, COLOR_UI_MUTED);

    const Assist_Mode modes[] = {
        ASSIST_MODE_LYRICS, ASSIST_MODE_SECTIONS, ASSIST_MODE_MIMO, ASSIST_MODE_ALL,
    };
    static_assert(NOB_ARRAY_LEN(modes) == ASSIST_MODE_COUNT,
                  "Assist mode selector is incomplete");
    char helper_path[PLUG_RELOAD_PATH_CAPACITY];
    bool helpers_available = find_assist_helper(helper_path, sizeof(helper_path));
    bool active = assist_job_is_active(p->assist_job_state);
    Assist_Start_Block start_block = assist_start_block(
        helpers_available, p->assist_job_state, p->assist_candidate != NULL);
    Assist_Panel_Content content = assist_panel_content(
        p->assist_job_state, p->assist_confirmation_pending,
        p->assist_candidate != NULL);
    Assist_Ui_Layout layout = assist_ui_layout(boundary.width, content);
    float button_width = (boundary.width - padding*2.0f -
                          gap*(float)(layout.mode_columns - 1u))/
                         (float)layout.mode_columns;

    if (track->scene_switches.count > 0 && boundary.width >= 560.0f) {
        char auto_label[96];
        snprintf(auto_label, sizeof(auto_label), "Current auto scenes: %s (%zu)",
                 track->scene_switches.enabled ? "On" : "Off",
                 track->scene_switches.count);
        Rectangle toggle = {
            boundary.x + boundary.width - padding - 190.0f,
            boundary.y + 8.0f, 190.0f, UI_BUTTON_HEIGHT,
        };
        if (text_button(UINT64_C(0x4155544F5343454E), toggle, auto_label,
                        track->scene_switches.enabled) & BS_CLICKED) {
            track->scene_switches.enabled = !track->scene_switches.enabled;
            scene_switch_reset(&track->scene_switches);
            if (!track->scene_switches.enabled) {
                track->cue_settings_active = false;
                (void)scene_instance_select(&p->scene, track->base_scene,
                                            track->scene_seed);
            }
            mark_project_dirty(track);
        }
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(modes); ++i) {
        size_t row = i/layout.mode_columns;
        size_t column = i%layout.mode_columns;
        Rectangle button_boundary = {
            boundary.x + padding + (float)column*(button_width + gap),
            boundary.y + layout.mode_top + (float)row*layout.mode_row_height,
            button_width,
            UI_BUTTON_HEIGHT,
        };
        bool selected = (p->assist_candidate != NULL &&
                         p->assist_candidate_mode == modes[i]) ||
                        (active && p->assist_mode == modes[i]);
        bool armed = p->assist_confirmation_pending && !active &&
                     p->assist_mode == modes[i];
        int state = BS_NONE;
        if (start_block == ASSIST_START_ALLOWED) {
            state = text_button(UINT64_C(0x4153534953540000) + i,
                                button_boundary,
                                assist_mode_display_name(modes[i]), selected);
        } else {
            disabled_text_button(button_boundary,
                                 assist_mode_display_name(modes[i]), selected);
            tooltip(button_boundary, assist_start_block_reason(start_block),
                    SIDE_BOTTOM, false);
        }
        if (armed) DrawRectangleLinesEx(button_boundary, 2.0f, COLOR_UI_WARNING);
        DrawTextEx(ui_font(), assist_mode_badge(modes[i]),
                   (Vector2){button_boundary.x + 3.0f, button_boundary.y + 39.0f},
                   11.0f, 1.0f,
                   start_block == ASSIST_START_ALLOWED ? COLOR_UI_MUTED :
                   ColorAlpha(COLOR_UI_MUTED, 0.62f));
        if (state & BS_CLICKED) {
            p->assist_mode = modes[i];
            p->assist_confirmation_pending = true;
        }
    }

    float status_y = boundary.y + layout.status_y;
    char status[384];
    if (p->assist_candidate != NULL) {
        const char *track_name = p->assist_candidate_track_index < p->tracks.count ?
                                 GetFileName(p->tracks.items[p->assist_candidate_track_index].file_path) :
                                 "missing track";
        snprintf(status, sizeof(status), "%s result  |  Validated  |  %s",
                 assist_mode_display_name(p->assist_candidate_mode), track_name);
    } else if (p->assist_job_state == ASSIST_JOB_CANCELLING ||
               p->assist_job_state == ASSIST_JOB_TIMING_OUT ||
               p->assist_job_state == ASSIST_JOB_FAILING) {
        const char *track_name = p->assist_track_index < p->tracks.count ?
                                 GetFileName(p->tracks.items[p->assist_track_index].file_path) :
                                 "missing track";
        const char *action = p->assist_job_state == ASSIST_JOB_TIMING_OUT ?
                             "Stopping at the 40:00 job deadline" :
                             p->assist_job_state == ASSIST_JOB_FAILING ?
                             "Verifying process-tree cleanup" : "Cancelling";
        snprintf(status, sizeof(status), "%s %s  |  %s", action,
                 assist_mode_display_name(p->assist_mode), track_name);
    } else if (p->assist_job_state == ASSIST_JOB_RUNNING) {
        double elapsed = fmax(0.0, GetTime() - p->assist_started_at);
        const char *track_name = p->assist_track_index < p->tracks.count ?
                                 GetFileName(p->tracks.items[p->assist_track_index].file_path) :
                                 "missing track";
        snprintf(status, sizeof(status), "%s  |  %s  |  %02u:%02u elapsed",
                 assist_mode_display_name(p->assist_mode), track_name,
                 (unsigned)(elapsed/60.0), (unsigned)fmod(elapsed, 60.0));
    } else if (p->assist_confirmation_pending) {
        const char *setup_state = p->assist_job_state == ASSIST_JOB_FAILED ?
                                  "Last launch failed; review and retry" :
                                  "Review before starting";
        snprintf(status, sizeof(status), "%s  |  %s  |  %s%s",
                 assist_mode_display_name(p->assist_mode),
                 GetFileName(track->file_path),
                 setup_state,
                 helpers_available ? "" : "  |  Helper unavailable");
    } else if (!helpers_available) {
        snprintf(status, sizeof(status), "%s",
                 assist_start_block_reason(ASSIST_START_HELPER_UNAVAILABLE));
    } else if (p->assist_job_state == ASSIST_JOB_CANCELLED) {
        snprintf(status, sizeof(status),
                 "Analysis cancelled  |  Editor content unchanged");
    } else if (p->assist_job_state == ASSIST_JOB_TIMED_OUT) {
        snprintf(status, sizeof(status),
                 "40:00 job deadline reached  |  Editor content unchanged");
    } else if (p->assist_job_state == ASSIST_JOB_FAILED) {
        snprintf(status, sizeof(status), "Analysis failed  |  %.250s%s%s",
                 p->assist_failure_detail[0] != '\0' ? p->assist_failure_detail :
                 "The helper exited before producing a validated result.",
                 p->assist_log_path[0] != '\0' ? "  |  Log: " : "",
                 p->assist_log_path[0] != '\0' ? GetFileName(p->assist_log_path) : "");
    } else {
        snprintf(status, sizeof(status),
                 "Ready  |  Select a workflow to review its data boundary");
    }
    Color status_color = p->assist_job_state == ASSIST_JOB_FAILED ? COLOR_UI_DANGER :
                         p->assist_job_state == ASSIST_JOB_SUCCEEDED ||
                         p->assist_candidate != NULL ? COLOR_UI_SUCCESS :
                         p->assist_confirmation_pending ? COLOR_UI_WARNING :
                         active ? signal : COLOR_UI_INK;
    DrawTextEx(ui_font(), status, (Vector2){boundary.x + padding, status_y},
               16.0f, 1.0f, status_color);

    float action_y = boundary.y + layout.content_y;
    if (p->assist_confirmation_pending && !active && p->assist_candidate == NULL) {
        DrawTextEx(ui_font(), assist_mode_workflow(p->assist_mode),
                   (Vector2){boundary.x + padding, action_y},
                   14.0f, 1.0f, COLOR_UI_INK);
        DrawTextEx(ui_font(), assist_mode_data_boundary(p->assist_mode),
                   (Vector2){boundary.x + padding, action_y + 21.0f},
                   14.0f, 1.0f, COLOR_UI_MUTED);
        Rectangle start = {boundary.x + padding, action_y + 48.0f,
                           144.0f, UI_BUTTON_HEIGHT};
        Rectangle cancel = {start.x + start.width + gap, start.y, 94.0f, start.height};
        if (helpers_available) {
            if (text_button(UINT64_C(0x4153534953544346), start,
                            "Start analysis", false) & BS_CLICKED) {
                if (!start_assist_job(p->assist_mode, track)) {
                    notice_push(UI_NOTICE_ERROR, "Analysis could not start",
                                p->assist_failure_detail,
                                p->assist_log_path[0] != '\0' ? p->assist_log_path : NULL,
                                true);
                }
            }
        } else {
            disabled_text_button(start, "Start analysis", false);
            tooltip(start, assist_start_block_reason(
                        helpers_available ? start_block :
                        ASSIST_START_HELPER_UNAVAILABLE), SIDE_BOTTOM, false);
        }
        if (text_button(UINT64_C(0x4153534953544343), cancel, "Cancel", false) & BS_CLICKED) {
            p->assist_confirmation_pending = false;
        }
    } else if (p->assist_job_state == ASSIST_JOB_RUNNING) {
        DrawTextEx(ui_font(), assist_mode_workflow(p->assist_mode),
                   (Vector2){boundary.x + padding, action_y},
                   14.0f, 1.0f, COLOR_UI_INK);
        DrawTextEx(ui_font(),
                   "No percentage is reported. The complete job stops at 40:00; playback remains available.",
                   (Vector2){boundary.x + padding, action_y + 21.0f},
                   14.0f, 1.0f, COLOR_UI_MUTED);
        Rectangle cancel = {boundary.x + padding, action_y + 48.0f,
                            130.0f, UI_BUTTON_HEIGHT};
        if (text_button(UINT64_C(0x4153534953545354), cancel,
                        "Cancel job", false) & BS_CLICKED) {
            request_assist_job_cancel();
        }
    } else if (p->assist_job_state == ASSIST_JOB_CANCELLING ||
               p->assist_job_state == ASSIST_JOB_TIMING_OUT ||
               p->assist_job_state == ASSIST_JOB_FAILING) {
        DrawTextEx(ui_font(),
                   p->assist_job_state == ASSIST_JOB_TIMING_OUT ?
                       "The 40:00 job deadline was reached. Verifying that the complete process tree stopped." :
                       "Waiting for the helper and its child processes to stop. Editor content is unchanged.",
                   (Vector2){boundary.x + padding, action_y},
                   14.0f, 1.0f, COLOR_UI_MUTED);
    } else if (p->assist_candidate != NULL) {
        Analysis_Candidate *candidate = p->assist_candidate;
        Track *candidate_track = p->assist_candidate_track_index < p->tracks.count ?
                                 &p->tracks.items[p->assist_candidate_track_index] : NULL;
        float line_y = action_y;
        char line[256];
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0) {
            if (candidate->uncertain_lyric_count == 0) {
                snprintf(line, sizeof(line),
                         "Lyrics: %zu -> %zu  |  No timing cues flagged for review",
                         candidate_track != NULL ? candidate_track->lyrics.count : 0,
                         candidate->lyrics.count);
            } else {
                snprintf(line, sizeof(line),
                         "Lyrics: %zu -> %zu  |  %zu timing cue%s %s review",
                         candidate_track != NULL ? candidate_track->lyrics.count : 0,
                         candidate->lyrics.count, candidate->uncertain_lyric_count,
                         candidate->uncertain_lyric_count == 1 ? "" : "s",
                         candidate->uncertain_lyric_count == 1 ? "needs" : "need");
            }
            DrawTextEx(ui_font(), line, (Vector2){boundary.x + padding, line_y},
                       14.0f, 1.0f, COLOR_UI_INK);
            line_y += 18.0f;
        }
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0) {
            snprintf(line, sizeof(line), "Scene changes: %zu -> %zu",
                     candidate_track != NULL ? candidate_track->scene_switches.count : 0,
                     candidate->sections.count);
            DrawTextEx(ui_font(), line, (Vector2){boundary.x + padding, line_y},
                       14.0f, 1.0f, COLOR_UI_INK);
            line_y += 18.0f;
        }
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SEMANTICS) != 0) {
            snprintf(line, sizeof(line), "Feeling cues: %zu -> %zu",
                     candidate_track != NULL ? candidate_track->semantic_events.count : 0,
                     candidate->semantic_events.count);
            DrawTextEx(ui_font(), line, (Vector2){boundary.x + padding, line_y},
                       14.0f, 1.0f, COLOR_UI_INK);
            line_y += 18.0f;
        }
        if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0 &&
            candidate->lyrics.count > 0) {
            char first_lyric[256];
            snprintf(first_lyric, sizeof(first_lyric), "First staged lyric: %.180s",
                     candidate->lyrics.cues[0].text);
            DrawTextEx(ui_font(), first_lyric,
                       (Vector2){boundary.x + padding, line_y},
                       13.0f, 1.0f, COLOR_UI_MUTED);
        }
        Rectangle apply = {boundary.x + padding, action_y + 72.0f,
                           144.0f, UI_BUTTON_HEIGHT};
        Rectangle discard = {apply.x + apply.width + gap, apply.y, 100.0f, apply.height};
        bool draft_conflict = assist_candidate_conflicts_with_lyric_draft(
            (candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0,
            p->assist_candidate_track_index == (size_t)p->current_track,
            candidate_track != NULL && lyric_editor_has_unsaved_draft(candidate_track));
        bool apply_blocked = candidate_track == NULL || draft_conflict;
        if (apply_blocked) {
            disabled_text_button(apply,
                                 p->assist_apply_confirmation_pending ?
                                     "Confirm apply" : "Apply changes",
                                 p->assist_apply_confirmation_pending);
        } else if (text_button(UINT64_C(0x4153534953544150), apply,
                               p->assist_apply_confirmation_pending ?
                                   "Confirm apply" : "Apply changes",
                               p->assist_apply_confirmation_pending) & BS_CLICKED) {
            if (!p->assist_apply_confirmation_pending) {
                p->assist_apply_confirmation_pending = true;
                p->assist_apply_notice_id = notice_push(
                    UI_NOTICE_WARNING, "Confirm staged changes",
                    "Only the listed lanes will be replaced; unlisted editor content remains unchanged.",
                    NULL, true);
            } else {
                notice_dismiss(&p->assist_apply_notice_id);
                (void)apply_assist_candidate();
            }
        }
        if (text_button(UINT64_C(0x4153534953544449), discard,
                        "Discard", false) & BS_CLICKED) {
            notice_dismiss(&p->assist_apply_notice_id);
            discard_assist_candidate();
        }
        if (draft_conflict) {
            DrawTextEx(ui_font(), "Finish the active lyric draft before applying this result.",
                       (Vector2){discard.x + discard.width + gap, discard.y + 10.0f},
                       13.0f, 1.0f, COLOR_ACCENT);
        } else if (candidate_track == NULL) {
            DrawTextEx(ui_font(), "The target track is no longer available. Discard this result.",
                       (Vector2){discard.x + discard.width + gap, discard.y + 10.0f},
                       13.0f, 1.0f, COLOR_ACCENT);
        }
    }
    EndScissorMode();
}

static void draw_export_panel(Rectangle boundary, Track *track)
{
    const float padding = UI_PANEL_PADDING;
    const float gap = UI_CONTROL_GAP;
    const Color signal = COLOR_ACCENT;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);
    DrawTextEx(ui_font(), "EXPORT",
               (Vector2){boundary.x + padding, boundary.y + padding},
               UI_FONT_HEADER, 1.0f, signal);
    DrawTextEx(ui_font(),
               "One deterministic scene path. The destination is replaced only after the encoder succeeds.",
               (Vector2){boundary.x + padding, boundary.y + 36.0f},
               UI_FONT_VALUE, 1.0f, COLOR_UI_MUTED);

    float y = boundary.y + 62.0f;
    DrawTextEx(ui_font(), "SIZE", (Vector2){boundary.x + padding, y + 10.0f},
               13.0f, 1.0f, COLOR_UI_MUTED);
    float x = boundary.x + 68.0f;
    const float size_width = 76.0f;
    for (int i = 0; i < RENDER_RESOLUTION_COUNT; ++i) {
        Rectangle button = {x, y, size_width, UI_BUTTON_HEIGHT};
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
        Rectangle button = {x, y, 72.0f, UI_BUTTON_HEIGHT};
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
        Rectangle button = {x, y, 106.0f, UI_BUTTON_HEIGHT};
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
        boundary.y + boundary.height - 44.0f, 212.0f, UI_BUTTON_HEIGHT,
    };
    Rectangle close = {render.x - 98.0f - gap, render.y, 98.0f, render.height};
    if (ffmpeg_available()) {
        if (text_button(UINT64_C(0x4558504F5254474F), render,
                        "Choose output and render", true) & BS_CLICKED) {
            start_rendering_track(track);
        }
    } else {
        disabled_text_button(render, "FFmpeg required", false);
        tooltip(render,
                "Install FFmpeg, then run the product doctor with --require export",
                SIDE_TOP, false);
    }
    if (text_button(UINT64_C(0x4558504F5254434C), close, "Close", false) & BS_CLICKED) {
        p->export_panel_open = false;
    }
}

static void seek_track_to(Track *track, double seconds)
{
    if (track == NULL || !track->transport_seekable) return;
    double duration = track->duration_seconds;
    double target = track_timeline_seek_relative(0.0, seconds, duration);
    bool was_playing = IsMusicStreamPlaying(track->music);
    if (!was_playing) ResumeMusicStream(track->music);
    StopMusicStream(track->music);
    SeekMusicStream(track->music, (float)target);
    UpdateMusicStream(track->music);
    PlayMusicStream(track->music);
    if (!was_playing) PauseMusicStream(track->music);
    fft_clean();
    p->scene_clock_initialized = false;
    scene_switch_reset(&track->scene_switches);
    track->cue_settings_active = false;
}

static void seek_track_by(Track *track, double delta_seconds)
{
    if (track == NULL || !track->transport_seekable) return;
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
        float local_peak = fmaxf(fabsf(minimum), fabsf(maximum));
        Color waveform = ColorAlpha(
            ColorBrightness(COLOR_ACCENT, -0.18f + fminf(1.0f, local_peak)*0.24f),
            0.38f + fminf(1.0f, local_peak)*0.48f);
        DrawLineEx((Vector2){x, center - maximum*amplitude},
                   (Vector2){x, center - minimum*amplitude},
                   1.0f, waveform);
    }
}

static void update_transport_shortcuts(Track *track)
{
    if (track == NULL || !track->transport_seekable ||
        p->lyric_editor.text_active) return;
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

    float played = p->timeline_scrubbing ? (float)p->timeline_scrub_seconds :
                                           GetMusicTimePlayed(track->music);
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
    const char *labels[6] = {"Lyrics", "Assist", "Export", "+ Feel", "+ Scene", "+ Custom"};
    const char *control_tooltips[6] = {
        "Edit timed lyric content and synchronization",
        "Stage local or model-assisted analysis",
        "Configure and render a deterministic MP4",
        "Record a feeling marker at the playhead",
        "Cue this scene and its current tuning at the playhead",
        "Record a custom marker at the playhead",
    };
    const float control_widths[6] = {74.0f, 74.0f, 90.0f, 74.0f, 82.0f, 86.0f};
    const uint32_t types[6] = {
        EVENT_TYPE_LYRIC, EVENT_TYPE_LYRIC, EVENT_TYPE_LYRIC, EVENT_TYPE_SEMANTIC,
        EVENT_TYPE_CUE, EVENT_TYPE_CUSTOM
    };
    float control_x = controls.x;
    for (size_t i = 0; i < 6; ++i) {
        Rectangle boundary = {control_x, controls.y, control_widths[i], controls.height};
        int state = text_button(UINT64_C(0x45564E5400000000) + i, boundary, labels[i],
                                (i == 0 && p->lyrics_editor_open) ||
                                (i == 1 && p->assist_panel_open) ||
                                i == 2);
        Color category = i < 3 ? (Color){242, 190, 66, 255} :
                                 event_type_color(types[i]);
        DrawRectangle((int)boundary.x, (int)boundary.y, 4,
                      (int)boundary.height, category);
        DrawRectangleLinesEx(boundary, 2.0f,
                             ColorAlpha(i < 3 ? (Color){242, 190, 66, 255}
                                               : event_type_color(types[i]), 0.8f));
        tooltip(boundary, control_tooltips[i], SIDE_TOP, false);
        if (state & BS_CLICKED) {
            bool blocked = p->lyrics_editor_open &&
                           !lyric_editor_allow_context_change(track);
            if (!blocked && i == 0) {
                p->lyrics_editor_open = !p->lyrics_editor_open;
                if (p->lyrics_editor_open) p->assist_panel_open = false;
                if (p->lyrics_editor_open) p->export_panel_open = false;
                if (!p->lyrics_editor_open) p->lyric_editor.text_active = false;
            } else if (!blocked && i == 1) {
                p->assist_panel_open = !p->assist_panel_open;
                if (p->assist_panel_open) {
                    p->lyrics_editor_open = false;
                    p->export_panel_open = false;
                    p->lyric_editor.text_active = false;
                }
            } else if (!blocked && i == 2) {
                p->export_panel_open = !p->export_panel_open;
                if (p->export_panel_open) {
                    p->lyrics_editor_open = false;
                    p->assist_panel_open = false;
                    p->lyric_editor.text_active = false;
                }
            } else if (!blocked && i == 4) {
                record_scene_cue(track);
            } else if (!blocked) {
                record_timeline_event(track, types[i]);
            }
        }
        control_x += control_widths[i] + margin;
    }
    Rectangle clear_boundary = {control_x, controls.y, 112.0f, controls.height};
    const char *clear_label = p->event_undo_available ? "Undo clear" :
                              p->clear_events_confirmation ? "Confirm clear" :
                              "Clear manual";
    int clear_state = p->clear_events_confirmation ?
        danger_text_button(UINT64_C(0x45564E54FFFFFFFF), clear_boundary,
                           clear_label, true) :
        text_button(UINT64_C(0x45564E54FFFFFFFF), clear_boundary,
                    clear_label, false);
    if (clear_state & BS_CLICKED) {
        if (p->event_undo_available) {
            if (event_timeline_replace(&track->manual_events, &p->event_undo) ==
                EVENT_TIMELINE_OK) {
                p->event_undo_available = false;
                notice_dismiss(&p->clear_events_notice_id);
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
            notice_dismiss(&p->clear_events_notice_id);
            mark_project_dirty(track);
            notice_push(UI_NOTICE_SUCCESS, "Manual events cleared",
                        "Lyrics, semantic cues, and scene suggestions were not changed.",
                        NULL, false);
        } else {
            p->clear_events_confirmation = true;
            p->clear_events_notice_id = notice_push(
                UI_NOTICE_WARNING, "Confirm manual-event clear",
                "Click Confirm clear. Lyrics and imported lanes will remain.",
                NULL, true);
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
    const char *seek_labels[] = {"Start", "-1 s", "+1 s"};
    const double seek_deltas[] = {0.0, -1.0, 1.0};
    float seek_x = transport.x;
    for (size_t i = 0; i < NOB_ARRAY_LEN(seek_labels); ++i) {
        float width = i == 0 ? 62.0f : 58.0f;
        Rectangle button = {seek_x, transport.y, width, transport.height};
        if (track->transport_seekable) {
            if (text_button(UINT64_C(0x5345454B00000000) + i, button,
                            seek_labels[i], false) & BS_CLICKED) {
                if (i == 0) seek_track_to(track, 0.0);
                else seek_track_by(track, seek_deltas[i]);
                played = GetMusicTimePlayed(track->music);
            }
        } else {
            disabled_text_button(button, seek_labels[i], false);
        }
        seek_x += width + margin;
    }
    const char *shortcut = track->transport_seekable ?
        "Arrow keys: 1 s  |  Ctrl: 0.1 s  |  Shift: 10 s" :
        "Precise seeking is unavailable for module audio";
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
               1.25f, COLOR_TIMELINE_CURSOR);
    DrawTriangle((Vector2){x - 5.0f, waveform_lane.y},
                 (Vector2){x + 5.0f, waveform_lane.y},
                 (Vector2){x, waveform_lane.y + 7.0f},
                 COLOR_TIMELINE_CURSOR);
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
    if (track->transport_seekable && p->active_button_id == 0 &&
        CheckCollisionPointRec(mouse, waveform_lane) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        p->active_button_id = drag_id;
        p->timeline_scrubbing = true;
        p->timeline_scrub_restore_playing =
            IsMusicStreamPlaying(track->music);
        if (p->timeline_scrub_restore_playing) PauseMusicStream(track->music);
    }
    if (p->active_button_id == drag_id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            p->timeline_scrub_seconds = track_timeline_seek_from_x(
                GetMusicTimePlayed(track->music), mouse.x, waveform_lane.x,
                waveform_lane.width, len);
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            bool restore_playing = p->timeline_scrub_restore_playing;
            double target = p->timeline_scrub_seconds;
            p->timeline_scrubbing = false;
            p->timeline_scrub_restore_playing = false;
            seek_track_to(track, target);
            if (restore_playing) ResumeMusicStream(track->music);
            p->active_button_id = 0;
        }
    }

    // TODO: enable the user to render a specific region instead of the whole song.
}

static int button_with_id(uint64_t id, Rectangle boundary)
{
    return ui_widgets_button_with_id(&p->active_button_id, id, boundary);
}

#define DJB2_INIT UI_WIDGETS_DJB2_INIT

static uint64_t djb2(uint64_t hash, const void *buf, size_t buf_sz)
{
    return ui_widgets_djb2(hash, buf, buf_sz);
}

static bool assist_job_start_failed(const char *detail, bool log_available)
{
    p->assist_process = NOB_INVALID_PROC;
#ifdef _WIN32
    if (p->assist_job_object != NULL) {
        CloseHandle(p->assist_job_object);
        p->assist_job_object = NULL;
    }
#endif
    p->assist_job_state = ASSIST_JOB_FAILED;
    snprintf(p->assist_failure_detail, sizeof(p->assist_failure_detail), "%s",
             detail != NULL ? detail : "The analysis process could not be started.");
    if (!log_available) p->assist_log_path[0] = '\0';
    return false;
}

#ifdef _WIN32
static HANDLE create_assist_job_object(void)
{
    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job == NULL) return NULL;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        CloseHandle(job);
        return NULL;
    }
    return job;
}
#endif

static bool start_assist_job(Assist_Mode mode, Track *track)
{
    if (track == NULL || assist_job_is_active(p->assist_job_state) ||
        p->assist_candidate != NULL || assist_mode_lanes(mode) == 0) return false;
    size_t track_index = (size_t)(track - p->tracks.items);
    if (track_index >= p->tracks.count) return false;
    p->assist_bridge_path[0] = '\0';
    p->assist_log_path[0] = '\0';

    uint64_t path_hash = djb2(DJB2_INIT, track->file_path, strlen(track->file_path));
    path_hash = djb2(path_hash, &track->lyrics.duration_seconds,
                     sizeof(track->lyrics.duration_seconds));
    if (!nob_mkdir_if_not_exists("./build/analysis")) {
        return assist_job_start_failed(
            "The local analysis workspace could not be created. Check directory permissions.",
            false);
    }
    snprintf(p->assist_output_dir, sizeof(p->assist_output_dir),
             "./build/analysis/%016llx", (unsigned long long)path_hash);
    if (!nob_mkdir_if_not_exists(p->assist_output_dir)) {
        return assist_job_start_failed(
            "The track analysis workspace could not be created. Check directory permissions.",
            false);
    }
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
            (size_t)log_length >= sizeof(p->assist_log_path)) {
            return assist_job_start_failed(
                "The analysis artifact path is too long for this installation.", false);
        }
        if (!FileExists(p->assist_bridge_path) && !FileExists(p->assist_log_path)) {
            unique_artifacts = true;
            break;
        }
    }
    if (!unique_artifacts) return assist_job_start_failed(
        "A unique analysis artifact name could not be reserved.", false);

    char duration_text[64];
    snprintf(duration_text, sizeof(duration_text), "%.9f", track->lyrics.duration_seconds);
    char helper_path[PLUG_RELOAD_PATH_CAPACITY];
    if (!find_assist_helper(helper_path, sizeof(helper_path))) {
        return assist_job_start_failed(
            "The Assist helper script is missing from this installation.", false);
    }
    Nob_Cmd command = {0};
    Nob_Procs processes = {0};
#ifdef _WIN32
    HANDLE assist_job_object = create_assist_job_object();
    if (assist_job_object == NULL) {
        nob_cmd_free(command);
        return assist_job_start_failed(
            "Windows could not create a process group for the analysis job.", false);
    }
#endif
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
#ifdef _WIN32
    bool started = nob_cmd_run(&command, .async = &processes, .max_procs = 1,
                               .stderr_path = p->assist_log_path,
                               .win32_job_object = assist_job_object);
#else
    bool started = nob_cmd_run(&command, .async = &processes, .max_procs = 1,
                               .stderr_path = p->assist_log_path);
#endif
    nob_cmd_free(command);
    if (!started || processes.count != 1) {
#ifdef _WIN32
        CloseHandle(assist_job_object);
#endif
        nob_da_free(processes);
        return assist_job_start_failed(
            "Python could not launch the Assist helper. Review the job log for details.",
            FileExists(p->assist_log_path));
    }
    p->assist_process = processes.items[0];
#ifdef _WIN32
    p->assist_job_object = assist_job_object;
#endif
    nob_da_free(processes);
    p->assist_mode = mode;
    p->assist_track_index = track_index;
    p->assist_job_state = ASSIST_JOB_RUNNING;
    p->assist_failure_detail[0] = '\0';
    p->assist_started_at = GetTime();
    p->assist_cancel_started_at = 0.0;
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
    case ASSIST_MODE_COUNT: break;
    }
    return 0;
}

static void assist_candidate_failure(const char *title, const char *detail,
                                     const char *path)
{
    snprintf(p->assist_failure_detail, sizeof(p->assist_failure_detail), "%s",
             detail != NULL ? detail :
             "The analysis result could not be validated.");
    notice_push(UI_NOTICE_ERROR, title, p->assist_failure_detail, path, true);
}

static Analysis_Candidate *load_analysis_candidate_for_track(
    const char *path, size_t track_index, Assist_Mode mode)
{
    if (path == NULL || track_index >= p->tracks.count) return NULL;
    int file_size = GetFileLength(path);
    if (file_size <= 0 || (size_t)file_size > ANALYSIS_BRIDGE_INPUT_MAX_BYTES) {
        assist_candidate_failure(
            "Analysis result was rejected",
            "The bridge is empty or exceeds the 4 MiB input limit.", path);
        return NULL;
    }
    int input_size = 0;
    unsigned char *input = LoadFileData(path, &input_size);
    if (input == NULL || input_size <= 0) {
        if (input != NULL) UnloadFileData(input);
        assist_candidate_failure(
            "Analysis result could not be read",
            "The validated bridge file is unavailable.", path);
        return NULL;
    }
    Analysis_Bridge *bridge = malloc(sizeof(*bridge));
    if (bridge == NULL) {
        UnloadFileData(input);
        assist_candidate_failure(
            "Analysis result could not be staged",
            "There is not enough memory for the bounded bridge.", path);
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
        assist_candidate_failure(
            "Track identity could not be verified",
            "The source audio could not be hashed.", track->file_path);
        return NULL;
    }
    Analysis_Bridge_Result parsed = analysis_bridge_parse(
        bridge, (const char *)input, (size_t)input_size,
        expected_audio_sha256, 0);
    UnloadFileData(input);
    if (parsed != ANALYSIS_BRIDGE_OK) {
        TraceLog(LOG_WARNING, "ASSIST: rejected bridge: %s",
                 analysis_bridge_result_string(parsed));
        assist_candidate_failure("Analysis result was rejected",
                                 analysis_bridge_result_string(parsed), path);
        free(bridge);
        return NULL;
    }
    // MP3 container duration and raylib's decoded frame count may differ by a
    // small encoder-padding tail. SHA-256 establishes identity; accept only a
    // narrow timing discrepancy and use measured duration for imported lanes.
    double bridge_duration = (double)bridge->duration_ms/1000.0;
    if (fabs(bridge_duration - track->lyrics.duration_seconds) > 0.25) {
        TraceLog(LOG_WARNING, "ASSIST: bridge duration does not match the active track");
        assist_candidate_failure(
            "Analysis result was rejected",
            "The result duration does not match this track.", path);
        free(bridge);
        return NULL;
    }

    Analysis_Candidate *candidate = malloc(sizeof(*candidate));
    if (candidate == NULL) {
        free(bridge);
        assist_candidate_failure(
            "Analysis result could not be staged",
            "There is not enough memory for editable suggestions.", path);
        return NULL;
    }
    Analysis_Candidate_Result prepared = analysis_candidate_prepare(
        candidate, bridge, assist_mode_lanes(mode), bridge_duration, COUNT_SCENES);
    free(bridge);
    if (prepared != ANALYSIS_CANDIDATE_OK) {
        assist_candidate_failure("Analysis result was rejected",
                                 analysis_candidate_result_string(prepared), path);
        free(candidate);
        return NULL;
    }
    return candidate;
}

static bool stage_candidate_analysis_lanes(
    const Track *track, const Analysis_Candidate *candidate,
    const char *path, bool imported_bridge,
    Musi_Analysis_Lane_Reference staged[MUSI_PROJECT_MAX_ANALYSIS_LANES],
    size_t *staged_count, char staged_audio_sha256[SHA256_HEX_SIZE])
{
    if (track == NULL || candidate == NULL || path == NULL || path[0] == '\0' ||
        staged == NULL || staged_count == NULL || staged_audio_sha256 == NULL ||
        strlen(path) >= MUSI_PROJECT_PATH_CAPACITY ||
        track->analysis_lane_count > MUSI_PROJECT_MAX_ANALYSIS_LANES) return false;
    if (track->audio_sha256[0] != '\0') {
        snprintf(staged_audio_sha256, SHA256_HEX_SIZE, "%s",
                 track->audio_sha256);
    } else if (!sha256_file_hex(track->file_path, staged_audio_sha256)) {
        return false;
    }
    char artifact_sha256[SHA256_HEX_SIZE];
    if (!sha256_file_hex(path, artifact_sha256)) return false;
    memcpy(staged, track->analysis_lanes,
           track->analysis_lane_count*sizeof(staged[0]));
    *staged_count = track->analysis_lane_count;

    struct Lane_Request {
        uint32_t bit;
        Musi_Analysis_Lane_Kind kind;
        const char *model;
    } requests[] = {
        {ANALYSIS_CANDIDATE_LYRICS, MUSI_LANE_LYRIC_TIMING,
         imported_bridge ? "imported-bridge" : "whisper-codex"},
        {ANALYSIS_CANDIDATE_SECTIONS, MUSI_LANE_MEASURED_SIGNAL,
         imported_bridge ? "imported-bridge" : "measured-sections"},
        {ANALYSIS_CANDIDATE_SEMANTICS, MUSI_LANE_SEMANTIC_SCORE,
         imported_bridge ? "imported-bridge" : "xiaomi-mimo-v2.5"},
    };
    for (size_t request = 0; request < NOB_ARRAY_LEN(requests); ++request) {
        if ((candidate->available_lanes & requests[request].bit) == 0) continue;
        size_t index = *staged_count;
        for (size_t existing = 0; existing < *staged_count; ++existing) {
            if (staged[existing].kind == requests[request].kind) {
                index = existing;
                break;
            }
        }
        if (index == *staged_count) {
            if (*staged_count >= MUSI_PROJECT_MAX_ANALYSIS_LANES) return false;
            ++*staged_count;
        }
        Musi_Analysis_Lane_Reference *lane = &staged[index];
        memset(lane, 0, sizeof(*lane));
        lane->kind = requests[request].kind;
        snprintf(lane->path, sizeof(lane->path), "%s", path);
        snprintf(lane->sha256, sizeof(lane->sha256), "%s", artifact_sha256);
        snprintf(lane->audio_sha256, sizeof(lane->audio_sha256), "%s",
                 staged_audio_sha256);
        snprintf(lane->provenance.adapter, sizeof(lane->provenance.adapter),
                 "external-analysis");
        snprintf(lane->provenance.adapter_version,
                 sizeof(lane->provenance.adapter_version), "1");
        snprintf(lane->provenance.schema_version,
                 sizeof(lane->provenance.schema_version), "analysis-bridge-v1");
        snprintf(lane->provenance.model, sizeof(lane->provenance.model), "%s",
                 requests[request].model);
        snprintf(lane->provenance.prompt_version,
                 sizeof(lane->provenance.prompt_version), "v1");
    }
    return true;
}

static bool apply_candidate_to_track(Analysis_Candidate *candidate, size_t track_index)
{
    if (candidate == NULL || track_index >= p->tracks.count) return false;
    Track *track = &p->tracks.items[track_index];
    if (assist_candidate_conflicts_with_lyric_draft(
            (candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0,
            track == current_track(), lyric_editor_has_unsaved_draft(track))) {
        notice_push(UI_NOTICE_WARNING, "Suggestions were not applied",
                    "Apply or discard the active lyric draft before replacing the lyric lane.",
                    NULL, false);
        return false;
    }
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
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0) {
        capture_missing_scene_cue_settings(track);
        track->cue_settings_active = false;
        track->scene_selection_pending = false;
        scene_switch_reset(&track->scene_switches);
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
    if (track == NULL) {
        notice_push(UI_NOTICE_ERROR, "Suggestions were not applied",
                    "The track targeted by this staged result is no longer available.",
                    p->assist_bridge_path, true);
        return false;
    }
    Musi_Analysis_Lane_Reference staged[MUSI_PROJECT_MAX_ANALYSIS_LANES];
    size_t staged_count = 0;
    char staged_audio_sha256[SHA256_HEX_SIZE];
    if (!stage_candidate_analysis_lanes(
            track, p->assist_candidate, p->assist_bridge_path, false,
            staged, &staged_count, staged_audio_sha256)) {
        notice_push(UI_NOTICE_ERROR, "Suggestions were not applied",
                    "The evidence artifact or its bounded provenance could not be verified.",
                    p->assist_bridge_path, true);
        return false;
    }
    if (!apply_candidate_to_track(p->assist_candidate,
                                  p->assist_candidate_track_index)) return false;
    memcpy(track->analysis_lanes, staged,
           staged_count*sizeof(track->analysis_lanes[0]));
    track->analysis_lane_count = staged_count;
    snprintf(track->audio_sha256, sizeof(track->audio_sha256), "%s",
             staged_audio_sha256);
    free(p->assist_candidate);
    p->assist_candidate = NULL;
    notice_dismiss(&p->assist_apply_notice_id);
    p->assist_confirmation_pending = false;
    p->assist_apply_confirmation_pending = false;
    p->assist_job_state = ASSIST_JOB_IDLE;
    char detail[UI_NOTICE_DETAIL_CAPACITY];
    snprintf(detail, sizeof(detail),
             "The selected validated lanes are now in %s.",
             GetFileName(track->file_path));
    notice_push(UI_NOTICE_SUCCESS, "Suggestions applied",
                detail, NULL, false);
    return true;
}

static void discard_assist_candidate(void)
{
    if (p->assist_candidate == NULL) return;
    notice_dismiss(&p->assist_apply_notice_id);
    const char *track_name = p->assist_candidate_track_index < p->tracks.count ?
                             GetFileName(p->tracks.items[p->assist_candidate_track_index].file_path) :
                             "the missing target track";
    char detail[UI_NOTICE_DETAIL_CAPACITY];
    snprintf(detail, sizeof(detail),
             "The staged result for %s was discarded. Editor content is unchanged.",
             track_name);
    free(p->assist_candidate);
    p->assist_candidate = NULL;
    p->assist_confirmation_pending = false;
    p->assist_apply_confirmation_pending = false;
    p->assist_job_state = ASSIST_JOB_IDLE;
    notice_push(UI_NOTICE_INFO, "Suggestions discarded",
                detail, NULL, false);
}

MUSIALIZER_PLUG bool plug_load_analysis_bridge(const char *file_path)
{
    if (p == NULL || p->current_track < 0) return false;
    Analysis_Candidate *candidate = load_analysis_candidate_for_track(
        file_path, (size_t)p->current_track, ASSIST_MODE_ALL);
    if (candidate == NULL) return false;
    Track *track = current_track();
    Musi_Analysis_Lane_Reference staged[MUSI_PROJECT_MAX_ANALYSIS_LANES];
    size_t staged_count = 0;
    char staged_audio_sha256[SHA256_HEX_SIZE];
    bool provenance_valid = stage_candidate_analysis_lanes(
        track, candidate, file_path, true, staged, &staged_count,
        staged_audio_sha256);
    bool applied = provenance_valid &&
        apply_candidate_to_track(candidate, (size_t)p->current_track);
    if (applied) {
        memcpy(track->analysis_lanes, staged,
               staged_count*sizeof(track->analysis_lanes[0]));
        track->analysis_lane_count = staged_count;
        snprintf(track->audio_sha256, sizeof(track->audio_sha256), "%s",
                 staged_audio_sha256);
    } else if (!provenance_valid) {
        notice_push(UI_NOTICE_ERROR, "Analysis bridge was not applied",
                    "The bridge provenance path or identity is not project-safe.",
                    file_path, true);
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
        track->cue_settings_active = false;
        (void)scene_instance_select(&p->scene, track->base_scene,
                                    track->scene_seed);
    }
    mark_project_dirty(track);
    return true;
}

static Assist_Job_State assist_stopped_state(Assist_Job_State stopping_state)
{
    if (stopping_state == ASSIST_JOB_TIMING_OUT) return ASSIST_JOB_TIMED_OUT;
    if (stopping_state == ASSIST_JOB_CANCELLING) return ASSIST_JOB_CANCELLED;
    return ASSIST_JOB_FAILED;
}

#ifdef _WIN32
static bool assist_windows_job_empty(bool *empty)
{
    if (empty == NULL || p->assist_job_object == NULL) return false;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
    memset(&accounting, 0, sizeof(accounting));
    if (!QueryInformationJobObject(
            p->assist_job_object, JobObjectBasicAccountingInformation,
            &accounting, sizeof(accounting), NULL)) return false;
    *empty = accounting.ActiveProcesses == 0;
    return true;
}

static void assist_windows_close_process_ownership(void)
{
    if (p->assist_process != NOB_INVALID_PROC) CloseHandle(p->assist_process);
    if (p->assist_job_object != NULL) CloseHandle(p->assist_job_object);
    p->assist_process = NOB_INVALID_PROC;
    p->assist_job_object = NULL;
}
#endif

static void finish_assist_stop(Assist_Job_State stopping_state)
{
    p->assist_process = NOB_INVALID_PROC;
    p->assist_job_state = assist_stopped_state(stopping_state);
    const char *track_name = p->assist_track_index < p->tracks.count ?
                             GetFileName(p->tracks.items[p->assist_track_index].file_path) :
                             "the missing target track";
    char detail[UI_NOTICE_DETAIL_CAPACITY];
    snprintf(detail, sizeof(detail),
             "The job for %s stopped. Editor content is unchanged.", track_name);
    if (stopping_state == ASSIST_JOB_TIMING_OUT) {
        notice_push(UI_NOTICE_WARNING, "Analysis reached its 40-minute deadline",
                    detail, p->assist_log_path, true);
    } else if (stopping_state == ASSIST_JOB_CANCELLING) {
        notice_push(UI_NOTICE_INFO, "Analysis cancelled",
                    detail, p->assist_log_path, false);
    } else {
        notice_push(UI_NOTICE_ERROR, "Analysis cleanup failed",
                    detail, p->assist_log_path, true);
    }
}

static void poll_assist_job(void)
{
    if (!assist_job_is_active(p->assist_job_state)) return;
    if (p->assist_process == NOB_INVALID_PROC) {
#ifdef _WIN32
        if (p->assist_job_object != NULL) {
            (void)TerminateJobObject(p->assist_job_object, ERROR_CANCELLED);
            bool empty = false;
            if (!assist_windows_job_empty(&empty) || !empty) {
                p->assist_job_state = ASSIST_JOB_FAILING;
                return;
            }
            CloseHandle(p->assist_job_object);
            p->assist_job_object = NULL;
        }
#endif
        p->assist_job_state = ASSIST_JOB_FAILED;
        TraceLog(LOG_WARNING, "ASSIST: active job lost its process handle");
        notice_push(UI_NOTICE_ERROR, "Analysis state was recovered",
                    "The worker handle was unavailable. Editor content is unchanged.",
                    p->assist_log_path, true);
        return;
    }

    if (assist_job_deadline_expired(p->assist_job_state,
                                    p->assist_started_at, GetTime())) {
        if (request_assist_job_stop(ASSIST_JOB_TIMING_OUT)) {
            notice_push(UI_NOTICE_WARNING, "Analysis deadline reached",
                        "Stopping the complete Assist process tree after 40:00.",
                        p->assist_log_path, false);
        }
    }

    Nob_Proc process = p->assist_process;
    if (p->assist_job_state == ASSIST_JOB_CANCELLING ||
        p->assist_job_state == ASSIST_JOB_TIMING_OUT ||
        p->assist_job_state == ASSIST_JOB_FAILING) {
        Assist_Job_State stopping_state = p->assist_job_state;
        bool finished = false;
#ifdef _WIN32
        if (stopping_state == ASSIST_JOB_FAILING && p->assist_job_object != NULL) {
            (void)TerminateJobObject(p->assist_job_object, ERROR_CANCELLED);
        }
        DWORD waited = WaitForSingleObject(process, 0);
        bool empty = false;
        bool queried = assist_windows_job_empty(&empty);
        if (waited == WAIT_OBJECT_0 && queried && empty) {
            assist_windows_close_process_ownership();
            finished = true;
        } else if (waited == WAIT_FAILED || !queried) {
            if (stopping_state != ASSIST_JOB_FAILING) {
                TraceLog(LOG_ERROR,
                         "ASSIST: process-tree termination could not be verified");
            }
            p->assist_job_state = ASSIST_JOB_FAILING;
        }
#else
        pid_t waited = waitpid(process, NULL, WNOHANG);
        if (waited == process || (waited < 0 && errno == ECHILD)) {
            finished = true;
        } else if (waited < 0 && errno != EINTR) {
            if (stopping_state != ASSIST_JOB_FAILING) {
                TraceLog(LOG_ERROR, "ASSIST: worker termination could not be reaped");
            }
            p->assist_job_state = ASSIST_JOB_FAILING;
        } else if (GetTime() - p->assist_cancel_started_at >= 2.0) {
            if (kill(-process, SIGKILL) < 0 && errno == ESRCH) {
                (void)kill(process, SIGKILL);
            }
        }
#endif
        if (finished) finish_assist_stop(stopping_state);
        return;
    }

    int status = 0;
#ifdef _WIN32
    DWORD waited = WaitForSingleObject(process, 0);
    if (waited == WAIT_TIMEOUT) return;
    if (waited == WAIT_FAILED) {
        p->assist_job_state = ASSIST_JOB_FAILING;
        if (p->assist_job_object != NULL) {
            (void)TerminateJobObject(p->assist_job_object, ERROR_CANCELLED);
        }
        return;
    }
    bool empty = false;
    if (!assist_windows_job_empty(&empty)) {
        p->assist_job_state = ASSIST_JOB_FAILING;
        (void)TerminateJobObject(p->assist_job_object, ERROR_CANCELLED);
        return;
    }
    if (!empty) {
        // The Python worker waits for every subprocess. A signaled root with
        // live descendants is an abnormal escape path, never a completed job.
        p->assist_job_state = ASSIST_JOB_FAILING;
        p->assist_cancel_started_at = GetTime();
        (void)TerminateJobObject(p->assist_job_object, ERROR_CANCELLED);
        return;
    }
    DWORD exit_status = 1;
    status = GetExitCodeProcess(process, &exit_status) && exit_status == 0 ? 1 : -1;
    assist_windows_close_process_ownership();
#else
    status = nob__proc_wait_async(process, 0);
#endif
    if (status == 0) return;
    p->assist_process = NOB_INVALID_PROC;
    if (status < 0) {
        p->assist_job_state = ASSIST_JOB_FAILED;
        snprintf(p->assist_failure_detail, sizeof(p->assist_failure_detail), "%s",
                 "The helper exited before producing a validated result.");
        TraceLog(LOG_WARNING, "ASSIST: analysis failed; see %s", p->assist_log_path);
        notice_push(UI_NOTICE_ERROR, "Analysis failed",
                    p->assist_failure_detail,
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
    const char *track_name = p->assist_track_index < p->tracks.count ?
                             GetFileName(p->tracks.items[p->assist_track_index].file_path) :
                             "the missing target track";
    char detail[UI_NOTICE_DETAIL_CAPACITY];
    snprintf(detail, sizeof(detail),
             "Validated suggestions for %s are staged in the Assist panel.",
             track_name);
    notice_push(UI_NOTICE_SUCCESS, "Analysis ready for review",
                detail, p->assist_bridge_path, false);
}

static void request_assist_job_cancel(void)
{
    (void)request_assist_job_stop(ASSIST_JOB_CANCELLING);
}

static bool request_assist_job_stop(Assist_Job_State stopping_state)
{
    if (p == NULL || p->assist_job_state != ASSIST_JOB_RUNNING ||
        p->assist_process == NOB_INVALID_PROC ||
        (stopping_state != ASSIST_JOB_CANCELLING &&
         stopping_state != ASSIST_JOB_TIMING_OUT &&
         stopping_state != ASSIST_JOB_FAILING)) return false;
#ifdef _WIN32
    if (p->assist_job_object == NULL ||
        !TerminateJobObject(p->assist_job_object, ERROR_CANCELLED)) {
        p->assist_job_state = ASSIST_JOB_FAILING;
        p->assist_cancel_started_at = GetTime();
        notice_push(UI_NOTICE_ERROR, "Analysis could not be cancelled",
                    "Windows could not terminate the contained process tree; ownership is retained for retry.",
                    p->assist_log_path, true);
        return false;
    }
#else
    pid_t process = p->assist_process;
    int result = kill(-process, SIGTERM);
    if (result < 0 && errno == ESRCH) result = kill(process, SIGTERM);
    if (result < 0 && errno != ESRCH) {
        notice_push(UI_NOTICE_ERROR, "Analysis could not be cancelled",
                    "The worker did not accept a termination request.",
                    p->assist_log_path, true);
        p->assist_job_state = ASSIST_JOB_FAILING;
        p->assist_cancel_started_at = GetTime();
        return false;
    }
#endif
    p->assist_job_state = stopping_state;
    p->assist_cancel_started_at = GetTime();
    p->assist_confirmation_pending = false;
    return true;
}

static bool cancel_assist_job_blocking(void)
{
    if (p == NULL || !assist_job_is_active(p->assist_job_state) ||
        p->assist_process == NOB_INVALID_PROC) return true;
    if (p->assist_job_state == ASSIST_JOB_RUNNING) {
        (void)request_assist_job_stop(ASSIST_JOB_CANCELLING);
    }
    bool finished = false;
#ifdef _WIN32
    if (p->assist_job_object != NULL) {
        (void)TerminateJobObject(p->assist_job_object, ERROR_CANCELLED);
    }
    for (unsigned attempt = 0; attempt < 400; ++attempt) {
        bool empty = false;
        if (WaitForSingleObject(p->assist_process, 0) == WAIT_OBJECT_0 &&
            assist_windows_job_empty(&empty) && empty) {
            assist_windows_close_process_ownership();
            finished = true;
            break;
        }
        Sleep(10);
    }
#else
    pid_t process = p->assist_process;
    if (p->assist_job_state == ASSIST_JOB_RUNNING &&
        kill(-process, SIGTERM) < 0 && errno == ESRCH) {
        (void)kill(process, SIGTERM);
    }
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
    if (finished) {
        p->assist_process = NOB_INVALID_PROC;
        p->assist_job_state = assist_stopped_state(p->assist_job_state);
    } else {
        // Keep the process-tree ownership and active state truthful. Callers
        // must not unload/free the Plug while these tokens remain live.
        p->assist_job_state = ASSIST_JOB_FAILING;
    }
    p->assist_confirmation_pending = false;
    return finished;
}

static bool build_project(Track *track, const char *project_path,
                          const char *stored_audio_path,
                          const char *stored_ascii_image_path,
                          Musi_Project *project)
{
    if (track == NULL || project_path == NULL || project_path[0] == '\0' ||
        stored_audio_path == NULL || stored_audio_path[0] == '\0' ||
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

    project->audio.mode = MUSI_ASSET_IMPORTED;
    if (snprintf(project->audio.path, sizeof(project->audio.path), "%s",
                 stored_audio_path) >= (int)sizeof(project->audio.path)) return false;
    snprintf(project->audio.sha256, sizeof(project->audio.sha256), "%s",
             track->audio_sha256);
    project->audio.duration_seconds = duration;
    project->audio.sample_rate = track->music.stream.sampleRate;
    project->audio.channels = (uint16_t)track->music.stream.channels;

    if (ascii_art_grid_is_populated(track->ascii_columns, track->ascii_rows)) {
        if (stored_ascii_image_path == NULL ||
            stored_ascii_image_path[0] == '\0' ||
            track->ascii_image_sha256[0] == '\0') return false;
        project->ascii_image.present = true;
        if (snprintf(project->ascii_image.path,
                     sizeof(project->ascii_image.path), "%s",
                     stored_ascii_image_path) >=
            (int)sizeof(project->ascii_image.path)) return false;
        snprintf(project->ascii_image.sha256,
                 sizeof(project->ascii_image.sha256), "%s",
                 track->ascii_image_sha256);
        project->ascii_image.columns = (uint32_t)track->ascii_columns;
        project->ascii_image.rows = (uint32_t)track->ascii_rows;
    }

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
    if (!scene_settings_export_mappings(
            &track->scene_settings, project->scenes[0].mappings,
            MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE,
            &project->scenes[0].mapping_count)) return false;

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
        destination->setting_count = source->settings.captured ?
                                     source->settings.count : 0;
        if (destination->setting_count > 0) {
            memcpy(destination->settings, source->settings.values,
                   destination->setting_count*sizeof(destination->settings[0]));
        }
        snprintf(destination->scene_name, sizeof(destination->scene_name), "%s",
                 scene_stable_name((Scene_Id)source->scene_index));
    }
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < track->scene_presets.counts[scene]; ++index) {
            if (project->scene_preset_count >= MUSI_PROJECT_MAX_SCENE_PRESETS) {
                return false;
            }
            const Scene_Settings_Preset *source =
                &track->scene_presets.items[scene][index];
            Musi_Scene_Preset *destination =
                &project->scene_presets[project->scene_preset_count++];
            destination->id = source->id;
            destination->setting_count = source->snapshot.count;
            memcpy(destination->settings, source->snapshot.values,
                   source->snapshot.count*sizeof(destination->settings[0]));
            snprintf(destination->scene_name, sizeof(destination->scene_name),
                     "%s", scene_stable_name((Scene_Id)scene));
            snprintf(destination->name, sizeof(destination->name), "%s",
                     source->name);
        }
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
    if (track->audio_sha256[0] == '\0' &&
        !sha256_file_hex(track->file_path, track->audio_sha256)) return false;
    char stored_audio[MUSI_PROJECT_PATH_CAPACITY];
    char bundled_audio[PLUG_RELOAD_PATH_CAPACITY];
    Musi_Project_Bundle_Result audio_bundle = musi_project_bundle_asset(
        path, MUSI_PROJECT_ASSET_AUDIO, track->file_path, track->audio_sha256,
        stored_audio, sizeof(stored_audio), bundled_audio, sizeof(bundled_audio));
    if (audio_bundle != MUSI_PROJECT_BUNDLE_OK) {
        notice_push(UI_NOTICE_ERROR, "Project audio could not be bundled",
                    musi_project_bundle_result_string(audio_bundle), path, true);
        return false;
    }
    char stored_image[MUSI_PROJECT_PATH_CAPACITY] = {0};
    char bundled_image[PLUG_RELOAD_PATH_CAPACITY] = {0};
    if (ascii_art_grid_is_populated(track->ascii_columns, track->ascii_rows)) {
        Musi_Project_Bundle_Result image_bundle = musi_project_bundle_asset(
            path, MUSI_PROJECT_ASSET_IMAGE, track->ascii_image_path,
            track->ascii_image_sha256, stored_image, sizeof(stored_image),
            bundled_image, sizeof(bundled_image));
        if (image_bundle != MUSI_PROJECT_BUNDLE_OK) {
            notice_push(UI_NOTICE_ERROR, "ASCII image could not be bundled",
                        musi_project_bundle_result_string(image_bundle), path, true);
            return false;
        }
    }
    char *durable_audio_path = strdup(bundled_audio);
    if (durable_audio_path == NULL) return false;
    Musi_Project *project = malloc(sizeof(*project));
    if (project == NULL) {
        free(durable_audio_path);
        return false;
    }
    if (!build_project(track, path, stored_audio, stored_image, project)) {
        free(durable_audio_path);
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
        free(durable_audio_path);
        free(project);
        return false;
    }
    char *json = malloc(required);
    if (json == NULL) {
        free(durable_audio_path);
        free(project);
        return false;
    }
    Musi_Project_Io_Result encoded = musi_project_json_serialize(
        project, json, required, &required);
    Musi_Project_Metadata saved_metadata = project->metadata;
    free(project);
    if (encoded != MUSI_PROJECT_IO_OK) {
        free(durable_audio_path);
        free(json);
        return false;
    }

    Musi_Project_File_Result file_result = musi_project_atomic_write(
        path, json, required - 1);
    free(json);
    if (file_result != MUSI_PROJECT_FILE_OK) {
        char detail[UI_NOTICE_DETAIL_CAPACITY];
        if (file_result == MUSI_PROJECT_FILE_ERROR_DURABILITY) {
            snprintf(detail, sizeof(detail),
                     "The file was published, but crash durability could not be confirmed: %s.",
                     musi_project_file_result_string(file_result));
        } else {
            snprintf(detail, sizeof(detail),
                     "The previous project file was preserved: %s.",
                     musi_project_file_result_string(file_result));
        }
        free(durable_audio_path);
        track->project_autosave_failed = true;
        notice_push(UI_NOTICE_ERROR, "Project could not be saved",
                    detail, path, true);
        return false;
    }

    free(track->file_path);
    track->file_path = durable_audio_path;
    if (bundled_image[0] != '\0') {
        snprintf(track->ascii_image_path, sizeof(track->ascii_image_path),
                 "%s", bundled_image);
    }
    snprintf(track->project_path, sizeof(track->project_path), "%s", path);
    track->project_metadata = saved_metadata;
    track->project_metadata_initialized = true;
    track->project_dirty = false;
    track->project_autosave_failed = false;
    if (show_success) {
        notice_push(UI_NOTICE_SUCCESS, "Project saved",
                    "Audio, ASCII imagery, lyrics, scenes, events, and output settings are durable.",
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
    Scene_Settings hydrated_settings;
    if (!scene_settings_import_mappings(
            &hydrated_settings, project->scenes[0].mappings,
            project->scenes[0].mapping_count)) {
        notice_push(UI_NOTICE_ERROR, "Project scene settings were rejected",
                    "A stored control is unknown or outside its supported range.",
                    path, true);
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
        if (project->scene_switches.cues[i].setting_count > 0) {
            cues[i].settings.captured = true;
            cues[i].settings.count = project->scene_switches.cues[i].setting_count;
            memcpy(cues[i].settings.values,
                   project->scene_switches.cues[i].settings,
                   cues[i].settings.count*sizeof(cues[i].settings.values[0]));
            if (!scene_settings_snapshot_valid(cue_scene, &cues[i].settings)) {
                notice_push(UI_NOTICE_ERROR, "Project scene cue was rejected",
                            "A captured tuning snapshot does not match its scene.",
                            path, true);
                free(project);
                return false;
            }
        }
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

    Scene_Settings_Preset_Library preset_library;
    scene_settings_preset_library_init(&preset_library);
    for (size_t i = 0; i < project->scene_preset_count; ++i) {
        const Musi_Scene_Preset *source = &project->scene_presets[i];
        Scene_Id preset_scene;
        if (!scene_id_from_name(source->scene_name, &preset_scene) ||
            source->id == UINT64_MAX ||
            preset_library.counts[preset_scene] >=
                SCENE_SETTINGS_PRESETS_PER_SCENE) {
            notice_push(UI_NOTICE_ERROR, "Project scene preset was rejected",
                        "The preset scene, ID, or per-scene capacity is unsupported.",
                        path, true);
            free(project);
            return false;
        }
        Scene_Settings_Preset *destination =
            &preset_library.items[preset_scene]
                                 [preset_library.counts[preset_scene]++];
        destination->id = source->id;
        snprintf(destination->name, sizeof(destination->name), "%s", source->name);
        destination->snapshot.captured = true;
        destination->snapshot.count = source->setting_count;
        memcpy(destination->snapshot.values, source->settings,
               source->setting_count*sizeof(source->settings[0]));
        if (!scene_settings_snapshot_valid(preset_scene, &destination->snapshot)) {
            notice_push(UI_NOTICE_ERROR, "Project scene preset was rejected",
                        "The stored tuning values do not match their scene.",
                        path, true);
            free(project);
            return false;
        }
        if (source->id >= preset_library.next_id) {
            preset_library.next_id = source->id + 1;
        }
    }
    if (!scene_settings_preset_library_valid(&preset_library)) {
        notice_push(UI_NOTICE_ERROR, "Project scene presets were rejected",
                    "Preset identifiers or names are invalid.", path, true);
        free(project);
        return false;
    }

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
    Musi_Project_Path_Result audio_resolution = project->audio.mode ==
        MUSI_ASSET_IMPORTED ? musi_project_resolve_bundled_asset_path(
            path, project->audio.path, audio_path, sizeof(audio_path)) :
        musi_project_resolve_asset_path(
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

    char ascii_image_path[PLUG_RELOAD_PATH_CAPACITY] = {0};
    char ascii_image_hash[SHA256_HEX_SIZE] = {0};
    if (project->ascii_image.present) {
        Musi_Project_Path_Result image_resolution =
            musi_project_resolve_bundled_asset_path(
                path, project->ascii_image.path, ascii_image_path,
                sizeof(ascii_image_path));
        if (image_resolution != MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE ||
            !sha256_file_hex(ascii_image_path, ascii_image_hash) ||
            strcmp(ascii_image_hash, project->ascii_image.sha256) != 0) {
            notice_push(UI_NOTICE_ERROR, "Project ASCII image does not match",
                        "The bundled image is missing, outside the project, or changed identity.",
                        project->ascii_image.path, true);
            free(project);
            return false;
        }
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

    AsciiCell *hydrated_ascii = NULL;
    size_t hydrated_ascii_columns = 0;
    size_t hydrated_ascii_rows = 0;
    if (project->ascii_image.present) {
        hydrated_ascii = calloc(ASCII_GRID_MAX_CELLS, sizeof(*hydrated_ascii));
        if (hydrated_ascii == NULL ||
            !load_ascii_image_grid(
                ascii_image_path, hydrated_ascii, ASCII_GRID_MAX_CELLS,
                &hydrated_ascii_columns, &hydrated_ascii_rows) ||
            hydrated_ascii_columns != project->ascii_image.columns ||
            hydrated_ascii_rows != project->ascii_image.rows) {
            free(hydrated_ascii);
            notice_push(UI_NOTICE_ERROR, "Project ASCII image could not be restored",
                        "The verified image no longer converts to the saved grid dimensions.",
                        ascii_image_path, true);
            free(project);
            return false;
        }
    }

    Scene_Instance hydrated_scene;
    if (!scene_instance_init(&hydrated_scene, scene_id,
                             project->deterministic_seed)) {
        notice_push(UI_NOTICE_ERROR, "Project scene could not be prepared",
                    "The scene did not have enough resources to restore its deterministic state.",
                    project->scenes[0].scene_type, true);
        free(hydrated_ascii);
        free(project);
        return false;
    }

    int previous_track_index = p->current_track;
    size_t new_index = p->tracks.count;
    if (!plug_load_track(audio_path) || new_index >= p->tracks.count) {
        scene_instance_unload(&hydrated_scene);
        notice_push(UI_NOTICE_ERROR, "Project audio could not be loaded",
                    "The verified audio decoder rejected the file.", audio_path, true);
        free(hydrated_ascii);
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
    track->previous_base_scene = scene_id;
    track->scene_selection_pending = false;
    track->scene_seed = project->deterministic_seed;
    track->scene_instance_id = project->scenes[0].instance_id;
    track->scene_settings = hydrated_settings;
    track->playback_scene_settings = hydrated_settings;
    track->scene_presets = preset_library;
    if (hydrated_ascii != NULL) {
        memcpy(track->ascii_cells, hydrated_ascii,
               hydrated_ascii_columns*hydrated_ascii_rows*
               sizeof(track->ascii_cells[0]));
        track->ascii_columns = hydrated_ascii_columns;
        track->ascii_rows = hydrated_ascii_rows;
        snprintf(track->ascii_image_path, sizeof(track->ascii_image_path),
                 "%s", ascii_image_path);
        snprintf(track->ascii_image_sha256,
                 sizeof(track->ascii_image_sha256), "%s", ascii_image_hash);
    }
    free(hydrated_ascii);
    capture_missing_scene_cue_settings(track);
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
    ui_widgets_track_label(font, text, position, fontSize, tint);
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
                p->scene_settings_reset_confirmation = false;
                p->scene_settings_reset_undo_available = false;
                p->scene_settings_reset_track = p->current_track;
                p->scene_settings_reset_scene = p->scene.id;
                notice_dismiss(&p->scene_settings_reset_notice_id);
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

static float slider_get_value(float x, float lox, float hix);

static void set_scene_settings_open(bool open)
{
    if (p->scene_settings_open == open) return;

    if (open) {
        p->scene_settings_open = true;
        p->scene_settings_scroll = 0.0f;
        p->scene_settings_scroll_scene = p->scene.id;
        return;
    }

    p->scene_settings_open = false;
    p->active_button_id = 0;
    p->scene_settings_reset_confirmation = false;
    notice_dismiss(&p->scene_settings_reset_notice_id);
    if (p->scene_settings_window_expanded && !IsWindowMaximized() &&
        abs(GetScreenWidth() - p->scene_settings_expanded_width) <= 4) {
        SetWindowSize(p->scene_settings_restore_width, GetScreenHeight());
    }
    p->scene_settings_window_expanded = false;
    p->scene_settings_restore_width = 0;
    p->scene_settings_expanded_width = 0;
}

static bool scene_settings_can_expand_window(void)
{
    if (IsWindowMaximized() || p->scene_settings_window_expanded) return false;
    const int inspector_width = 340;
    int monitor = GetCurrentMonitor();
    Vector2 position = GetWindowPosition();
    Vector2 monitor_position = GetMonitorPosition(monitor);
    return scene_settings_window_can_expand(
        (int)roundf(position.x), GetScreenWidth(),
        (int)roundf(monitor_position.x), GetMonitorWidth(monitor),
        inspector_width);
}

static void scene_settings_expand_window(void)
{
    if (!scene_settings_can_expand_window()) return;
    const int inspector_width = 340;
    p->scene_settings_restore_width = GetScreenWidth();
    p->scene_settings_expanded_width = p->scene_settings_restore_width + inspector_width;
    p->scene_settings_window_expanded = true;
    SetWindowSize(p->scene_settings_expanded_width, GetScreenHeight());
}

static bool scene_setting_slider(Rectangle boundary, Scene_Id scene_id,
                                 size_t setting_index, float *value)
{
    const Scene_Setting_Descriptor *descriptor = scene_settings_descriptor(
        (size_t)scene_id, setting_index);
    if (descriptor == NULL || value == NULL || boundary.width <= 1.0f) return false;

    const uint64_t id = UINT64_C(0x534554534C440000) +
                        (uint64_t)scene_id*SCENE_SETTINGS_MAX_CONTROLS +
                        setting_index;
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, boundary);
    if (p->active_button_id == 0 && hover &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        p->active_button_id = id;
    }

    bool changed = false;
    if (p->active_button_id == id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float normalized = slider_get_value(mouse.x, boundary.x,
                                                boundary.x + boundary.width);
            float next = descriptor->minimum +
                         normalized*(descriptor->maximum - descriptor->minimum);
            if (descriptor->precision == 0) next = roundf(next);
            if (next != *value) {
                *value = next;
                changed = true;
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) p->active_button_id = 0;
    }

    float normalized = (*value - descriptor->minimum)/
                       (descriptor->maximum - descriptor->minimum);
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    float center_y = boundary.y + boundary.height*0.5f;
    DrawRectangle((int)boundary.x, (int)(center_y - 1.0f),
                  (int)boundary.width, 2, COLOR_UI_RULE);
    DrawRectangle((int)boundary.x, (int)(center_y - 2.0f),
                  (int)(boundary.width*normalized), 4, COLOR_ACCENT);
    float handle_x = boundary.x + boundary.width*normalized;
    DrawRectangleRec((Rectangle){handle_x - 5.0f, center_y - 8.0f, 10.0f, 16.0f},
                     COLOR_ACCENT);
    if (p->active_button_id == id) {
        DrawRectangleLinesEx(boundary, 2.0f, ColorAlpha(COLOR_ACCENT, 0.72f));
    } else if (hover) {
        DrawRectangleLinesEx(boundary, 1.0f, ColorAlpha(COLOR_ACCENT, 0.28f));
    }
    return changed;
}

static bool scene_setting_toggle(Rectangle boundary, Scene_Id scene_id,
                                 size_t setting_index, float *value,
                                 const char *off_label, const char *on_label)
{
    if (value == NULL || boundary.width <= 1.0f) return false;
    const uint64_t base = UINT64_C(0x534554544F470000) +
                          ((uint64_t)scene_id*SCENE_SETTINGS_MAX_CONTROLS +
                           setting_index)*2U;
    const float gap = 6.0f;
    Rectangle filled = {boundary.x, boundary.y,
                        (boundary.width - gap)*0.5f, boundary.height};
    Rectangle wireframe = {filled.x + filled.width + gap, boundary.y,
                           filled.width, boundary.height};
    bool enabled = *value >= 0.5f;
    if (text_button(base, filled, off_label, !enabled) & BS_CLICKED) {
        if (!enabled) return false;
        *value = 0.0f;
        return true;
    }
    if (text_button(base + 1U, wireframe, on_label, enabled) & BS_CLICKED) {
        if (enabled) return false;
        *value = 1.0f;
        return true;
    }
    return false;
}

static void scene_settings_panel(Rectangle boundary, Track *track)
{
    if (track == NULL || boundary.width < 1.0f || boundary.height < 1.0f) return;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawLineEx((Vector2){boundary.x, boundary.y},
               (Vector2){boundary.x, boundary.y + boundary.height},
               2.0f, COLOR_ACCENT);

    const float padding = UI_PANEL_PADDING;
    DrawTextEx(ui_font(), "SCENE SETTINGS",
               (Vector2){boundary.x + padding, boundary.y + 18.0f},
               UI_FONT_VALUE, 1.0f, COLOR_UI_MUTED);
    DrawTextEx(ui_font(), scene_name(p->scene.id),
               (Vector2){boundary.x + padding, boundary.y + 39.0f},
               24.0f, 0.0f, COLOR_UI_INK);

    Scene_Settings *editable_settings = track_effective_scene_settings(track);
    if (p->scene_settings_reset_scene != p->scene.id ||
        p->scene_settings_reset_track != p->current_track) {
        p->scene_settings_reset_confirmation = false;
        p->scene_settings_reset_undo_available = false;
        p->scene_settings_reset_scene = p->scene.id;
        p->scene_settings_reset_track = p->current_track;
        notice_dismiss(&p->scene_settings_reset_notice_id);
    }
    float button_y = boundary.y + 72.0f;
    const float header_gap = 6.0f;
    float button_width = (boundary.width - padding*2.0f - header_gap*2.0f)/3.0f;
    Rectangle reset = {boundary.x + padding, button_y, button_width,
                       UI_COMPACT_BUTTON_HEIGHT};
    Rectangle expand = {reset.x + reset.width + header_gap, button_y,
                        button_width, UI_COMPACT_BUTTON_HEIGHT};
    Rectangle hide = {expand.x + expand.width + header_gap, button_y,
                      button_width, UI_COMPACT_BUTTON_HEIGHT};
    const char *reset_label = p->scene_settings_reset_undo_available ? "Undo reset" :
                              p->scene_settings_reset_confirmation ? "Confirm" : "Reset";
    int reset_state = p->scene_settings_reset_confirmation ?
        danger_text_button(UINT64_C(0x53455454494E4752), reset,
                           reset_label, true) :
        text_button(UINT64_C(0x53455454494E4752), reset, reset_label, false);
    if (reset_state & BS_CLICKED) {
        if (p->scene_settings_reset_undo_available) {
            if (scene_settings_apply_snapshot(editable_settings, p->scene.id,
                                              &p->scene_settings_reset_undo)) {
                p->scene_settings_reset_undo_available = false;
                commit_active_cue_settings(track, p->scene.id);
                mark_project_dirty(track);
                notice_push(UI_NOTICE_SUCCESS, "Scene settings restored",
                            "The values from before Reset are active again.", NULL, false);
            }
        } else if (p->scene_settings_reset_confirmation) {
            Scene_Settings_Snapshot previous;
            if (scene_settings_capture(editable_settings, p->scene.id, &previous) &&
                scene_settings_reset_scene(editable_settings, p->scene.id)) {
                p->scene_settings_reset_undo = previous;
                p->scene_settings_reset_undo_available = true;
                p->scene_settings_reset_track = p->current_track;
                p->scene_settings_reset_scene = p->scene.id;
                p->scene_settings_reset_confirmation = false;
                notice_dismiss(&p->scene_settings_reset_notice_id);
                commit_active_cue_settings(track, p->scene.id);
                mark_project_dirty(track);
                notice_push(UI_NOTICE_SUCCESS, "Scene settings reset",
                            "Use Undo reset to restore the previous values.", NULL, false);
            }
        } else {
            p->scene_settings_reset_confirmation = true;
            p->scene_settings_reset_notice_id = notice_push(
                UI_NOTICE_WARNING, "Confirm scene reset",
                "Click Confirm to replace this scene's tuned values with defaults.",
                NULL, true);
        }
    }
    if (scene_settings_can_expand_window()) {
        if (text_button(UINT64_C(0x53455454494E4745), expand,
                        "Expand", false) & BS_CLICKED) {
            scene_settings_expand_window();
        }
        tooltip(expand, "Expand the application window for this inspector",
                SIDE_BOTTOM, false);
    } else {
        disabled_text_button(expand,
                             p->scene_settings_window_expanded ? "Expanded" : "Fit", false);
        tooltip(expand, p->scene_settings_window_expanded ?
                "The application window is already expanded" :
                "The inspector is fitted inside the current window",
                SIDE_BOTTOM, false);
    }
    if (text_button(UINT64_C(0x53455454494E4748), hide,
                    "Hide", false) & BS_CLICKED) {
        set_scene_settings_open(false);
    }

    if (p->scene_settings_scroll_scene != p->scene.id) {
        p->scene_settings_scroll_scene = p->scene.id;
        p->scene_settings_scroll = 0.0f;
    }
    size_t scene_index = (size_t)p->scene.id;
    size_t preset_count = track->scene_presets.counts[scene_index];
    size_t *selected = &track->selected_scene_preset[scene_index];
    if (preset_count == 0) *selected = 0;
    else if (*selected >= preset_count) *selected = preset_count - 1;
    if (p->scene_preset_delete_confirmation &&
        (p->scene_preset_delete_scene != p->scene.id ||
         p->scene_preset_delete_index != *selected)) {
        p->scene_preset_delete_confirmation = false;
    }
    float preset_y = button_y + 44.0f;
    char preset_status[64];
    snprintf(preset_status, sizeof(preset_status), "PRESETS  %zu / %u",
             preset_count, SCENE_SETTINGS_PRESETS_PER_SCENE);
    DrawTextEx(ui_font(), preset_status,
               (Vector2){boundary.x + padding, preset_y + 3.0f},
               13.0f, 1.0f, COLOR_UI_MUTED);
    const float small_gap = 5.0f;
    float nav_y = preset_y + 22.0f;
    Rectangle previous = {boundary.x + padding, nav_y, 34.0f, 28.0f};
    Rectangle next = {boundary.x + boundary.width - padding - 34.0f,
                      nav_y, 34.0f, 28.0f};
    Rectangle preset_name = {previous.x + previous.width + small_gap, nav_y,
                             next.x - previous.x - previous.width - small_gap*2.0f,
                             28.0f};
    if (preset_count > 0) {
        if ((text_button(UINT64_C(0x5052455345545052), previous, "<", false) &
             BS_CLICKED) != 0) {
            *selected = (*selected + preset_count - 1)%preset_count;
        }
        if ((text_button(UINT64_C(0x5052455345544E58), next, ">", false) &
             BS_CLICKED) != 0) {
            *selected = (*selected + 1)%preset_count;
        }
        disabled_text_button(preset_name,
            track->scene_presets.items[scene_index][*selected].name, true);
    } else {
        disabled_text_button(previous, "<", false);
        DrawRectangleLinesEx(preset_name, 1.0f, ColorAlpha(COLOR_UI_RULE, 0.72f));
        Vector2 empty_size = MeasureTextEx(ui_font(), "Save a preset to see it here",
                                           UI_FONT_CAPTION, 0.0f);
        DrawTextEx(ui_font(), "Save a preset to see it here",
                   (Vector2){preset_name.x + (preset_name.width - empty_size.x)*0.5f,
                             preset_name.y + (preset_name.height - empty_size.y)*0.5f},
                   UI_FONT_CAPTION, 0.0f, COLOR_UI_MUTED);
        disabled_text_button(next, ">", false);
    }

    float action_y = nav_y + 34.0f;
    float action_width = (boundary.width - padding*2.0f - small_gap*3.0f)*0.25f;
    Rectangle apply = {boundary.x + padding, action_y, action_width, 30.0f};
    Rectangle save = {apply.x + action_width + small_gap, action_y,
                      action_width, 30.0f};
    Rectangle replace = {save.x + action_width + small_gap, action_y,
                         action_width, 30.0f};
    Rectangle remove = {replace.x + action_width + small_gap, action_y,
                        action_width, 30.0f};
    if (preset_count > 0) {
        if ((text_button(UINT64_C(0x5052455345544150), apply,
                         "Load", false) & BS_CLICKED) != 0 &&
            scene_settings_preset_apply(&track->scene_presets, scene_index,
                                        *selected, editable_settings)) {
            commit_active_cue_settings(track, p->scene.id);
            mark_project_dirty(track);
        }
        if ((text_button(UINT64_C(0x5052455345545250), replace,
                         "Update", false) & BS_CLICKED) != 0 &&
            scene_settings_preset_replace(&track->scene_presets, scene_index,
                                          *selected, editable_settings)) {
            mark_project_dirty(track);
        }
        const char *delete_label = p->scene_preset_delete_confirmation ?
                                   "Confirm" : "Delete";
        int delete_state = p->scene_preset_delete_confirmation ?
            danger_text_button(UINT64_C(0x505245534554444C), remove,
                               delete_label, true) :
            text_button(UINT64_C(0x505245534554444C), remove,
                        delete_label, false);
        if ((delete_state & BS_CLICKED) != 0) {
            if (!p->scene_preset_delete_confirmation) {
                p->scene_preset_delete_confirmation = true;
                p->scene_preset_delete_scene = p->scene.id;
                p->scene_preset_delete_index = *selected;
            } else if (scene_settings_preset_remove(
                           &track->scene_presets, scene_index, *selected)) {
                p->scene_preset_delete_confirmation = false;
                if (*selected > 0 &&
                    *selected >= track->scene_presets.counts[scene_index]) {
                    (*selected)--;
                }
                mark_project_dirty(track);
            }
        }
        tooltip(apply, "Load this preset into the active scene", SIDE_BOTTOM, false);
        tooltip(replace, "Replace this preset with the current values", SIDE_BOTTOM, false);
        tooltip(remove, p->scene_preset_delete_confirmation ?
                "Click again to permanently remove this preset" :
                "Remove the selected preset", SIDE_BOTTOM, false);
    } else {
        disabled_text_button(apply, "Load", false);
        disabled_text_button(replace, "Update", false);
        disabled_text_button(remove, "Delete", false);
    }
    if (preset_count < SCENE_SETTINGS_PRESETS_PER_SCENE &&
        (text_button(UINT64_C(0x5052455345545341), save,
                     "Save new", false) & BS_CLICKED) != 0) {
        char name[SCENE_SETTINGS_PRESET_NAME_CAPACITY];
        snprintf(name, sizeof(name), "Preset %llu",
                 (unsigned long long)track->scene_presets.next_id);
        if (scene_settings_preset_save(&track->scene_presets, scene_index,
                                       name, editable_settings, selected)) {
            mark_project_dirty(track);
        }
    } else if (preset_count >= SCENE_SETTINGS_PRESETS_PER_SCENE) {
        disabled_text_button(save, "Full", false);
    }

    const float content_top = action_y + 42.0f;
    const float footer_height = 44.0f;
    const float row_height = 76.0f;
    size_t setting_count = scene_settings_count(p->scene.id);
    float total_height = (float)setting_count*row_height;
    float available_content_height = fmaxf(0.0f, boundary.height -
                                           (content_top - boundary.y) - footer_height);
    const float content_height = fminf(available_content_height, total_height);
    float max_scroll = fmaxf(0.0f, total_height - content_height);
    Vector2 mouse = GetMousePosition();
    Rectangle content = {boundary.x, content_top, boundary.width, content_height};
    if (CheckCollisionPointRec(mouse, content)) {
        p->scene_settings_scroll -= GetMouseWheelMove()*34.0f;
    }
    if (p->scene_settings_scroll < 0.0f) p->scene_settings_scroll = 0.0f;
    if (p->scene_settings_scroll > max_scroll) p->scene_settings_scroll = max_scroll;

    BeginScissorMode((int)content.x, (int)content.y,
                     (int)content.width, (int)content.height);
    for (size_t index = 0; index < setting_count; ++index) {
        const Scene_Setting_Descriptor *descriptor = scene_settings_descriptor(
            p->scene.id, index);
        float y = content.y + (float)index*row_height - p->scene_settings_scroll;
        if (descriptor == NULL || y + row_height < content.y ||
            y > content.y + content.height) continue;
        DrawTextEx(ui_font(), descriptor->label,
                   (Vector2){boundary.x + padding, y + 5.0f},
                   UI_FONT_LABEL, 0.0f, COLOR_UI_INK);
        char value_text[32];
        float value = scene_settings_get(editable_settings, p->scene.id, index);
        if (descriptor->kind == SCENE_SETTING_TOGGLE) {
            bool hue_motion = p->scene.id == SCENE_SONG_ATLAS &&
                              index == ATLAS_SETTING_HUE_MOTION;
            snprintf(value_text, sizeof(value_text), "%s", hue_motion ?
                     (value >= 0.5f ? "Music-reactive" : "Manual") :
                     (value >= 0.5f ? "Wireframe" : "Filled"));
        } else if (p->scene.id == SCENE_SONG_ATLAS &&
                   index == ATLAS_SETTING_DETAIL) {
            snprintf(value_text, sizeof(value_text), "%dx", (int)lroundf(value));
        } else {
            snprintf(value_text, sizeof(value_text), "%.*f",
                     (int)descriptor->precision, value);
        }
        Vector2 value_size = MeasureTextEx(ui_font(), value_text, UI_FONT_VALUE, 0.0f);
        DrawTextEx(ui_font(), value_text,
                   (Vector2){boundary.x + boundary.width - padding - value_size.x,
                             y + 6.0f},
                   UI_FONT_VALUE, 0.0f, COLOR_ACCENT);
        Rectangle slider = {boundary.x + padding, y + 31.0f,
                            boundary.width - padding*2.0f, 30.0f};
        bool changed;
        if (descriptor->kind == SCENE_SETTING_TOGGLE) {
            bool hue_motion = p->scene.id == SCENE_SONG_ATLAS &&
                              index == ATLAS_SETTING_HUE_MOTION;
            changed = scene_setting_toggle(
                slider, p->scene.id, index, &value,
                hue_motion ? "Manual" : "Filled",
                hue_motion ? "Music" : "Wireframe");
        } else {
            changed = scene_setting_slider(
                slider, p->scene.id, index, &value);
        }
        if (changed &&
            scene_settings_set(editable_settings, p->scene.id, index, value)) {
            commit_active_cue_settings(track, p->scene.id);
            mark_project_dirty(track);
        }
        DrawLine((int)(boundary.x + padding), (int)(y + row_height - 1.0f),
                 (int)(boundary.x + boundary.width - padding),
                 (int)(y + row_height - 1.0f), COLOR_UI_RULE);
    }
    EndScissorMode();

    if (max_scroll > 0.0f && content_height > 0.0f) {
        Rectangle rail = {boundary.x + boundary.width - 7.0f, content.y,
                          2.0f, content.height};
        float thumb_height = fmaxf(34.0f,
                                    content.height*content.height/total_height);
        if (thumb_height > content.height) thumb_height = content.height;
        float thumb_y = content.y +
            (content.height - thumb_height)*(p->scene_settings_scroll/max_scroll);
        DrawRectangleRec(rail, COLOR_UI_RULE);
        DrawRectangleRec((Rectangle){rail.x - 1.0f, thumb_y, 4.0f, thumb_height},
                         COLOR_ACCENT);
    }

    DrawTextEx(ui_font(), track->cue_settings_active ?
               "Editing this cue snapshot" : "Presets saved with this track",
               (Vector2){boundary.x + padding,
                         content.y + content.height + 15.0f},
               14.0f, 0.0f, COLOR_UI_MUTED);
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
    bool compact_header = boundary.width < 280.0f;
    if (track != NULL && track->scene_switches.enabled && compact_header) {
        snprintf(status, sizeof(status), "AUTO");
    } else if (track != NULL && track->scene_switches.enabled) {
        snprintf(status, sizeof(status), "AUTO  |  %zu events",
                 combined_scene_events().count);
    } else if (compact_header) {
        snprintf(status, sizeof(status), "%zu", combined_scene_events().count);
    } else {
        snprintf(status, sizeof(status), "%zu events", combined_scene_events().count);
    }
    Rectangle settings_button = {
        boundary.x + boundary.width - padding - 68.0f,
        boundary.y + 3.0f,
        68.0f,
        24.0f,
    };
    if (text_button(UINT64_C(0x5343454E45534554), settings_button,
                    p->scene_settings_open ? "Hide" : "Tune",
                    p->scene_settings_open) & BS_CLICKED) {
        set_scene_settings_open(!p->scene_settings_open);
    }
    Vector2 status_size = MeasureTextEx(ui_font(), status, 12.0f, 0.0f);
    DrawTextEx(ui_font(), status,
               (Vector2){settings_button.x - status_size.x - 7.0f,
                         boundary.y + 9.0f},
               12.0f, 0.0f, COLOR_UI_MUTED);

    const float footer_height = 36.0f;
    const float gap = 4.0f;
    const size_t columns = 2;
    const size_t rows = (COUNT_SCENES + columns - 1)/columns;
    float row_height = (boundary.height - header_height - footer_height - padding*2.0f
                      - gap*(rows - 1))/(float)rows;
    if (row_height > 38.0f) row_height = 38.0f;
    if (row_height < 24.0f) row_height = 24.0f;
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
            (void)select_base_scene(id);
        }
        char shortcut[16];
        snprintf(shortcut, sizeof(shortcut), "Scene [%u]", (unsigned)id + 1U);
        tooltip(row, shortcut, SIDE_RIGHT, false);
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
            track->ascii_image_path[0] = '\0';
            track->ascii_image_sha256[0] = '\0';
            mark_project_dirty(track);
            notice_push(UI_NOTICE_INFO, "ASCII image cleared",
                        "The bundled image will be removed from the next project save.",
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
    return ui_widgets_slider_get_value(x, lox, hix);
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
    return ui_notice_severity_label(severity);
}

static Color notice_severity_color(Ui_Notice_Severity severity)
{
    switch (severity) {
    case UI_NOTICE_INFO: return COLOR_ACCENT;
    case UI_NOTICE_SUCCESS: return COLOR_UI_SUCCESS;
    case UI_NOTICE_WARNING: return COLOR_UI_WARNING;
    case UI_NOTICE_ERROR: return COLOR_UI_DANGER;
    case UI_NOTICE_SEVERITY_COUNT: break;
    }
    return COLOR_UI_INK;
}

static void draw_notice_wrapped_text(const char *text, Vector2 position,
                                     float maximum_width, float font_size,
                                     size_t maximum_lines, Color color)
{
    ui_widgets_draw_wrapped_text(ui_font(), text, position, maximum_width,
                                 font_size, maximum_lines, color);
}

static void notice_tray(Rectangle preview_boundary)
{
    (void)ui_notice_tick(&p->notices, GetFrameTime());
    const float width = fminf(390.0f, preview_boundary.width - 32.0f);
    if (width < 220.0f) return;
    const float height = 142.0f;
    const float gap = 10.0f;
    const float margin = 16.0f;
    size_t tray_capacity = preview_boundary.height > margin ?
        (size_t)((preview_boundary.height - margin)/(height + gap)) : 1u;
    if (tray_capacity < 1u) tray_capacity = 1u;
    if (tray_capacity > 3u) tray_capacity = 3u;
    const size_t visible_count = p->notices.count < tray_capacity ?
                                 p->notices.count : tray_capacity;
    uint64_t dismiss_id = 0;
    uint64_t retry_id = 0;
    for (size_t row = 0; row < visible_count; ++row) {
        size_t index = p->notices.count - 1 - row;
        const Ui_Notice *notice = &p->notices.notices[index];
        Rectangle card = {
            .x = preview_boundary.x + preview_boundary.width - width - margin,
            .y = preview_boundary.y + margin + row*(height + gap),
            .width = width,
            .height = height,
        };
        Color severity_color = notice_severity_color(notice->severity);
        DrawRectangleRec(card, (Color){247, 247, 248, 248});
        DrawRectangleLinesEx(card, 1.0f, (Color){20, 20, 20, 255});
        DrawRectangle((int)card.x, (int)card.y, 5, (int)card.height, severity_color);
        DrawTextEx(ui_font(), notice_severity_label(notice->severity),
                   (Vector2){card.x + 16.0f, card.y + 11.0f}, 14.0f, 1.0f,
                   severity_color);
        DrawTextEx(ui_font(), notice->title,
                   (Vector2){card.x + 82.0f, card.y + 9.0f}, 18.0f, 1.0f,
                   (Color){20, 20, 20, 255});
        const float text_width = card.width - 32.0f;
        draw_notice_wrapped_text(notice->detail,
                                 (Vector2){card.x + 16.0f, card.y + 36.0f},
                                 text_width, 14.0f, 3,
                                 (Color){70, 70, 72, 255});
        if (notice->path[0] != '\0') {
            const char *path_label = GetFileName(notice->path);
            DrawTextEx(ui_font(), path_label,
                       (Vector2){card.x + 16.0f, card.y + 89.0f}, 12.0f, 1.0f,
                       (Color){92, 92, 96, 255});
            tooltip((Rectangle){card.x + 12.0f, card.y + 86.0f,
                                text_width, 18.0f}, notice->path,
                    SIDE_BOTTOM, false);
        }
        Rectangle dismiss = {
            card.x + card.width - 76.0f, card.y + card.height - 30.0f, 64.0f, 22.0f,
        };
        int state = button_with_id(UINT64_C(0x4E4F544943450000) ^ notice->id, dismiss);
        DrawRectangleLinesEx(dismiss, state & BS_HOVEROVER ? 2.0f : 1.0f,
                             (Color){20, 20, 20, 255});
        DrawTextEx(ui_font(), "Dismiss",
                   (Vector2){dismiss.x + 8.0f, dismiss.y + 5.0f}, 13.0f, 1.0f,
                   (Color){20, 20, 20, 255});
        if (state & BS_CLICKED) dismiss_id = notice->id;
        if (notice->path[0] != '\0') {
            Rectangle copy = {dismiss.x - 78.0f, dismiss.y, 70.0f, dismiss.height};
            int copy_state = button_with_id(
                UINT64_C(0x4E4F544943454350) ^ notice->id, copy);
            DrawRectangleLinesEx(copy, copy_state & BS_HOVEROVER ? 2.0f : 1.0f,
                                 COLOR_UI_INK);
            DrawTextEx(ui_font(), "Copy path",
                       (Vector2){copy.x + 7.0f, copy.y + 5.0f}, 12.0f, 1.0f,
                       COLOR_UI_INK);
            if (copy_state & BS_CLICKED) SetClipboardText(notice->path);
        }
        bool retry_available = ui_notice_is_actionable_failure(notice) &&
            strncmp(notice->title, "Analysis", 8) == 0 && current_track() != NULL &&
            !assist_job_is_active(p->assist_job_state) && p->assist_candidate == NULL;
        if (retry_available) {
            Rectangle retry = {card.x + 16.0f, dismiss.y, 58.0f, dismiss.height};
            int retry_state = button_with_id(
                UINT64_C(0x4E4F544943455254) ^ notice->id, retry);
            DrawRectangleRec(retry, retry_state & BS_HOVEROVER ?
                             COLOR_TRACK_BUTTON_HOVEROVER : COLOR_UI_RAISED);
            DrawRectangleLinesEx(retry, 1.0f, COLOR_ACCENT);
            DrawTextEx(ui_font(), "Retry",
                       (Vector2){retry.x + 11.0f, retry.y + 5.0f}, 12.0f, 1.0f,
                       COLOR_ACCENT);
            if (retry_state & BS_CLICKED) retry_id = notice->id;
        }
    }
    if (dismiss_id != 0) (void)ui_notice_dismiss(&p->notices, dismiss_id);
    if (retry_id != 0) {
        (void)ui_notice_dismiss(&p->notices, retry_id);
        p->assist_panel_open = true;
        p->lyrics_editor_open = false;
        p->export_panel_open = false;
        p->assist_confirmation_pending = true;
    }
    if (p->notices.count > visible_count) {
        char hidden[48];
        snprintf(hidden, sizeof(hidden), "+%zu more notice%s",
                 p->notices.count - visible_count,
                 p->notices.count - visible_count == 1 ? "" : "s");
        DrawTextEx(ui_font(), hidden,
                   (Vector2){preview_boundary.x + preview_boundary.width - width,
                             preview_boundary.y + margin +
                             visible_count*(height + gap)},
                   UI_FONT_CAPTION, 1.0f, COLOR_UI_MUTED);
    }
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
    if (assist_job_is_active(p->assist_job_state)) {
        notice_push(UI_NOTICE_WARNING, "Export was not started",
                    "Wait for Assist to finish, or cancel its job, before starting a render.",
                    p->assist_log_path, false);
        return false;
    }
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
    if (track_uses_song_atlas(track) &&
        !song_atlas_map_valid(&track->song_atlas_map)) {
        track->song_atlas_map_attempted = true;
        if (song_atlas_map_build(
                wave_samples, (size_t)wave.frameCount, (size_t)wave.channels,
                wave.sampleRate, &track->song_atlas_map) == 0) {
            TraceLog(LOG_WARNING,
                     "ATLAS: whole-song map could not be prepared for export of %s",
                     track->file_path);
        }
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
    track->cue_settings_active = false;
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
    bool analysis_running = assist_job_is_active(p->assist_job_state);
    bool export_running = p->rendering;
    if (dirty_projects == 0 && !dirty_draft && !staged_suggestions &&
        !analysis_running && !export_running) return true;

    char message[768];
    size_t used = 0;
    used += (size_t)snprintf(message + used, sizeof(message) - used,
                            "Resolve these items before quitting, or discard them now:\n");
    if (dirty_draft && used < sizeof(message)) used += (size_t)snprintf(
        message + used, sizeof(message) - used,
        "\n- Apply or discard the active lyric draft.");
    if (staged_suggestions && used < sizeof(message)) used += (size_t)snprintf(
        message + used, sizeof(message) - used,
        "\n- Apply or discard the validated Assist result.");
    if (dirty_projects > 0 && used < sizeof(message)) used += (size_t)snprintf(
        message + used, sizeof(message) - used,
        "\n- Save %zu unnamed or unresolved track project%s.",
        dirty_projects, dirty_projects == 1 ? "" : "s");
    if (analysis_running && used < sizeof(message)) used += (size_t)snprintf(
        message + used, sizeof(message) - used,
        "\n- Cancel the running analysis job.");
    if (export_running && used < sizeof(message)) used += (size_t)snprintf(
        message + used, sizeof(message) - used,
        "\n- Cancel the running video export.");
    if (used < sizeof(message)) (void)snprintf(
        message + used, sizeof(message) - used,
        "\n\nQuit anyway and discard or cancel the items above?");
    return tinyfd_messageBox("Unresolved Musializer work", message,
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
    if (assist_job_is_active(p->assist_job_state)) {
        notice_push(UI_NOTICE_WARNING, "Capture was not started",
                    "Wait for Assist to finish, or cancel its job, before starting microphone capture.",
                    p->assist_log_path, false);
        return;
    }
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
        p->capturing = false;
        notice_push(UI_NOTICE_ERROR, "Microphone capture could not start",
                    "The recording file could not be created in the current directory.",
                    recording_file_path, true);
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
        p->capturing = false;
        notice_push(UI_NOTICE_ERROR, "Microphone capture could not start",
                    ma_result_description(result), NULL, true);
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
        p->capturing = false;
        notice_push(UI_NOTICE_ERROR, "Microphone capture could not start",
                    ma_result_description(result), NULL, true);
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
            p->lyric_editor.text_active = false;
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

static void draw_fullscreen_assist_status(Rectangle preview_boundary)
{
    const char *message = NULL;
    char status[256];
    if (p->assist_candidate != NULL) {
        snprintf(status, sizeof(status), "Assist result ready  |  F to review");
        message = status;
    } else if (p->assist_job_state == ASSIST_JOB_CANCELLING ||
               p->assist_job_state == ASSIST_JOB_TIMING_OUT ||
               p->assist_job_state == ASSIST_JOB_FAILING) {
        snprintf(status, sizeof(status), "%s  |  F to return to the editor",
                 p->assist_job_state == ASSIST_JOB_TIMING_OUT ?
                     "Assist reached its 40:00 job deadline" :
                     "Assist is verifying process-tree cleanup");
        message = status;
    } else if (p->assist_job_state == ASSIST_JOB_RUNNING) {
        double elapsed = fmax(0.0, GetTime() - p->assist_started_at);
        snprintf(status, sizeof(status), "%s  |  %02u:%02u elapsed  |  F to return",
                 assist_mode_display_name(p->assist_mode),
                 (unsigned)(elapsed/60.0), (unsigned)fmod(elapsed, 60.0));
        message = status;
    } else if (p->assist_panel_open && p->assist_confirmation_pending) {
        snprintf(status, sizeof(status), "Assist setup pending  |  F to continue");
        message = status;
    } else if (p->assist_panel_open && p->assist_job_state == ASSIST_JOB_FAILED) {
        snprintf(status, sizeof(status), "Assist failed  |  F to inspect the job log");
        message = status;
    }
    if (message == NULL || preview_boundary.width < 320.0f) return;

    float width = fminf(440.0f, preview_boundary.width - 24.0f);
    Rectangle badge = {preview_boundary.x + 12.0f, preview_boundary.y + 12.0f,
                       width, 42.0f};
    DrawRectangleRec(badge, ColorAlpha(COLOR_UI_RAISED, 0.93f));
    DrawRectangleLinesEx(badge, 1.0f, COLOR_UI_RULE);
    DrawRectangleRec((Rectangle){badge.x, badge.y, 4.0f, badge.height}, COLOR_ACCENT);
    BeginScissorMode((int)badge.x + 5, (int)badge.y,
                     (int)fmaxf(0.0f, badge.width - 6.0f), (int)badge.height);
    DrawTextEx(ui_font(), message, (Vector2){badge.x + 14.0f, badge.y + 12.0f},
               14.0f, 1.0f, COLOR_UI_INK);
    EndScissorMode();
}

static void preview_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    // Service decode buffers before polling jobs, handling drops, hashing, or
    // any other operation that may occasionally stall the UI thread. The later
    // call catches tracks created by those operations and refills after them.
    Track *early_track = current_track();
    if (early_track != NULL) UpdateMusicStream(early_track->music);

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
        Scene_Settings_Ui_Layout settings_layout = {
            .workspace_width = (float)w,
            .tracks_width = 320.0f,
        };
        (void)scene_settings_ui_layout((float)w, p->scene_settings_open,
                                       &settings_layout);
        float workspace_width = settings_layout.workspace_width;
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

        if (!p->lyric_editor.text_active && IsKeyPressed(KEY_TOGGLE_PLAY)) {
            toggle_track_playing(track);
        }

        if (!p->lyric_editor.text_active && IsKeyPressed(KEY_RENDER)) {
            if (lyric_editor_allow_context_change(track)) {
                p->export_panel_open = true;
                p->lyrics_editor_open = false;
                p->assist_panel_open = false;
                p->fullscreen = false;
            }
        }

        if (!p->lyric_editor.text_active && IsKeyPressed(KEY_FULLSCREEN)) {
            p->fullscreen = !p->fullscreen;
        }

        update_transport_shortcuts(track);
        double scene_time = GetMusicTimePlayed(track->music);
        float scene_dt = scene_clock_delta(scene_time);
        AudioSpectrumView spectrum = fft_analyze(scene_dt);
        if (!p->lyric_editor.text_active) update_scene_shortcuts();
        apply_auto_scene_switch(track, scene_time);

        float toolbar_height = HUD_BUTTON_SIZE;
        if (p->fullscreen) {
            // TODO: make timeline somehow visible in fullscreen mode (maybe miniversion of it on the toolbar)
            static float hud_timer = HUD_TIMER_SECS;

            Rectangle preview_boundary = {
                .x = 0,
                .y = 0,
                .width = workspace_width,
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
            draw_fullscreen_assist_status(preview_boundary);

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
            float tracks_panel_width = settings_layout.tracks_width;
            float timeline_height = 180.0f;
            if (p->assist_panel_open) {
                Assist_Panel_Content assist_content = assist_panel_content(
                    p->assist_job_state, p->assist_confirmation_pending,
                    p->assist_candidate != NULL);
                Assist_Ui_Layout assist_layout = assist_ui_layout(
                    workspace_width - 12.0f, assist_content);
                timeline_height = assist_timeline_height(
                    (float)h, toolbar_height, assist_layout.required_height);
            } else if (p->lyrics_editor_open || p->export_panel_open) {
                timeline_height = 330.0f;
                if (timeline_height > h - toolbar_height - 180.0f) {
                    timeline_height = fmaxf(150.0f,
                                            h - toolbar_height - 180.0f);
                }
            }
            Rectangle preview_boundary = {
                .x = tracks_panel_width,
                .y = 0,
                .width = workspace_width - tracks_panel_width,
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
            float scene_panel_height = fminf(292.0f,
                fmaxf(fminf(216.0f, sidebar_height), sidebar_height - 120.0f));
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
                .width = workspace_width,
                .height = timeline_height,
            }, track);

            toolbar(track, CLITERAL(Rectangle) {
                .x = tracks_panel_width,
                .y = preview_boundary.height,
                .width = preview_boundary.width,
                .height = toolbar_height,
            });
        }
        if (p->scene_settings_open) {
            scene_settings_panel((Rectangle){
                .x = workspace_width,
                .y = 0.0f,
                .width = (float)w - workspace_width,
                .height = (float)h,
            }, track);
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

        // start_capture() reports initialization failures through the normal
        // notice tray and leaves capture mode immediately. This branch is only
        // a defensive escape for corrupted hot-reload state.
        p->capturing = false;
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

static bool release_reload_sensitive_resources(void)
{
    // A helper may still be executing code/files from this checkout. Stop it
    // before unloading the plug that owns its process handle.
    if (!cancel_assist_job_blocking()) {
        TraceLog(LOG_ERROR,
                 "HOTRELOAD: Assist process ownership is still active; reload cancelled");
        return false;
    }
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
    return true;
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
    p->lyric_editor.list_follow_selection = true;
    render_export_config_init(&p->render_config);
    scene_settings_init(&p->scene_settings);
    p->scene_settings_reset_track = -1;
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
    } else {
        // Returning NULL vetoes reload in the host. No callback, process, GPU,
        // audio, or heap ownership has been released yet.
        TraceLog(LOG_ERROR, "HOTRELOAD: handoff allocation failed; reload cancelled");
        free(handoff);
        free(owned_allocations);
        return NULL;
    }
    if (!release_reload_sensitive_resources()) {
        free(handoff->owned_allocations);
        free(handoff);
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
    if (!cancel_assist_job_blocking()) {
        // Do not free the sole PID/HANDLE ownership record. The host is already
        // crossing its final process boundary and will reclaim this state.
        TraceLog(LOG_ERROR,
                 "SHUTDOWN: retaining Assist ownership until process teardown");
        return;
    }
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
