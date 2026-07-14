#include "scene.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

// The normal path renders a bounded whole-song analysis map prepared when the
// track loads. This rolling ring remains as a live-input fallback for sources
// that do not provide a complete decoded track.
#define ATLAS_BAND_COUNT SONG_ATLAS_BAND_COUNT
#define ATLAS_SLICE_COUNT SONG_ATLAS_MAX_SLICES

#define ATLAS_BASE_CAPTURE_INTERVAL 0.11
#define ATLAS_CAPTURE_INTERVAL \
    (ATLAS_BASE_CAPTURE_INTERVAL/(double)SONG_ATLAS_MAX_DETAIL)
#define ATLAS_SLICE_SPACING 0.29f

typedef struct {
    Song_Atlas_Slice slices[ATLAS_SLICE_COUNT];
    uint64_t seed;
    size_t first;
    size_t count;
    double last_frame_time;
    double last_capture_time;
    float filtered_bands[ATLAS_BAND_COUNT];
    float camera_energy;
    float camera_flux;
    bool pending_onset;
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
    memset(atlas->filtered_bands, 0, sizeof(atlas->filtered_bands));
    atlas->camera_energy = 0.0f;
    atlas->camera_flux = 0.0f;
    atlas->pending_onset = false;
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

    Song_Atlas_Slice *slice = &atlas->slices[index];
    slice->flux = atlas->camera_flux;
    slice->onset = atlas->pending_onset;
    for (size_t band = 0; band < ATLAS_BAND_COUNT; ++band) {
        float value = atlas_resample_band(frame, band);
        float neighbor = value;
        float weight = 1.0f;
        if (band > 0) {
            neighbor += atlas_resample_band(frame, band - 1)*0.55f;
            weight += 0.55f;
        }
        if (band + 1 < ATLAS_BAND_COUNT) {
            neighbor += atlas_resample_band(frame, band + 1)*0.55f;
            weight += 0.55f;
        }
        value = neighbor/weight;

        // Quick attacks preserve musical articulation; slower releases keep
        // adjacent terrain slices visually connected instead of sparkling.
        float base_response = value > atlas->filtered_bands[band] ? 0.62f : 0.24f;
        float response = 1.0f - powf(
            1.0f - base_response, 1.0f/(float)SONG_ATLAS_MAX_DETAIL);
        atlas->filtered_bands[band] +=
            (value - atlas->filtered_bands[band])*response;
        slice->bands[band] = powf(atlas_clamp01(atlas->filtered_bands[band]), 0.78f);
    }
    atlas->last_capture_time = frame->time_seconds;
    atlas->pending_onset = false;
}

static void song_atlas_update(void *state, const Scene_Frame *frame)
{
    Song_Atlas_State *atlas = state;
    double elapsed = frame->time_seconds - atlas->last_frame_time;

    // Backward time is a seek. A very large forward jump is also treated as a
    // discontinuity so unrelated regions are never joined by terrain.
    bool discontinuity = atlas->last_frame_time >= 0.0 &&
                         (elapsed < -0.001 || elapsed > 3.0);
    if (discontinuity) {
        atlas_clear_history(atlas);
        atlas->camera_energy = atlas_clamp01(frame->audio.rms*1.8f);
        atlas->camera_flux = atlas_clamp01(frame->audio.spectral_flux*5.0f);
    }

    float delta = frame->delta_seconds;
    if (delta < 0.0f) delta = 0.0f;
    if (delta > 0.1f) delta = 0.1f;
    float camera_blend = 1.0f - expf(-3.2f*delta);
    atlas->camera_energy +=
        (atlas_clamp01(frame->audio.rms*1.8f) - atlas->camera_energy)*camera_blend;
    atlas->camera_flux +=
        (atlas_clamp01(frame->audio.spectral_flux*5.0f) - atlas->camera_flux)*camera_blend;
    if (frame->audio.onset) {
        atlas->pending_onset = true;
    }

    // Capture on a fixed cadence. Geometry scrolls continuously between these
    // samples, so its apparent frame rate is the renderer frame rate rather
    // than the analysis-history rate. Onsets are latched into the next slice.
    if (atlas->last_capture_time < 0.0 ||
        frame->time_seconds - atlas->last_capture_time >= ATLAS_CAPTURE_INTERVAL) {
        atlas_capture(atlas, frame);
    }
    atlas->last_frame_time = frame->time_seconds;
}

static const Song_Atlas_Slice *atlas_slice(const Song_Atlas_State *atlas,
                                           size_t chronological)
{
    return &atlas->slices[(atlas->first + chronological)%ATLAS_SLICE_COUNT];
}

static float atlas_scroll_phase(const Song_Atlas_State *atlas, double time_seconds)
{
    if (atlas->last_capture_time < 0.0) return 0.0f;
    return atlas_clamp01((float)((time_seconds - atlas->last_capture_time)/
                                ATLAS_CAPTURE_INTERVAL));
}

static Vector3 atlas_vertex(const Song_Atlas_Slice *slice, size_t band, float age,
                            float scroll_phase)
{
    float across = (float)band/(float)(ATLAS_BAND_COUNT - 1);
    float amplitude = slice->bands[band];
    float terrain_profile = 0.42f + 0.58f*sinf(across*PI);
    return (Vector3) {
        (across - 0.5f)*9.4f,
        -1.68f + amplitude*(0.72f + terrain_profile*2.35f),
        1.40f - (age + scroll_phase)*ATLAS_SLICE_SPACING,
    };
}

static Vector3 atlas_live_vertex(const Song_Atlas_Slice *slice, size_t band,
                                 float source_age, float scroll_phase)
{
    return atlas_vertex(slice, band,
                        song_atlas_map_render_distance(source_age + scroll_phase),
                        0.0f);
}

static Color atlas_mix_color(Color from, Color to, float amount)
{
    amount = atlas_clamp01(amount);
    return (Color) {
        (unsigned char)(from.r + (to.r - from.r)*amount),
        (unsigned char)(from.g + (to.g - from.g)*amount),
        (unsigned char)(from.b + (to.b - from.b)*amount),
        (unsigned char)(from.a + (to.a - from.a)*amount),
    };
}

static Color atlas_hue_shift(Color color, float color_shift)
{
    Vector3 hsv = ColorToHSV(color);
    Color shifted = ColorFromHSV(fmodf(hsv.x + color_shift + 720.0f, 360.0f),
                                 hsv.y, hsv.z);
    shifted.a = color.a;
    return shifted;
}

static Color atlas_color(const Song_Atlas_Slice *slice, size_t band, float age,
                         float color_shift)
{
    static const Color bass = { 20, 72, 156, 255 };
    static const Color middle = { 42, 205, 184, 255 };
    static const Color treble = { 210, 73, 176, 255 };
    static const Color summit = { 255, 224, 172, 255 };
    float across = (float)band/(float)(ATLAS_BAND_COUNT - 1);
    Color frequency = across < 0.5f ?
        atlas_mix_color(bass, middle, across*2.0f) :
        atlas_mix_color(middle, treble, (across - 0.5f)*2.0f);
    float amplitude = slice->bands[band];
    Color color = atlas_mix_color(frequency, summit,
                                  atlas_clamp01(amplitude*0.52f + slice->flux*0.14f));
    float depth = 1.0f - (float)age/(float)(SONG_ATLAS_BASE_SLICES - 1);
    float exposure = 0.12f + depth*0.68f + amplitude*0.24f;
    color.r = (unsigned char)((float)color.r*atlas_clamp01(exposure));
    color.g = (unsigned char)((float)color.g*atlas_clamp01(exposure));
    color.b = (unsigned char)((float)color.b*atlas_clamp01(exposure));
    return atlas_hue_shift(color, color_shift);
}

static void atlas_rl_vertex(Vector3 point, Color color)
{
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex3f(point.x, point.y, point.z);
}

static void atlas_draw_surface(const Song_Atlas_State *atlas, float scroll_phase,
                               float pixel_scale, float contour_scale,
                               float color_shift, bool wireframe,
                               size_t detail_level)
{
    if (atlas->count < 2) return;
    size_t available = atlas->count;
    size_t render_count = song_atlas_map_render_sample_count(
        available, detail_level);
    if (render_count < 2) return;

    if (!wireframe) {
        rlBegin(RL_TRIANGLES);
        for (size_t sample = 1; sample < render_count; ++sample) {
            size_t newer_age = song_atlas_map_render_sample_index(
                0, available, render_count, sample - 1);
            size_t older_age = song_atlas_map_render_sample_index(
                0, available, render_count, sample);
            const Song_Atlas_Slice *newer = atlas_slice(
                atlas, atlas->count - newer_age - 1);
            const Song_Atlas_Slice *older = atlas_slice(
                atlas, atlas->count - older_age - 1);
            for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
                Vector3 a = atlas_live_vertex(newer, band, newer_age, scroll_phase);
                Vector3 b = atlas_live_vertex(newer, band + 1, newer_age, scroll_phase);
                Vector3 c = atlas_live_vertex(older, band, older_age, scroll_phase);
                Vector3 d = atlas_live_vertex(older, band + 1, older_age, scroll_phase);
                float newer_depth = song_atlas_map_render_distance((float)newer_age);
                float older_depth = song_atlas_map_render_distance((float)older_age);
                Color ca = atlas_color(newer, band, newer_depth, color_shift);
                Color cb = atlas_color(newer, band + 1, newer_depth, color_shift);
                Color cc = atlas_color(older, band, older_depth, color_shift);
                Color cd = atlas_color(older, band + 1, older_depth, color_shift);
                // Counter-clockwise from above: the atlas camera lives above the
                // heightfield, so upward-facing terrain must survive back-face
                // culling on every OpenGL target.
                atlas_rl_vertex(a, ca); atlas_rl_vertex(b, cb); atlas_rl_vertex(c, cc);
                atlas_rl_vertex(b, cb); atlas_rl_vertex(d, cd); atlas_rl_vertex(c, cc);
            }
        }
        rlEnd();
    }

    if (contour_scale <= 0.001f) return;

    // A single batched contour pass replaces hundreds of tiny cylinders. The
    // fixed-cadence cross-lines and sparse frequency meridians read as a map;
    // latched onsets become warm survey lines instead of arbitrary cubes.
    rlSetLineWidth(fmaxf(1.0f, pixel_scale*contour_scale));
    rlBegin(RL_LINES);
    size_t row_step = wireframe ? 1U : 5U;
    size_t band_step = wireframe ? 1U : 5U;
    for (size_t sample = 0; sample < render_count; sample += row_step) {
        size_t source_age = song_atlas_map_render_sample_index(
            0, available, render_count, sample);
        const Song_Atlas_Slice *slice = atlas_slice(
            atlas, atlas->count - source_age - 1);
        Color line = ColorAlpha(RAYWHITE, (wireframe ? 0.16f : 0.08f) + 0.22f*
                                (1.0f - (float)sample/(float)render_count));
        for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
            atlas_rl_vertex(atlas_live_vertex(
                                slice, band, source_age, scroll_phase), line);
            atlas_rl_vertex(atlas_live_vertex(
                                slice, band + 1, source_age, scroll_phase), line);
        }
    }
    for (size_t band = wireframe ? 0U : 2U;
         band + 1 < ATLAS_BAND_COUNT; band += band_step) {
        Color line = ColorAlpha(atlas_hue_shift(
                                (Color){ 93, 219, 205, 255 }, color_shift),
                                wireframe ? 0.32f : 0.18f);
        for (size_t sample = 1; sample < render_count; ++sample) {
            size_t newer_age = song_atlas_map_render_sample_index(
                0, available, render_count, sample - 1);
            size_t older_age = song_atlas_map_render_sample_index(
                0, available, render_count, sample);
            const Song_Atlas_Slice *newer = atlas_slice(
                atlas, atlas->count - newer_age - 1);
            const Song_Atlas_Slice *older = atlas_slice(
                atlas, atlas->count - older_age - 1);
            atlas_rl_vertex(atlas_live_vertex(
                                newer, band, newer_age, scroll_phase), line);
            atlas_rl_vertex(atlas_live_vertex(
                                older, band, older_age, scroll_phase), line);
        }
    }
    for (size_t sample = 0; sample < render_count; ++sample) {
        size_t source_age = song_atlas_map_render_sample_index(
            0, available, render_count, sample);
        const Song_Atlas_Slice *slice = atlas_slice(
            atlas, atlas->count - source_age - 1);
        if (!slice->onset) continue;
        Color landmark = ColorAlpha(atlas_hue_shift(
                                    (Color){ 255, 219, 150, 255 }, color_shift),
                                    0.24f + slice->flux*0.52f);
        for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
            Vector3 a = atlas_live_vertex(slice, band, source_age, scroll_phase);
            Vector3 b = atlas_live_vertex(
                slice, band + 1, source_age, scroll_phase);
            a.y += 0.025f;
            b.y += 0.025f;
            atlas_rl_vertex(a, landmark);
            atlas_rl_vertex(b, landmark);
        }
    }
    rlEnd();
    rlSetLineWidth(1.0f);
}

static float atlas_map_playhead(const Song_Atlas_Map *map, double time_seconds)
{
    if (!song_atlas_map_valid(map)) return 0.0f;
    double normalized = time_seconds/map->duration_seconds;
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    return (float)(normalized*(double)(map->count - 1));
}

static void atlas_map_dynamics(const Song_Atlas_Map *map, float playhead,
                               float *energy, float *flux)
{
    if (!song_atlas_map_valid(map) || energy == NULL || flux == NULL) return;
    size_t lower = (size_t)floorf(playhead);
    if (lower >= map->count - 1) {
        *energy = map->slices[map->count - 1].rms;
        *flux = map->slices[map->count - 1].flux;
        return;
    }
    float amount = playhead - (float)lower;
    *energy = map->slices[lower].rms +
              (map->slices[lower + 1].rms - map->slices[lower].rms)*amount;
    *flux = map->slices[lower].flux +
            (map->slices[lower + 1].flux - map->slices[lower].flux)*amount;
}

static Vector3 atlas_map_playhead_vertex(const Song_Atlas_Map *map,
                                         float playhead, size_t band)
{
    size_t lower = (size_t)floorf(playhead);
    if (lower >= map->count - 1) {
        return atlas_vertex(&map->slices[map->count - 1], band, 0.0f, 0.0f);
    }
    size_t upper = lower + 1;
    float amount = playhead - (float)lower;
    Vector3 a = atlas_vertex(&map->slices[lower], band,
                             -amount/(float)SONG_ATLAS_MAX_DETAIL, 0.0f);
    Vector3 b = atlas_vertex(&map->slices[upper], band,
                             (1.0f - amount)/(float)SONG_ATLAS_MAX_DETAIL, 0.0f);
    return (Vector3) {
        a.x + (b.x - a.x)*amount,
        a.y + (b.y - a.y)*amount,
        a.z + (b.z - a.z)*amount,
    };
}

static Vector3 atlas_complete_vertex(const Song_Atlas_Slice *slice, size_t band,
                                     float map_distance)
{
    return atlas_vertex(slice, band,
                        song_atlas_map_render_distance(map_distance), 0.0f);
}

static void atlas_draw_complete_surface(const Song_Atlas_Map *map,
                                        double time_seconds, float pixel_scale,
                                        float contour_scale, float color_shift,
                                        bool wireframe, size_t detail_level)
{
    if (!song_atlas_map_valid(map)) return;
    float playhead = atlas_map_playhead(map, time_seconds);
    const size_t history = 10U*SONG_ATLAS_MAX_DETAIL;
    size_t first = playhead > (float)history ?
                   (size_t)floorf(playhead) - history : 0;
    size_t available = map->count - first;
    size_t sample_count = song_atlas_map_render_sample_count(
        available, detail_level);
    if (sample_count < 2) return;

    if (!wireframe) {
        rlBegin(RL_TRIANGLES);
        for (size_t sample = 0; sample + 1 < sample_count; ++sample) {
            size_t row = song_atlas_map_render_sample_index(
                first, available, sample_count, sample);
            size_t next_row = song_atlas_map_render_sample_index(
                first, available, sample_count, sample + 1);
            const Song_Atlas_Slice *near = &map->slices[row];
            const Song_Atlas_Slice *far = &map->slices[next_row];
            float near_distance = (float)row - playhead;
            float far_distance = (float)next_row - playhead;
            float near_depth = fmaxf(0.0f, near_distance)/
                               (float)SONG_ATLAS_MAX_DETAIL;
            float far_depth = fmaxf(0.0f, far_distance)/
                              (float)SONG_ATLAS_MAX_DETAIL;
            for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
                Vector3 a = atlas_complete_vertex(near, band, near_distance);
                Vector3 b = atlas_complete_vertex(near, band + 1, near_distance);
                Vector3 c = atlas_complete_vertex(far, band, far_distance);
                Vector3 d = atlas_complete_vertex(far, band + 1, far_distance);
                Color ca = atlas_color(near, band, near_depth, color_shift);
                Color cb = atlas_color(near, band + 1, near_depth, color_shift);
                Color cc = atlas_color(far, band, far_depth, color_shift);
                Color cd = atlas_color(far, band + 1, far_depth, color_shift);
                atlas_rl_vertex(a, ca); atlas_rl_vertex(b, cb); atlas_rl_vertex(c, cc);
                atlas_rl_vertex(b, cb); atlas_rl_vertex(d, cd); atlas_rl_vertex(c, cc);
            }
        }
        rlEnd();
    }

    if (contour_scale <= 0.001f) return;

    rlSetLineWidth(fmaxf(1.0f, pixel_scale*contour_scale));
    rlBegin(RL_LINES);
    for (size_t sample = 0; sample < sample_count; ++sample) {
        size_t row = song_atlas_map_render_sample_index(
            first, available, sample_count, sample);
        float distance = (float)row - playhead;
        if (!wireframe && row%8 != 0 && !map->slices[row].onset) continue;
        Color line = map->slices[row].onset ?
            ColorAlpha(atlas_hue_shift((Color){ 255, 219, 150, 255 },
                                       color_shift),
                       0.28f + map->slices[row].flux*0.50f) :
            ColorAlpha(RAYWHITE, 0.12f);
        for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
            atlas_rl_vertex(atlas_complete_vertex(
                                 &map->slices[row], band, distance), line);
            atlas_rl_vertex(atlas_complete_vertex(
                                 &map->slices[row], band + 1, distance), line);
        }
    }
    size_t band_step = wireframe ? 1U : 5U;
    for (size_t band = wireframe ? 0U : 2U;
         band + 1 < ATLAS_BAND_COUNT; band += band_step) {
        Color line = ColorAlpha(atlas_hue_shift(
                                (Color){ 93, 219, 205, 255 }, color_shift),
                                wireframe ? 0.30f : 0.16f);
        for (size_t sample = 0; sample + 1 < sample_count; ++sample) {
            size_t row = song_atlas_map_render_sample_index(
                first, available, sample_count, sample);
            size_t next_row = song_atlas_map_render_sample_index(
                first, available, sample_count, sample + 1);
            float near_distance = (float)row - playhead;
            float far_distance = (float)next_row - playhead;
            atlas_rl_vertex(atlas_complete_vertex(
                                 &map->slices[row], band, near_distance), line);
            atlas_rl_vertex(atlas_complete_vertex(
                                 &map->slices[next_row], band, far_distance), line);
        }
    }
    Color playhead_color = { 255, 238, 196, 255 };
    for (size_t band = 0; band + 1 < ATLAS_BAND_COUNT; ++band) {
        atlas_rl_vertex(atlas_map_playhead_vertex(map, playhead, band),
                        playhead_color);
        atlas_rl_vertex(atlas_map_playhead_vertex(map, playhead, band + 1),
                        playhead_color);
    }
    rlEnd();
    rlSetLineWidth(1.0f);
}

static void song_atlas_draw(const void *state, const Scene_Frame *frame,
                            const Scene_Renderer *renderer, Rectangle boundary)
{
    const Song_Atlas_State *atlas = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float energy = atlas->camera_energy;
    float flux = atlas->camera_flux;
    float height_scale = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_HEIGHT);
    float width_scale = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_WIDTH);
    float depth_scale = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_DEPTH);
    float camera_scale = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_CAMERA);
    float contour_scale = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_CONTOURS);
    float color_shift = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_COLOR);
    float speed_scale = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_SPEED);
    bool wireframe = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_WIREFRAME) >= 0.5f;
    size_t detail_level = (size_t)lroundf(scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_DETAIL));
    if (detail_level < 1) detail_level = 1;
    if (detail_level > SONG_ATLAS_MAX_DETAIL) {
        detail_level = SONG_ATLAS_MAX_DETAIL;
    }
    bool hue_motion = scene_settings_get(
        renderer->settings, SCENE_SONG_ATLAS, ATLAS_SETTING_HUE_MOTION) >= 0.5f;
    if (hue_motion) {
        float hue_energy = frame->audio.rms;
        float hue_flux = frame->audio.spectral_flux;
        if (song_atlas_map_valid(renderer->song_atlas_map)) {
            float playhead = atlas_map_playhead(renderer->song_atlas_map,
                                                frame->time_seconds);
            atlas_map_dynamics(renderer->song_atlas_map, playhead,
                               &hue_energy, &hue_flux);
        }
        hue_energy = atlas_clamp01(hue_energy);
        hue_flux = atlas_clamp01(hue_flux);
        float hue_wave = sinf((float)frame->time_seconds*
                              (0.70f + hue_energy*0.55f));
        color_shift += fmodf((float)frame->time_seconds*12.0f +
                             hue_wave*(14.0f + hue_energy*34.0f) +
                             hue_flux*82.0f, 360.0f);
    }
    float seed_phase = atlas_hash_unit(atlas->seed, 7)*2.0f*PI;
    float semantic_weight = frame->semantic.available ? frame->semantic.confidence : 0.0f;
    float hue = fmodf(205.0f + atlas_hash_unit(atlas->seed, 2)*100.0f +
                      frame->semantic.valence*70.0f*semantic_weight +
                      color_shift + 720.0f, 360.0f);
    Color background = ColorFromHSV(hue, 0.70f, 0.038f + energy*0.025f);
    Color horizon = ColorFromHSV(fmodf(hue + 24.0f, 360.0f), 0.64f,
                                 0.075f + energy*0.035f);
    DrawRectangleGradientV((int)boundary.x, (int)boundary.y,
                           (int)boundary.width, (int)boundary.height,
                           background, horizon);

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

    float journey = (float)frame->time_seconds*0.025f*speed_scale + seed_phase;
    float target_z = -5.4f;
    if (song_atlas_map_valid(renderer->song_atlas_map)) {
        float playhead = atlas_map_playhead(renderer->song_atlas_map,
                                            frame->time_seconds);
        float remaining = ((float)(renderer->song_atlas_map->count - 1) - playhead)/
                          (float)SONG_ATLAS_MAX_DETAIL;
        float focus_slices = fminf(18.0f, fmaxf(4.0f, remaining*0.45f));
        target_z = 1.40f - focus_slices*ATLAS_SLICE_SPACING;
    }
    target_z *= depth_scale;
    Camera3D camera = {
        .position = {
            sinf(journey)*0.52f,
            (4.62f + cosf(journey*0.47f)*0.14f + energy*0.20f)*camera_scale,
            7.35f,
        },
        .target = { sinf(journey*0.31f)*0.34f, -0.58f*height_scale, target_z },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 49.0f + flux*2.2f,
        .projection = CAMERA_PERSPECTIVE,
    };

    BeginMode3D(camera);
    float viewport_aspect = (float)viewport_width/(float)viewport_height;
    float full_aspect = (float)saved_width/(float)saved_height;
    rlMatrixMode(RL_PROJECTION);
    rlScalef(full_aspect/viewport_aspect, 1.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);
    rlScalef(width_scale, height_scale, depth_scale);

    if (song_atlas_map_valid(renderer->song_atlas_map)) {
        atlas_draw_complete_surface(renderer->song_atlas_map,
                                    frame->time_seconds,
                                    renderer->pixel_scale, contour_scale,
                                    color_shift, wireframe, detail_level);
    } else {
        atlas_draw_surface(atlas, atlas_scroll_phase(atlas, frame->time_seconds),
                           renderer->pixel_scale, contour_scale, color_shift,
                           wireframe, detail_level);
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
    .state_version = 4,
    .state_size = sizeof(Song_Atlas_State),
    .init = song_atlas_init,
    .update = song_atlas_update,
    .draw = song_atlas_draw,
};
