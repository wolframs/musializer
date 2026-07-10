#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "build/config.h"
#include "audio_analyzer.h"
#include "plug.h"
#include "ffmpeg.h"
#include "sample_ring.h"
#include "scene.h"
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

#define RENDER_FPS 30
#define RENDER_FACTOR 100
#define RENDER_WIDTH (16*RENDER_FACTOR)
#define RENDER_HEIGHT (9*RENDER_FACTOR)

#define PLUG_STATE_MAGIC UINT64_C(0x4D555349504C5547)
#define PLUG_STATE_VERSION 5

#define COLOR_ACCENT                  ColorFromHSV(225, 0.75, 0.8)
#define COLOR_BACKGROUND              GetColor(0x151515FF)
#define COLOR_TRACK_PANEL_BACKGROUND  ColorBrightness(COLOR_BACKGROUND, -0.1)
#define COLOR_TRACK_BUTTON_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_HOVEROVER  ColorBrightness(COLOR_TRACK_BUTTON_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_SELECTED   COLOR_ACCENT
#define COLOR_TIMELINE_CURSOR         COLOR_ACCENT
#define COLOR_TIMELINE_BACKGROUND     ColorBrightness(COLOR_BACKGROUND, -0.3)
#define COLOR_HUD_BUTTON_BACKGROUND   COLOR_TRACK_BUTTON_BACKGROUND
#define COLOR_HUD_BUTTON_HOVEROVER    COLOR_TRACK_BUTTON_HOVEROVER
#define COLOR_POPUP_BACKGROUND        ColorFromHSV(0, 0.75, 0.8)
#define COLOR_TOOLTIP_BACKGROUND      COLOR_TRACK_PANEL_BACKGROUND
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
} Track;

typedef struct {
    Track *items;
    size_t count;
    size_t capacity;
} Tracks;

typedef struct {
    float lifetime;
} Popup;

#define PT_GET(pt, index) (assert(index < (pt)->count), &(pt)->items[((pt)->begin + index)%POPUP_TRAY_CAPACITY])
#define PT_FIRST(pt) PT_GET((pt), 0)
#define PT_LAST(pt) PT_GET((pt), (pt)->count - 1)

#define POPUP_TRAY_CAPACITY 20
typedef struct {
    Popup items[POPUP_TRAY_CAPACITY];
    size_t begin;
    size_t count;
    float slide;
} Popup_Tray;


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
    uint64_t next_event_id;

    uint64_t active_button_id;

    Popup_Tray pt;

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

static bool fft_settled(void)
{
    return audio_analyzer_settled(&p->analyzer, 1e-3f);
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

static void start_preview_track(Track *track)
{
    analyzer_configure(track->music.stream.sampleRate, 2);
    p->scene_frame_index = 0;
    p->scene_clock_initialized = false;
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

MUSIALIZER_PLUG bool plug_load_track(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') return false;

    Music music = LoadMusicStream(file_path);
    if (!IsMusicValid(music)) return false;

    char *owned_path = strdup(file_path);
    if (owned_path == NULL) {
        UnloadMusicStream(music);
        return false;
    }

    AttachAudioStreamProcessor(music.stream, callback);
    size_t new_index = p->tracks.count;
    nob_da_append(&p->tracks, (CLITERAL(Track) {
        .file_path = owned_path,
        .music = music,
    }));

    if (current_track() == NULL) {
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

    bool converted = ascii_art_convert_rgba8(
        image.data,
        (size_t)image.width,
        (size_t)image.height,
        columns,
        rows,
        p->ascii_cells,
        NOB_ARRAY_LEN(p->ascii_cells));
    UnloadImage(image);
    if (!converted) return false;

    p->ascii_columns = columns;
    p->ascii_rows = rows;
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

    return scene_instance_select(&p->scene, id, p->scene.seed);
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

MUSIALIZER_PLUG bool plug_record_event(Event_Record event)
{
    if (event_timeline_record(&p->event_timeline, &event) != EVENT_TIMELINE_OK) {
        return false;
    }
    if (event.id >= p->next_event_id && event.id != UINT64_MAX) {
        p->next_event_id = event.id + 1;
    }
    return true;
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

    return (Scene_Frame) {
        .time_seconds = time_seconds,
        .delta_seconds = delta_seconds,
        .frame_index = p->scene_frame_index++,
        .events = event_timeline_view(&p->event_timeline),
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

static void scene_render(Rectangle boundary, AudioSpectrumView spectrum, double time_seconds, float delta_seconds)
{
    Scene_Frame frame = make_scene_frame(spectrum, time_seconds, delta_seconds);
    Scene_Renderer renderer = {
        .circle_shader = p->circle,
        .circle_radius_location = p->circle_radius_location,
        .circle_power_location = p->circle_power_location,
        .font = p->font,
        .ascii_cells = p->ascii_cells,
        .ascii_columns = p->ascii_columns,
        .ascii_rows = p->ascii_rows,
    };
    scene_instance_update(&p->scene, &frame);
    scene_instance_draw(&p->scene, &frame, &renderer, boundary);
}

static void update_scene_shortcuts(void)
{
    if (IsKeyPressed(KEY_ONE)) {
        scene_instance_select(&p->scene, SCENE_SPECTRUM, p->scene.seed);
    }
    if (IsKeyPressed(KEY_TWO)) {
        scene_instance_select(&p->scene, SCENE_PULSE_FIELD, p->scene.seed);
    }
    if (IsKeyPressed(KEY_THREE)) {
        scene_instance_select(&p->scene, SCENE_ORBITAL_LATTICE, p->scene.seed);
    }
    if (IsKeyPressed(KEY_FOUR)) {
        scene_instance_select(&p->scene, SCENE_ASCII_FIELD, p->scene.seed);
    }
    if (IsKeyPressed(KEY_FIVE)) {
        scene_instance_select(&p->scene, SCENE_SONG_ATLAS, p->scene.seed);
    }
    if (IsKeyPressed(KEY_SIX)) {
        scene_instance_select(&p->scene, SCENE_SPECTRAL_TERRARIUM, p->scene.seed);
    }
    if (IsKeyPressed(KEY_SEVEN)) {
        scene_instance_select(&p->scene, SCENE_CONSTELLATION, p->scene.seed);
    }
}


static void popup_tray_push(Popup_Tray *pt)
{
    if (pt->count < POPUP_TRAY_CAPACITY) {
        if (pt->begin == 0) {
            pt->begin = POPUP_TRAY_CAPACITY - 1;
        } else {
            pt->begin -= 1;
        }
        pt->count += 1;

        pt->slide += HUD_POPUP_SLIDEIN_SECS;
        PT_FIRST(pt)->lifetime = HUD_POPUP_LIFETIME_SECS + pt->slide;
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
    Vector2 text_size = MeasureTextEx(p->font, p->tooltip_buffer, fontSize, spacing);

    Rectangle tooltip_boundary = {
        .width = text_size.x + margin.x*2.0,
        .height = text_size.y + margin.y*2.0,
    };

    align_to_side_of_rect(p->tooltip_element_boundary, &tooltip_boundary, p->tooltip_align);
    snap_boundary_inside_screen(&tooltip_boundary);

    DrawRectangleRounded(tooltip_boundary, 0.4, 20, COLOR_TOOLTIP_BACKGROUND);
    Vector2 position = {
        .x = tooltip_boundary.x + tooltip_boundary.width/2 - text_size.x/2,
        .y = tooltip_boundary.y + tooltip_boundary.height/2 - text_size.y/2,
    };
    DrawTextEx(p->font, p->tooltip_buffer, position, fontSize, spacing, COLOR_TOOLTIP_FOREGROUND);
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

static int text_button(uint64_t id, Rectangle boundary, const char *label, bool selected)
{
    int state = button_with_id(id, boundary);
    Color background = selected ? COLOR_TRACK_BUTTON_SELECTED : COLOR_TRACK_BUTTON_BACKGROUND;
    if (state & BS_HOVEROVER) background = COLOR_TRACK_BUTTON_HOVEROVER;
    DrawRectangleRounded(boundary, 0.18f, 10, background);
    float font_size = fminf(boundary.height*0.52f, 22.0f);
    Vector2 size = MeasureTextEx(p->font, label, font_size, 0.0f);
    DrawTextEx(p->font, label,
               (Vector2){boundary.x + (boundary.width - size.x)*0.5f,
                         boundary.y + (boundary.height - size.y)*0.5f},
               font_size, 0.0f, WHITE);
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

static void record_timeline_event(Track *track, uint32_t type)
{
    if (track == NULL || p->next_event_id == UINT64_MAX) {
        popup_tray_push(&p->pt);
        return;
    }
    Event_Record event = {
        .timestamp_seconds = GetMusicTimePlayed(track->music),
        .id = p->next_event_id,
        .type = type,
        .value_count = 1,
        .values = {1.0f},
    };
    if (!plug_record_event(event)) {
        popup_tray_push(&p->pt);
        return;
    }
    if (p->scene.id != SCENE_CONSTELLATION) {
        scene_instance_select(&p->scene, SCENE_CONSTELLATION, p->scene.seed);
    }
}

static void timeline(Rectangle timeline_boundary, Track *track)
{
    DrawRectangleRec(timeline_boundary, COLOR_TIMELINE_BACKGROUND);

    float played = GetMusicTimePlayed(track->music);
    float len = GetMusicTimeLength(track->music);
    if (len <= 0.0f) return;

    const float controls_height = 38.0f;
    const float margin = 6.0f;
    const float event_button_width = 74.0f;
    Rectangle controls = {
        timeline_boundary.x + margin,
        timeline_boundary.y + margin,
        event_button_width*5.0f + margin*4.0f,
        controls_height,
    };
    const char *labels[4] = {"+ Lyric", "+ Feel", "+ Cue", "+ Custom"};
    const uint32_t types[4] = {
        EVENT_TYPE_LYRIC, EVENT_TYPE_SEMANTIC, EVENT_TYPE_CUE, EVENT_TYPE_CUSTOM
    };
    bool over_controls = CheckCollisionPointRec(GetMousePosition(), controls);
    float control_x = controls.x;
    for (size_t i = 0; i < 4; ++i) {
        Rectangle boundary = {control_x, controls.y, event_button_width, controls.height};
        int state = text_button(UINT64_C(0x45564E5400000000) + i, boundary, labels[i], false);
        DrawRectangleLinesEx(boundary, 1.0f, ColorAlpha(event_type_color(types[i]), 0.8f));
        if (state & BS_CLICKED) record_timeline_event(track, types[i]);
        control_x += event_button_width + margin;
    }
    Rectangle clear_boundary = {control_x, controls.y, event_button_width, controls.height};
    if (text_button(UINT64_C(0x45564E54FFFFFFFF), clear_boundary, "Clear", false) & BS_CLICKED) {
        event_timeline_clear(&p->event_timeline);
        p->next_event_id = 1;
    }

    Event_Timeline_View events = event_timeline_view(&p->event_timeline);
    for (size_t i = 0; i < events.count; ++i) {
        const Event_Record *event = &events.events[i];
        float t = (float)(event->timestamp_seconds/len);
        if (t < 0.0f || t > 1.0f) continue;
        float marker_x = timeline_boundary.x + t*timeline_boundary.width;
        Color color = event_type_color(event->type);
        DrawLineEx((Vector2){marker_x, timeline_boundary.y + controls_height + margin*2.0f},
                   (Vector2){marker_x, timeline_boundary.y + timeline_boundary.height},
                   3.0f, ColorAlpha(color, 0.75f));
        DrawCircleV((Vector2){marker_x, timeline_boundary.y + controls_height + margin*2.0f},
                    5.0f, color);
    }

    float x = timeline_boundary.x + played/len*timeline_boundary.width;
    Vector2 startPos = {
        .x = x,
        .y = timeline_boundary.y
    };
    Vector2 endPos = {
        .x = x,
        .y = timeline_boundary.y + timeline_boundary.height
    };
    DrawLineEx(startPos, endPos, 10, COLOR_TIMELINE_CURSOR);

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, timeline_boundary) && !over_controls) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            float t = (mouse.x - timeline_boundary.x)/timeline_boundary.width;
            SeekMusicStream(track->music, t*len);
        }

    }

    // TODO: enable the user to render a specific region instead of the whole song.
    // TODO: visualize sound wave on the timeline
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
    if (font.texture.id == 0) font = GetFontDefault();  // Security check in case of not valid font

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

    Vector2 mouse = GetMousePosition();

    float scroll_bar_width = panel_boundary.width*0.03;
    float item_size = panel_boundary.width*0.2;
    float visible_area_size = panel_boundary.height;
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
        panel_scroll = (mouse.y - panel_boundary.y - scrolling_mouse_offset)/visible_area_size*entire_scrollable_area;
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
            .y = i*item_size + panel_boundary.y + panel_padding - panel_scroll,
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
                if (track) StopMusicStream(track->music);
                start_preview_track(&p->tracks.items[i]);
                p->current_track = i;
            }
        } else {
            color = COLOR_TRACK_BUTTON_SELECTED;
        }
        // TODO: enable MSAA so the rounded rectangles look better
        // That triggers an old raylib bug with circles tho, so we will have to look into that
        DrawRectangleRounded(item_boundary, 0.2, 20, color);

        const char *text = GetFileName(p->tracks.items[i].file_path);
        float fontSize = item_boundary.height*0.5;
        float text_padding = item_boundary.width*0.05;
        Vector2 size = MeasureTextEx(p->font, text, fontSize, 0);
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
            track_label(p->font, text, position, fontSize, WHITE);
            EndScissorMode();

        } else { // <-- No need for ScissorMode
            track_label(p->font, text, position, fontSize, WHITE);
        }
    }

    // TODO: up and down clickable buttons on the scrollbar

    if (entire_scrollable_area > visible_area_size) { // Is scrolling needed
        float t = visible_area_size/entire_scrollable_area;
        float q = panel_scroll/entire_scrollable_area;
        Rectangle scroll_bar_area = {
            .x = panel_boundary.x + panel_boundary.width - scroll_bar_width,
            .y = panel_boundary.y,
            .width = scroll_bar_width,
            .height = panel_boundary.height,
        };
        // TODO: some sort of color for the scroll bar background
        //DrawRectangleRounded(scroll_bar_area, 0.8, 20, RED);
        Rectangle scroll_bar_boundary = {
            .x = panel_boundary.x + panel_boundary.width - scroll_bar_width,
            .y = panel_boundary.y + panel_boundary.height*q,
            .width = scroll_bar_width,
            .height = panel_boundary.height*t,
        };
        DrawRectangleRounded(scroll_bar_boundary, 0.8, 20, COLOR_TRACK_BUTTON_BACKGROUND);

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
    DrawRectangleRec(boundary, ColorBrightness(COLOR_TRACK_PANEL_BACKGROUND, -0.05f));
    DrawLineEx((Vector2){boundary.x, boundary.y},
               (Vector2){boundary.x + boundary.width, boundary.y},
               2.0f, ColorAlpha(COLOR_ACCENT, 0.45f));

    const float padding = 8.0f;
    const float header_height = 27.0f;
    float header_font = 18.0f;
    DrawTextEx(p->font, "SCENES", (Vector2){boundary.x + padding, boundary.y + 5.0f},
               header_font, 0.0f, ColorAlpha(WHITE, 0.78f));
    char status[48];
    snprintf(status, sizeof(status), "%zu events", p->event_timeline.count);
    Vector2 status_size = MeasureTextEx(p->font, status, 14.0f, 0.0f);
    DrawTextEx(p->font, status,
               (Vector2){boundary.x + boundary.width - status_size.x - padding,
                         boundary.y + 8.0f},
               14.0f, 0.0f, ColorAlpha(WHITE, 0.48f));

    const float footer_height = 36.0f;
    const float gap = 3.0f;
    float row_height = (boundary.height - header_height - footer_height - padding*2.0f
                      - gap*(COUNT_SCENES - 1))/(float)COUNT_SCENES;
    if (row_height > 34.0f) row_height = 34.0f;
    if (row_height < 20.0f) return;
    float y = boundary.y + header_height;
    for (Scene_Id id = 0; id < COUNT_SCENES; ++id) {
        Rectangle row = {boundary.x + padding, y, boundary.width - padding*2.0f, row_height};
        if (text_button(UINT64_C(0x5343454E45000000) + (uint64_t)id,
                        row, scene_name(id), p->scene.id == id) & BS_CLICKED) {
            scene_instance_select(&p->scene, id, p->scene.seed);
        }
        y += row_height + gap;
    }

    Rectangle import_button = {
        boundary.x + padding,
        boundary.y + boundary.height - footer_height,
        boundary.width - padding*2.0f,
        footer_height - padding*0.5f,
    };
    if (text_button(UINT64_C(0x41534349494D504F), import_button,
                    "Import image -> ASCII", false) & BS_CLICKED) {
        const char *filters[] = {"*.png", "*.jpg", "*.jpeg", "*.bmp"};
        char *path = tinyfd_openFileDialog("Image for ASCII Field", "./",
                                           NOB_ARRAY_LEN(filters), filters,
                                           "image files", 0);
        if (path == NULL) return;
        if (!plug_load_ascii_image(path) || !plug_select_scene("ascii")) {
            popup_tray_push(&p->pt);
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
    DrawTexturePro(p->icon_textures[UI_ICON_FULLSCREEN], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

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
    Color color = WHITE;
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

    DrawTexturePro(p->icon_textures[UI_ICON_VOLUME], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

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

static void popup_tray(Popup_Tray *pt, Rectangle preview_boundary)
{
    float dt = GetFrameTime();
    if (pt->slide > 0) {
        pt->slide -= dt;
    }
    if (pt->slide < 0) {
        pt->slide = 0;
    }

    float popup_width = 250;
    float popup_height = 75;
    float popup_padding = 20;
    for (size_t i = 0; i < pt->count; ++i) {
        Popup *it = PT_GET(pt, i);
        it->lifetime -= dt;

        float t = it->lifetime/HUD_POPUP_LIFETIME_SECS;
        float alpha = t >= 0.5f ? 1.0f : t/0.5f;

        float q = pt->slide / HUD_POPUP_SLIDEIN_SECS;

        Rectangle popup_boundary = {
            .x = preview_boundary.x + preview_boundary.width - popup_width - popup_padding,
            .y = preview_boundary.y + preview_boundary.height - (i + 1 - q)*(popup_height + popup_padding),
            .width = popup_width,
            .height = popup_height,
        };
        DrawRectangleRounded(popup_boundary, 0.3, 20, ColorAlpha(COLOR_POPUP_BACKGROUND, alpha));
        const char *text = "Could not load file";
        float fontSize = popup_boundary.width*0.15;
        Vector2 size = MeasureTextEx(p->font, text, fontSize, 0);
        Vector2 position = {
            .x = popup_boundary.x + popup_boundary.width/2 - size.x/2,
            .y = popup_boundary.y + popup_boundary.height/2 - size.y/2,
        };
        DrawTextEx(p->font, text, position, fontSize, 0, ColorAlpha(WHITE, alpha));
    }

    while (pt->count > 0 && PT_LAST(pt)->lifetime <= 0) {
        pt->count -= 1;
    }
}

#define cancel_rendering_button(boundary) \
    cancel_rendering_button_with_location(__FILE__, __LINE__, boundary)
static int cancel_rendering_button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);

    Color color = (state & BS_HOVEROVER) ? COLOR_TRACK_BUTTON_HOVEROVER : COLOR_TRACK_BUTTON_BACKGROUND;
    DrawRectangleRounded(boundary, 0.4, 20, color);

    float pad_x = boundary.width*0.3;
    float pad_y = boundary.height*0.3;
    float thick = boundary.width*0.10;

    {
        Vector2 startPos = {
            boundary.x + pad_x,
            boundary.y + pad_y,
        };
        Vector2 endPos = {
            boundary.x + boundary.width - pad_x,
            boundary.y + boundary.height - pad_y,
        };
        DrawLineEx(startPos, endPos, thick, COLOR_TOOLTIP_FOREGROUND);
    }

    {
        Vector2 startPos = {
            boundary.x + pad_x,
            boundary.y + boundary.height - pad_y,
        };
        Vector2 endPos = {
            boundary.x + boundary.width - pad_x,
            boundary.y + pad_y,
        };
        DrawLineEx(startPos, endPos, thick, COLOR_TOOLTIP_FOREGROUND);
    }

    return state;
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
    DrawTexturePro(p->icon_textures[UI_ICON_PLAY], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

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
    DrawTexturePro(p->icon_textures[UI_ICON_RENDER], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

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
    DrawTexturePro(p->icon_textures[UI_ICON_MICROPHONE], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

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

static bool start_rendering_track_to(Track *track, const char *output_path)
{
    if (track == NULL || output_path == NULL || output_path[0] == '\0') return false;

    StopMusicStream(track->music);

    // TODO: LoadWave is pretty slow on big files
    Wave wave = LoadWave(track->file_path);
    if (!IsWaveValid(wave)) {
        start_preview_track(track);
        return false;
    }
    float *wave_samples = LoadWaveSamples(wave);
    if (wave_samples == NULL) {
        UnloadWave(wave);
        start_preview_track(track);
        return false;
    }
    Scene_Id scene_id = p->scene.id;
    uint64_t scene_seed = p->scene.seed;
    if (!scene_instance_select(&p->scene, scene_id, scene_seed)) {
        UnloadWaveSamples(wave_samples);
        UnloadWave(wave);
        start_preview_track(track);
        return false;
    }

    fft_clean();
    p->wave = wave;
    p->wave_cursor = 0;
    p->wave_samples = wave_samples;
    analyzer_configure(p->wave.sampleRate, p->wave.channels);
    p->scene_frame_index = 0;
    p->scene_clock_initialized = false;
    // TODO: set the rendering output path based on the input path
    // Basically output into the same folder
    p->ffmpeg = ffmpeg_start_rendering(output_path, p->screen.texture.width, p->screen.texture.height, RENDER_FPS, track->file_path);
    p->render_failed = p->ffmpeg == NULL;
    SetTargetFPS(0);
    p->rendering = true;
    p->cancel_rendering = false;
    SetTraceLogLevel(LOG_WARNING);
    if (p->ffmpeg == NULL) {
        finish_rendering_track(track);
        return false;
    }
    return true;
}

static void start_rendering_track(Track *track)
{
    char const * filter_params[] = { "*.mp4" };
    char *output_path = tinyfd_saveFileDialog("Path to rendered video", "./", NOB_ARRAY_LEN(filter_params), filter_params, "mp4 video file");
    if (output_path != NULL) start_rendering_track_to(track, output_path);
}

MUSIALIZER_PLUG bool plug_start_render(const char *output_path)
{
    return start_rendering_track_to(current_track(), output_path);
}

MUSIALIZER_PLUG bool plug_render_active(void)
{
    return p->rendering && p->ffmpeg != NULL;
}

MUSIALIZER_PLUG bool plug_render_failed(void)
{
    return p->render_failed;
}

static void finish_rendering_track(Track *track)
{
    SetTraceLogLevel(LOG_INFO);
    UnloadWave(p->wave);
    UnloadWaveSamples(p->wave_samples);
    SetTargetFPS(PREVIEW_FPS);
    p->rendering = false;
    start_preview_track(track);
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
        start_rendering_track(track);
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

    if (IsFileDropped()) {
        FilePathList droppedFiles = LoadDroppedFiles();
        // TODO: loading files synchronously like that actually blocks the UI thread
        // Maybe we should do that in a separate thread.
        for (size_t i = 0; i < droppedFiles.count; ++i) {
            const char *path = droppedFiles.paths[i];
            bool image = IsFileExtension(path, ".png") || IsFileExtension(path, ".jpg") ||
                         IsFileExtension(path, ".jpeg") || IsFileExtension(path, ".bmp");
            if (image) {
                if (!plug_load_ascii_image(path) || !plug_select_scene("ascii")) {
                    popup_tray_push(&p->pt);
                }
            } else if (!plug_load_track(path)) {
                popup_tray_push(&p->pt);
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

        if (IsKeyPressed(KEY_TOGGLE_PLAY)) {
            toggle_track_playing(track);
        }

        if (IsKeyPressed(KEY_RENDER)) {
            start_rendering_track(track);
        }

        if (IsKeyPressed(KEY_FULLSCREEN)) {
            p->fullscreen = !p->fullscreen;
        }

        double scene_time = GetMusicTimePlayed(track->music);
        float scene_dt = scene_clock_delta(scene_time);
        AudioSpectrumView spectrum = fft_analyze(scene_dt);
        update_scene_shortcuts();

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

            popup_tray(&p->pt, preview_boundary);
        } else {
            float tracks_panel_width = 320.0f;
            float timeline_height = 150.0f;
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
            popup_tray(&p->pt, preview_boundary);
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
    } else { // We are waiting for the user to Drag&Drop the Music
        const char *label = "Click to Select File";
        int font_size = p->font.baseSize;
        Color color = WHITE;
        Vector2 size = MeasureTextEx(p->font, label, font_size, 0);
        Vector2 position = {
            w/2 - size.x/2,
            h/2 - size.y/2,
        };
        DrawTextEx(p->font, label, position, font_size, 0, color);

        font_size /= 2;
        label = "(or just Drag&Drop it)";
        color = WHITE;
        size = MeasureTextEx(p->font, label, font_size, 0);
        position.y += font_size*2;
        position.x = w/2 - size.x/2;
        DrawTextEx(p->font, label, position, font_size, 0, color);

        popup_tray(&p->pt, CLITERAL(Rectangle) {
            .x = 0,
            .y = 0,
            .width = w,
            .height = h,
        });

        if (button(((Rectangle) {0, 0, w, h})) & BS_CLICKED) {
            int allow_multiple_selects = 0; // TODO: enable multiple selects
            char const *filter_params[] = {"*.wav", "*.ogg", "*.mp3", "*.qoa", "*.xm", "*.mod", "*.flac"};
            char *input_path = tinyfd_openFileDialog("Path to music file", "./", NOB_ARRAY_LEN(filter_params), filter_params, "music file", allow_multiple_selects);
            if (input_path && !plug_load_track(input_path)) popup_tray_push(&p->pt);
        }
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
            if (!plug_load_track(recording_file_path)) popup_tray_push(&p->pt);
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
    if (p->ffmpeg == NULL) { // Starting FFmpeg process has failed for some reason
        if (IsKeyPressed(KEY_ESCAPE)) {
            finish_rendering_track(track);
        }

        const char *label = "FFmpeg Failure: Check the Logs";
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
    } else { // FFmpeg process is going
        // TODO: introduce a rendering mode that perfectly loops the video
        if (p->wave_cursor >= p->wave.frameCount && fft_settled()) { // Rendering is finished
            if (!ffmpeg_end_rendering(p->ffmpeg, false)) {
                // NOTE: Ending FFmpeg process has failed, let's mark ffmpeg handle as NULL
                // which will be interpreted as "FFmpeg Failure" on the next frame.
                //
                // It should be safe to set ffmpeg to NULL even if ffmpeg_end_rendering() failed
                // cause it should deallocate all the resources even in case of a failure.
                p->ffmpeg = NULL;
                p->render_failed = true;
            } else {
                finish_rendering_track(track);
            }
        } else if (IsKeyPressed(KEY_ESCAPE) || p->cancel_rendering) {  // Rendering is cancelled
            ffmpeg_end_rendering(p->ffmpeg, true);
            p->ffmpeg = NULL;

            finish_rendering_track(track);
        } else { // Rendering is going...
            // Label
            const char *label = "Rendering video...";
            Color color = WHITE;

            Vector2 size = MeasureTextEx(p->font, label, p->font.baseSize, 0);
            Vector2 position = {
                w/2 - size.x/2,
                h/2 - size.y/2,
            };
            DrawTextEx(p->font, label, position, p->font.baseSize, 0, color);

            // Progress bar
            float bar_width = w*2/3;
            float bar_height = p->font.baseSize*0.25;
            float bar_progress = (float)p->wave_cursor/p->wave.frameCount;
            float bar_padding_top = p->font.baseSize*0.5;
            if (bar_progress > 1) bar_progress = 1;
            Rectangle bar_filling = {
                .x = w/2 - bar_width/2,
                .y = h/2 + p->font.baseSize/2 + bar_padding_top,
                .width = bar_width*bar_progress,
                .height = bar_height,
            };
            DrawRectangleRec(bar_filling, WHITE);

            Rectangle bar_box = {
                .x = w/2 - bar_width/2,
                .y = h/2 + p->font.baseSize/2 + bar_padding_top,
                .width = bar_width,
                .height = bar_height,
            };
            DrawRectangleLinesEx(bar_box, 2, WHITE);

            {
                Rectangle boundary = {
                    .width = HUD_BUTTON_SIZE,
                    .height = HUD_BUTTON_SIZE,
                };
                boundary.x = w - boundary.width - HUD_BUTTON_SIZE*0.5;
                boundary.y = HUD_BUTTON_SIZE*0.5;
                tooltip(boundary, "Cancel [Esc]", SIDE_LEFT, false);
                if (cancel_rendering_button(boundary) & BS_CLICKED) {
                    p->cancel_rendering = true;
                }
            }

            // Rendering
            if (p->scene_frame_index > 0) {
                size_t chunk_size = p->wave.sampleRate/RENDER_FPS;
                // Carry the rational remainder so sample rates not divisible
                // by the render FPS never accumulate transport drift.
                size_t next_cursor = (size_t)(((uint64_t)p->scene_frame_index
                                             * p->wave.sampleRate)/RENDER_FPS);
                if (next_cursor > p->wave_cursor) chunk_size = next_cursor - p->wave_cursor;
                float *fs = (float*)p->wave_samples;
                size_t available = 0;
                if (p->wave_cursor < p->wave.frameCount) {
                    available = p->wave.frameCount - p->wave_cursor;
                    if (available > chunk_size) available = chunk_size;
                    audio_analyzer_push_interleaved(
                        &p->analyzer,
                        fs + p->wave_cursor*p->wave.channels,
                        available);
                }

                static const float silence[1024] = {0};
                size_t silence_count = chunk_size - available;
                while (silence_count > 0) {
                    size_t batch = silence_count;
                    if (batch > NOB_ARRAY_LEN(silence)) batch = NOB_ARRAY_LEN(silence);
                    audio_analyzer_push_mono(&p->analyzer, silence, batch);
                    silence_count -= batch;
                }
                p->wave_cursor += chunk_size;
            }

            float scene_dt = p->scene_frame_index == 0 ? 0.0f : 1.0f/RENDER_FPS;
            AudioSpectrumView spectrum = audio_analyzer_spectrum(&p->analyzer);
            if (!audio_analyzer_analyze(&p->analyzer, scene_dt)) {
                spectrum = (AudioSpectrumView){0};
            } else {
                spectrum = audio_analyzer_spectrum(&p->analyzer);
            }
            double scene_time = (double)p->scene_frame_index/RENDER_FPS;

            BeginTextureMode(p->screen);
            ClearBackground(COLOR_BACKGROUND);
            scene_render(CLITERAL(Rectangle) {
                0, 0, p->screen.texture.width, p->screen.texture.height
            }, spectrum, scene_time, scene_dt);
            EndTextureMode();

            Image image = LoadImageFromTexture(p->screen.texture);
            if (!ffmpeg_send_frame_flipped(p->ffmpeg, image.data, image.width, image.height)) {
                // NOTE: we don't check the result of ffmpeg_end_rendering here because we
                // don't care at this point: writing a frame failed, so something went completely
                // wrong. So let's just show to the user the "FFmpeg Failure" screen. ffmpeg_end_rendering
                // should log any additional errors anyway.
                ffmpeg_end_rendering(p->ffmpeg, false);
                p->ffmpeg = NULL;
                p->render_failed = true;
            }
            UnloadImage(image);
        }
    }
}

static void load_assets(void)
{
    size_t data_size = 0;
    void *data = NULL;

    const char *alegreya_path = "./resources/fonts/Alegreya-Regular.ttf";
    data = plug_load_resource(alegreya_path, &data_size);
        p->font = LoadFontFromMemory(GetFileExtension(alegreya_path), data, data_size, FONT_SIZE, NULL, 0);
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
    UnloadFont(p->font);
    UnloadShader(p->circle);
    for (UI_Icon icon = 0; icon < COUNT_UI_ICONS; ++icon) {
        UnloadTexture(p->icon_textures[icon]);
    }
    memset(&p->font, 0, sizeof(p->font));
    memset(&p->circle, 0, sizeof(p->circle));
    memset(p->icon_textures, 0, sizeof(p->icon_textures));
}

static void release_reload_sensitive_resources(void)
{
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
    p->rendering = false;
    p->cancel_rendering = false;
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
        Track track = p->tracks.items[read_index];
        track.music = LoadMusicStream(track.file_path);
        if (!IsMusicValid(track.music)) {
            TraceLog(LOG_WARNING, "HOTRELOAD: could not restore track %s", track.file_path);
            free(track.file_path);
            continue;
        }
        AttachAudioStreamProcessor(track.music.stream, callback);
        p->tracks.items[write_index] = track;
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
    p->next_event_id = 1;
    analyzer_configure(48000, 2);
    NOB_ASSERT(scene_instance_init(&p->scene, SCENE_SPECTRUM, UINT64_C(0x4D555349414C495A)));

    load_assets();
    p->screen = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
    p->current_track = -1;

    // TODO: restore master volume between sessions
    SetMasterVolume(0.5);
    SetTargetFPS(PREVIEW_FPS);
}

MUSIALIZER_PLUG void *plug_pre_reload(void)
{
    if (p == NULL) return NULL;

    const size_t allocation_count = 3 + p->tracks.count;
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
        handoff->scene_seed = p->scene.seed;
        Event_Timeline_View events = event_timeline_view(&p->event_timeline);
        handoff->event_count = events.count;
        if (handoff->event_count > EVENT_TIMELINE_CAPACITY) {
            handoff->event_count = EVENT_TIMELINE_CAPACITY;
        }
        memcpy(handoff->events, events.events,
               handoff->event_count*sizeof(handoff->events[0]));
        const char *selected_scene = scene_stable_name(p->scene.id);
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
        TraceLog(LOG_WARNING, "HOTRELOAD: unknown handoff ABI; restart required");
        plug_init();
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
    p->screen = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
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
    }
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
        UnloadMusicStream(track->music);
        free(track->file_path);
    }
    free(p->tracks.items);
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

    EndDrawing();
}

// TODO: About Page that includes current commit, version and the platforms
// We may also include licenses and contributors there.
// TODO: Actual Fullscreen Mode
// We do have fullscreen button, but apparently there is a demand on a fullscreen fullscreen mode.
// Raylib does have ToggleFullscreen(). Let's see how we can integrate it into the current UI/UX
// TODO: Adding Files by Ctrl+C, Ctrl+V
