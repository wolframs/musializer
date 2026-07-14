#include "scene.h"

#include <math.h>

enum {
    LOOM_MIN_COLUMNS = 28,
    LOOM_MAX_COLUMNS = 144,
    LOOM_MIN_ROWS = 16,
    LOOM_MAX_ROWS = 72,
    LOOM_WARP_SEGMENTS = 32,
};

typedef struct Loom_State {
    uint64_t seed;
} Loom_State;

static float loom_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint64_t loom_mix(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static float loom_unit(uint64_t seed, uint64_t salt)
{
    return (float)(loom_mix(seed ^ salt) & UINT64_C(0xffff))/65535.0f;
}

static void loom_init(void *state, uint64_t seed)
{
    Loom_State *loom = state;
    loom->seed = seed;
}

static Semantic_Frame loom_semantic_at(const Scene_Frame *frame, double time)
{
    Semantic_Frame semantic = {0};
    if (semantic_lane_sample(frame->events, time, &semantic)) return semantic;
    if (frame->semantic.available) return frame->semantic;
    semantic.available = true;
    semantic.energy = loom_clamp01(frame->audio.rms*1.8f);
    semantic.tension = loom_clamp01(frame->audio.spectral_flux*4.0f);
    semantic.valence = 0.0f;
    semantic.confidence = 0.35f;
    return semantic;
}

static Color loom_thread_color(Semantic_Frame semantic, float saturation_scale,
                               float brightness)
{
    float valence = loom_clamp01((semantic.valence + 1.0f)*0.5f);
    float hue = 218.0f + (28.0f - 218.0f)*valence;
    float saturation = fminf(1.0f, (0.42f + semantic.tension*0.38f)*
                                   saturation_scale);
    return ColorFromHSV(hue, saturation, loom_clamp01(brightness));
}

// Weave topology is a structural consequence of the arc, not a tint: calm
// passages interlace as plain weave, rising tension shifts the cloth into a
// 2/2 twill's diagonal ribs, and peaks bind as a warp-faced satin whose long
// floats read dense and glossy. Returns whether the warp passes over the
// weft at this crossing.
static bool loom_warp_over(size_t row, size_t column, float tension)
{
    if (tension < 0.34f) return ((row + column) & 1U) == 0U;
    if (tension < 0.67f) return ((row + column*2U) & 3U) < 2U;
    return ((row*3U + column) % 5U) != 0U;
}

// Threads pinch at each crossing and bulge between them; this crimp is most
// of what makes raster lines read as spun thread instead of wireframe.
static float loom_crimp(float along, float crossings)
{
    return 1.05f - 0.30f*fabsf(sinf(along*crossings*PI));
}

static void loom_draw(const void *state, const Scene_Frame *frame,
                      const Scene_Renderer *renderer, Rectangle boundary)
{
    const Loom_State *loom = state;
    if (loom == NULL || frame == NULL || renderer == NULL ||
        boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float density_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_DENSITY);
    float weight_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_WEIGHT);
    float complexity_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_COMPLEXITY);
    float edge_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_EDGE);
    float saturation_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_SATURATION);
    float motion_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_MOTION);
    float glint_scale = scene_settings_get(
        renderer->settings, SCENE_LOOM, LOOM_SETTING_GLINTS);
    float pixel_scale = renderer->pixel_scale > 0.0f ? renderer->pixel_scale : 1.0f;

    double duration = frame->duration_seconds > 0.0 ? frame->duration_seconds :
                      fmax(1.0, frame->time_seconds + 1.0);
    float progress = loom_clamp01((float)(frame->time_seconds/duration));
    Rectangle cloth = {
        boundary.x + boundary.width*0.055f,
        boundary.y + boundary.height*0.07f,
        boundary.width*0.89f,
        boundary.height*0.86f,
    };
    Semantic_Frame current = loom_semantic_at(frame, frame->time_seconds);
    Color background = ColorFromHSV(224.0f, 0.48f,
                                    0.025f + current.energy*0.025f);
    DrawRectangleRec(boundary, background);
    DrawRectangleRec(cloth, ColorAlpha((Color){13, 14, 28, 255}, 0.92f));

    size_t columns = (size_t)lroundf(72.0f*density_scale);
    if (columns < LOOM_MIN_COLUMNS) columns = LOOM_MIN_COLUMNS;
    if (columns > LOOM_MAX_COLUMNS) columns = LOOM_MAX_COLUMNS;
    size_t rows = (size_t)lroundf(34.0f*density_scale);
    if (rows < LOOM_MIN_ROWS) rows = LOOM_MIN_ROWS;
    if (rows > LOOM_MAX_ROWS) rows = LOOM_MAX_ROWS;
    size_t visible_columns = (size_t)ceilf((float)columns*progress);
    if (visible_columns > columns) visible_columns = columns;
    float woven_width = cloth.width*progress;

    // Each column is a fixed sample of the song at its own time, so the
    // finished tapestry is a stable record of the whole arc.
    Semantic_Frame column_semantic[LOOM_MAX_COLUMNS];
    for (size_t column = 0; column < columns; ++column) {
        float x_t = ((float)column + 0.5f)/(float)columns;
        column_semantic[column] = loom_semantic_at(frame, (double)x_t*duration);
    }

    // Bare warp waits ahead of the fell: pale, straight, uncolored threads
    // that the song has not woven yet.
    for (size_t column = visible_columns; column < columns; ++column) {
        float x = cloth.x + (((float)column + 0.5f)/(float)columns)*cloth.width;
        float shade = 0.30f + loom_unit(loom->seed, column*3U + 2U)*0.18f;
        DrawLineEx((Vector2){x, cloth.y}, (Vector2){x, cloth.y + cloth.height},
                   0.6f*pixel_scale*weight_scale,
                   ColorAlpha(ColorFromHSV(224.0f, 0.10f, shade), 0.30f));
    }

    // Woven warp: swaying segmented threads with crimp at every weft row.
    for (size_t column = 0; column < visible_columns; ++column) {
        Semantic_Frame semantic = column_semantic[column];
        float confidence = semantic.available ?
                           loom_clamp01(semantic.confidence) : 0.0f;
        float complexity = (0.55f + semantic.tension*1.45f)*complexity_scale;
        float x = cloth.x + (((float)column + 0.5f)/(float)columns)*cloth.width;
        float phase = loom_unit(loom->seed, column + 1U)*2.0f*PI;
        Color color = loom_thread_color(
            semantic, saturation_scale,
            0.32f + semantic.energy*0.54f + confidence*0.08f);
        Vector2 previous = {x, cloth.y};
        for (int segment = 1; segment <= LOOM_WARP_SEGMENTS; ++segment) {
            float y_t = (float)segment/(float)LOOM_WARP_SEGMENTS;
            float lift = sinf(y_t*PI*complexity*2.0f + phase +
                              frame->audio.beat_phase*PI*motion_scale)*
                         (1.0f + semantic.tension*3.6f)*pixel_scale;
            Vector2 point = {x + lift, cloth.y + y_t*cloth.height};
            float thickness = (0.45f + semantic.energy*1.25f)*pixel_scale*
                              weight_scale*loom_crimp(y_t, (float)rows);
            DrawLineEx(previous, point, thickness,
                       ColorAlpha(color, 0.34f + confidence*0.46f));
            previous = point;
        }
    }

    // Weft: each pick is drawn with a soft shadow under its lit face so the
    // thread reads as a rounded body catching light, not a flat stroke.
    for (size_t row = 0; row < rows && woven_width > 0.0f; ++row) {
        float y_t = ((float)row + 0.5f)/(float)rows;
        float y = cloth.y + y_t*cloth.height;
        Vector2 previous = {cloth.x, y};
        for (size_t column = 1; column <= visible_columns; ++column) {
            float x_t = (float)column/(float)columns;
            Semantic_Frame semantic = column_semantic[column - 1U];
            float mid_t = (x_t - 0.5f/(float)columns);
            float over = ((row + column) & 1U) ? 1.0f : -1.0f;
            float lift = over*(0.7f + semantic.tension*2.4f)*pixel_scale*
                         complexity_scale;
            Vector2 point = {
                cloth.x + fminf(woven_width, x_t*cloth.width),
                y + lift,
            };
            float thickness = (0.75f + semantic.energy*1.10f)*pixel_scale*
                              weight_scale*loom_crimp(mid_t, (float)columns);
            Color color = loom_thread_color(
                semantic, saturation_scale,
                0.46f + semantic.energy*0.44f + semantic.tension*0.10f);
            Vector2 shadow_offset = {0.6f*pixel_scale, 0.7f*pixel_scale};
            DrawLineEx((Vector2){previous.x + shadow_offset.x,
                                 previous.y + shadow_offset.y},
                       (Vector2){point.x + shadow_offset.x,
                                 point.y + shadow_offset.y},
                       thickness*1.05f, ColorAlpha(BLACK, 0.30f));
            DrawLineEx(previous, point, thickness,
                       ColorAlpha(color, 0.48f + semantic.confidence*0.34f));
            previous = point;
            if (point.x >= cloth.x + woven_width) break;
        }
    }

    // Interlace: wherever the weave pattern binds warp over weft, a short
    // bright stub of the warp thread crosses back on top and catches the
    // light. This is what makes the grid read as cloth.
    float row_pitch = cloth.height/(float)rows;
    for (size_t column = 0; column < visible_columns; ++column) {
        Semantic_Frame semantic = column_semantic[column];
        float tension = loom_clamp01(semantic.tension);
        float x = cloth.x + (((float)column + 0.5f)/(float)columns)*cloth.width;
        if (x > cloth.x + woven_width) break;
        float thickness = (0.50f + semantic.energy*1.30f)*pixel_scale*
                          weight_scale;
        Color lit = loom_thread_color(
            semantic, saturation_scale,
            0.52f + semantic.energy*0.48f);
        for (size_t row = 0; row < rows; ++row) {
            if (!loom_warp_over(row, column, tension)) continue;
            float y = cloth.y + (((float)row + 0.5f)/(float)rows)*cloth.height;
            float half = row_pitch*0.28f;
            DrawLineEx((Vector2){x, y - half}, (Vector2){x, y + half},
                       thickness,
                       ColorAlpha(lit, 0.44f + semantic.confidence*0.36f));
        }
    }

    // The fell: a bright working edge where the next pick is beaten in,
    // shimmering with the beat.
    float frontier_x = cloth.x + woven_width;
    if (progress < 1.0f) {
        DrawRectangleGradientH(
            (int)fmaxf(cloth.x, frontier_x - 22.0f*pixel_scale*edge_scale),
            (int)cloth.y, (int)(22.0f*pixel_scale*edge_scale), (int)cloth.height,
            ColorAlpha(background, 0.0f),
            ColorAlpha(loom_thread_color(current, saturation_scale, 1.0f), 0.30f));
        float shimmer = 0.34f + 0.22f*motion_scale*
                        sinf(frame->audio.beat_phase*2.0f*PI);
        DrawLineEx((Vector2){frontier_x, cloth.y},
                   (Vector2){frontier_x, cloth.y + cloth.height},
                   fmaxf(1.0f, 1.6f*pixel_scale),
                   ColorAlpha(loom_thread_color(current, saturation_scale, 1.0f),
                              loom_clamp01(shimmer)));
    }

    // Glints land on the fabric structure itself: sequins at crossings,
    // not sparks floating over it.
    BeginBlendMode(BLEND_ADDITIVE);
    if (frame->audio.onset || current.tension > 0.62f) {
        size_t glints = (size_t)lroundf(10.0f*glint_scale);
        for (size_t i = 0; i < glints && visible_columns > 0; ++i) {
            size_t column = (size_t)(loom_unit(
                loom->seed, frame->frame_index + i*7U)*
                (float)(visible_columns - 1U) + 0.5f);
            size_t row = (size_t)(loom_unit(
                loom->seed, frame->frame_index + i*7U + 1U)*
                (float)(rows - 1U) + 0.5f);
            float x = cloth.x + (((float)column + 0.5f)/(float)columns)*cloth.width;
            float y = cloth.y + (((float)row + 0.5f)/(float)rows)*cloth.height;
            if (x > cloth.x + woven_width) continue;
            Color color = loom_thread_color(current, saturation_scale, 1.0f);
            DrawCircleV((Vector2){x, y},
                        (1.0f + current.tension*2.4f)*pixel_scale*glint_scale,
                        ColorAlpha(color, 0.18f + current.tension*0.30f));
        }
    }
    EndBlendMode();
    DrawRectangleLinesEx(cloth, fmaxf(1.0f, pixel_scale),
                         ColorAlpha(RAYWHITE, 0.12f));
}

const Scene_Descriptor scene_loom_descriptor = {
    .id = SCENE_LOOM,
    .name = "Loom",
    .state_version = 1,
    .state_size = sizeof(Loom_State),
    .init = loom_init,
    .draw = loom_draw,
};
