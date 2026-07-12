#include "scene.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

#include "scene_draw.h"

// Streaming prototype: the ring below records only the recent past. A future
// measured-analysis cache can prefill the same slices before playback, turning
// this local fly-through into a true whole-song atlas without changing draw.
enum {
    ATLAS_BAND_COUNT = 24,
    ATLAS_SLICE_COUNT = 84,
};

typedef struct {
    double time_seconds;
    float bands[ATLAS_BAND_COUNT];
    float rms;
    float flux;
    bool onset;
} Atlas_Slice;

typedef struct {
    Atlas_Slice slices[ATLAS_SLICE_COUNT];
    uint64_t seed;
    size_t first;
    size_t count;
    double last_frame_time;
    double last_capture_time;
    float onset_pulse;
} Song_Atlas_State;

static float atlas_clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static uint32_t atlas_hash(uint64_t seed, uint32_t salt)
{
    uint64_t value = seed ^ ((uint64_t)salt + UINT64_C(0x9e3779b97f4a7c15));
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (uint32_t)value;
}

static float atlas_hash_unit(uint64_t seed, uint32_t salt)
{
    return (float)(atlas_hash(seed, salt) & UINT32_C(0xffff))/65535.0f;
}

static void atlas_clear_history(Song_Atlas_State *atlas)
{
    atlas->first = 0;
    atlas->count = 0;
    atlas->last_capture_time = -1.0;
    atlas->onset_pulse = 0.0f;
}

static void song_atlas_init(void *state, uint64_t seed)
{
    Song_Atlas_State *atlas = state;
    memset(atlas, 0, sizeof(*atlas));
    atlas->seed = seed;
    atlas->last_frame_time = -1.0;
    atlas->last_capture_time = -1.0;
}

static float atlas_resample_band(const Scene_Frame *frame, size_t destination)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;

    size_t begin = destination*frame->audio.bands_count/ATLAS_BAND_COUNT;
    size_t end = (destination + 1)*frame->audio.bands_count/ATLAS_BAND_COUNT;
    if (end <= begin) end = begin + 1;
    if (begin >= frame->audio.bands_count) begin = frame->audio.bands_count - 1;
    if (end > frame->audio.bands_count) end = frame->audio.bands_count;

    float peak = 0.0f;
    for (size_t i = begin; i < end; ++i) {
        float value = atlas_clamp01(frame->audio.bands[i]);
        if (value > peak) peak = value;
    }
    return peak;
}

static void atlas_capture(Song_Atlas_State *atlas, const Scene_Frame *frame)
{
    size_t index;
    if (atlas->count < ATLAS_SLICE_COUNT) {
        index = (atlas->first + atlas->count)%ATLAS_SLICE_COUNT;
        atlas->count += 1;
    } else {
        index = atlas->first;
        atlas->first = (atlas->first + 1)%ATLAS_SLICE_COUNT;
    }

    Atlas_Slice *slice = &atlas->slices[index];
    slice->time_seconds = frame->time_seconds;
    slice->rms = atlas_clamp01(frame->audio.rms*1.8f);
    slice->flux = atlas_clamp01(frame->audio.spectral_flux*5.0f);
    slice->onset = frame->audio.onset;
    for (size_t band = 0; band < ATLAS_BAND_COUNT; ++band) {
        slice->bands[band] = atlas_resample_band(frame, band);
    }
    atlas->last_capture_time = frame->time_seconds;
}

static void song_atlas_update(void *state, const Scene_Frame *frame)
{
    Song_Atlas_State *atlas = state;
    double elapsed = frame->time_seconds - atlas->last_frame_time;

    // Backward time is a seek. A very large forward jump is also treated as a
    // discontinuity so unrelated regions are never joined by terrain.
    if (atlas->last_frame_time >= 0.0 &&
        (elapsed < -0.001 || elapsed > 3.0)) {
        atlas_clear_history(atlas);
    }

    float delta = frame->delta_seconds;
    if (delta < 0.0f) delta = 0.0f;
    if (delta > 0.1f) delta = 0.1f;
    atlas->onset_pulse *= expf(-6.0f*delta);
    if (frame->audio.onset) atlas->onset_pulse = 1.0f;

    // Roughly eleven samples per second: enough shape for motion while keeping
    // geometry and state strictly bounded for realtime and offline rendering.
    if (atlas->last_capture_time < 0.0 ||
        frame->time_seconds - atlas->last_capture_time >= 0.09 ||
        frame->audio.onset) {
        atlas_capture(atlas, frame);
    }
    atlas->last_frame_time = frame->time_seconds;
}

static const Atlas_Slice *atlas_slice(const Song_Atlas_State *atlas, size_t chronological)
{
    return &atlas->slices[(atlas->first + chronological)%ATLAS_SLICE_COUNT];
}

static Vector3 atlas_vertex(const Atlas_Slice *slice, size_t band, size_t age)
{
    float across = (float)band/(float)(ATLAS_BAND_COUNT - 1);
    float edge = fabsf(across*2.0f - 1.0f);
    float amplitude = slice->bands[band];
    return (Vector3) {
        (across - 0.5f)*8.6f,
        -1.75f + amplitude*(1.15f + edge*1.75f),
        2.2f - (float)age*0.36f,
    };
}

static Color atlas_color(const Song_Atlas_State *atlas, const Atlas_Slice *slice,
                         size_t band, size_t age)
{
    float depth = 1.0f - (float)age/(float)(ATLAS_SLICE_COUNT - 1);
    float amplitude = slice->bands[band];
    float hue = fmodf(188.0f + atlas_hash_unit(atlas->seed, 3)*125.0f
                    + (float)band*3.4f + slice->flux*58.0f, 360.0f);
    return ColorFromHSV(hue, 0.62f + amplitude*0.34f,
                        atlas_clamp01(0.09f + depth*0.42f + amplitude*0.46f));
}

static void atlas_rl_vertex(Vector3 point, Color color)
{
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex3f(point.x, point.y, point.z);
}

static void atlas_draw_surface(const Song_Atlas_State *atlas)
{
    if (atlas->count < 2) return;

    rlBegin(RL_TRIANGLES);
    for (size_t row = 1; row < atlas->count; ++row) {
        const Atlas_Slice *newer = atlas_slice(atlas, atlas->count - row);
        const Atlas_Slice *older = atlas_slice(atlas, atlas->count - row - 1);
        for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
            Vector3 a = atlas_vertex(newer, band, row - 1);
            Vector3 b = atlas_vertex(newer, band + 1, row - 1);
            Vector3 c = atlas_vertex(older, band, row);
            Vector3 d = atlas_vertex(older, band + 1, row);
            Color ca = atlas_color(atlas, newer, band, row - 1);
            Color cb = atlas_color(atlas, newer, band + 1, row - 1);
            Color cc = atlas_color(atlas, older, band, row);
            Color cd = atlas_color(atlas, older, band + 1, row);
            atlas_rl_vertex(a, ca); atlas_rl_vertex(c, cc); atlas_rl_vertex(b, cb);
            atlas_rl_vertex(b, cb); atlas_rl_vertex(c, cc); atlas_rl_vertex(d, cd);
        }
    }
    rlEnd();

    // Sparse longitude lines make the surface read as an explorable map even
    // when amplitudes are quiet, without duplicating the full triangle budget.
    for (size_t band = 0; band < ATLAS_BAND_COUNT; band += 3) {
        for (size_t row = 1; row < atlas->count; ++row) {
            const Atlas_Slice *newer = atlas_slice(atlas, atlas->count - row);
            const Atlas_Slice *older = atlas_slice(atlas, atlas->count - row - 1);
            Color line = ColorAlpha(atlas_color(atlas, newer, band, row), 0.36f);
            scene_draw_tube(atlas_vertex(newer, band, row - 1),
                            atlas_vertex(older, band, row),
                            0.007f + newer->flux*0.006f, 5, line);
        }
    }
}

static void song_atlas_draw(const void *state, const Scene_Frame *frame,
                            const Scene_Renderer *renderer, Rectangle boundary)
{
    (void)renderer;
    const Song_Atlas_State *atlas = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float energy = atlas_clamp01(frame->audio.rms*1.8f);
    float flux = atlas_clamp01(frame->audio.spectral_flux*5.0f);
    float seed_phase = atlas_hash_unit(atlas->seed, 7)*2.0f*PI;
    float hue = fmodf(205.0f + atlas_hash_unit(atlas->seed, 2)*100.0f, 360.0f);
    Color background = ColorFromHSV(hue, 0.65f, 0.045f + energy*0.035f);
    DrawRectangleRec(boundary, background);

    int saved_width = rlGetFramebufferWidth();
    int saved_height = rlGetFramebufferHeight();
    if (saved_width < 1 || saved_height < 1) return;

    float coordinate_width = (float)saved_width;
    float coordinate_height = (float)saved_height;
    if (rlGetActiveFramebuffer() == 0) {
        int screen_width = GetScreenWidth();
        int screen_height = GetScreenHeight();
        if (screen_width < 1 || screen_height < 1) return;
        coordinate_width = (float)screen_width;
        coordinate_height = (float)screen_height;
    }

    float scale_x = (float)saved_width/coordinate_width;
    float scale_y = (float)saved_height/coordinate_height;
    int viewport_x = (int)roundf(boundary.x*scale_x);
    int viewport_width = (int)roundf(boundary.width*scale_x);
    int viewport_height = (int)roundf(boundary.height*scale_y);
    int viewport_top = (int)roundf((boundary.y + boundary.height)*scale_y);
    int viewport_y = saved_height - viewport_top;
    if (viewport_width < 1 || viewport_height < 1) return;

    rlDrawRenderBatchActive();
    rlViewport(viewport_x, viewport_y, viewport_width, viewport_height);
    rlSetFramebufferWidth(viewport_width);
    rlSetFramebufferHeight(viewport_height);

    float journey = (float)frame->time_seconds*0.065f + seed_phase;
    Camera3D camera = {
        .position = {
            sinf(journey)*1.15f,
            3.0f + cosf(journey*0.71f)*0.34f + energy*0.4f,
            7.1f - atlas->onset_pulse*0.35f,
        },
        .target = { sinf(journey*0.43f)*0.65f, -0.55f, -8.5f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 56.0f + flux*7.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    BeginMode3D(camera);
    float viewport_aspect = (float)viewport_width/(float)viewport_height;
    float full_aspect = (float)saved_width/(float)saved_height;
    rlMatrixMode(RL_PROJECTION);
    rlScalef(full_aspect/viewport_aspect, 1.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);

    atlas_draw_surface(atlas);

    // Onsets become navigational monoliths. Their repeatable lateral position
    // is derived from song time, never from frame count or wall-clock state.
    for (size_t row = 0; row < atlas->count; ++row) {
        const Atlas_Slice *slice = atlas_slice(atlas, atlas->count - row - 1);
        if (!slice->onset) continue;
        uint32_t time_key = (uint32_t)floor(slice->time_seconds*10.0);
        float side = (atlas_hash(atlas->seed, time_key) & 1U) ? 1.0f : -1.0f;
        float height = 0.45f + slice->rms*2.4f + slice->flux*0.8f;
        Vector3 position = { side*(3.75f + slice->flux*0.35f),
                             -1.7f + height*0.5f,
                             2.2f - (float)row*0.36f };
        Color marker = ColorFromHSV(fmodf(hue + 115.0f + slice->flux*45.0f, 360.0f),
                                    0.55f, 0.72f);
        DrawCube(position, 0.11f + slice->flux*0.16f, height, 0.13f, marker);
        DrawCubeWires(position, 0.18f + slice->flux*0.17f, height + 0.06f,
                      0.2f, ColorAlpha(RAYWHITE, 0.38f));
    }

    EndMode3D();
    rlDrawRenderBatchActive();
    rlSetFramebufferWidth(saved_width);
    rlSetFramebufferHeight(saved_height);
    rlViewport(0, 0, saved_width, saved_height);

    DrawRectangleRec(boundary, ColorAlpha(background, 0.045f));
}

const Scene_Descriptor scene_song_atlas_descriptor = {
    .id = SCENE_SONG_ATLAS,
    .name = "Song Atlas",
    .state_version = 1,
    .state_size = sizeof(Song_Atlas_State),
    .init = song_atlas_init,
    .update = song_atlas_update,
    .draw = song_atlas_draw,
};
