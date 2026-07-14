#include "scene.h"

#include <math.h>

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
    DrawRectangleRec(cloth, ColorAlpha((Color){8, 9, 20, 255}, 0.92f));

    size_t columns = (size_t)lroundf(72.0f*density_scale);
    if (columns < 28U) columns = 28U;
    if (columns > 144U) columns = 144U;
    size_t visible_columns = (size_t)ceilf((float)columns*progress);
    if (visible_columns > columns) visible_columns = columns;
    for (size_t column = 0; column < visible_columns; ++column) {
        float x_t = ((float)column + 0.5f)/(float)columns;
        double sample_time = (double)x_t*duration;
        Semantic_Frame semantic = loom_semantic_at(frame, sample_time);
        float confidence = semantic.available ? loom_clamp01(semantic.confidence) : 0.0f;
        float complexity = (0.55f + semantic.tension*1.45f)*complexity_scale;
        float x = cloth.x + x_t*cloth.width;
        float phase = loom_unit(loom->seed, column + 1U)*2.0f*PI;
        Color color = loom_thread_color(
            semantic, saturation_scale,
            0.32f + semantic.energy*0.54f + confidence*0.08f);
        Vector2 previous = {x, cloth.y};
        enum { WARP_SEGMENTS = 32 };
        for (int segment = 1; segment <= WARP_SEGMENTS; ++segment) {
            float y_t = (float)segment/(float)WARP_SEGMENTS;
            float lift = sinf(y_t*PI*complexity*2.0f + phase +
                              frame->audio.beat_phase*PI*motion_scale)*
                         (1.0f + semantic.tension*3.6f)*pixel_scale;
            Vector2 point = {x + lift, cloth.y + y_t*cloth.height};
            DrawLineEx(previous, point,
                       (0.45f + semantic.energy*1.25f)*pixel_scale*weight_scale,
                       ColorAlpha(color, 0.34f + confidence*0.46f));
            previous = point;
        }
    }

    size_t rows = (size_t)lroundf(34.0f*density_scale);
    if (rows < 16U) rows = 16U;
    if (rows > 72U) rows = 72U;
    float woven_width = cloth.width*progress;
    for (size_t row = 0; row < rows && woven_width > 0.0f; ++row) {
        float y_t = ((float)row + 0.5f)/(float)rows;
        float y = cloth.y + y_t*cloth.height;
        Vector2 previous = {cloth.x, y};
        for (size_t column = 1; column <= visible_columns; ++column) {
            float x_t = (float)column/(float)columns;
            double sample_time = (double)x_t*duration;
            Semantic_Frame semantic = loom_semantic_at(frame, sample_time);
            float over = ((row + column) & 1U) ? 1.0f : -1.0f;
            float lift = over*(0.7f + semantic.tension*2.4f)*pixel_scale*
                         complexity_scale;
            Vector2 point = {
                cloth.x + fminf(woven_width, x_t*cloth.width),
                y + lift,
            };
            Color color = loom_thread_color(
                semantic, saturation_scale,
                0.30f + semantic.energy*0.48f + semantic.tension*0.10f);
            DrawLineEx(previous, point,
                       (0.55f + semantic.energy)*pixel_scale*weight_scale,
                       ColorAlpha(color, 0.32f + semantic.confidence*0.42f));
            previous = point;
            if (point.x >= cloth.x + woven_width) break;
        }
    }

    float frontier_x = cloth.x + woven_width;
    if (progress < 1.0f) {
        DrawRectangleGradientH(
            (int)fmaxf(cloth.x, frontier_x - 22.0f*pixel_scale*edge_scale),
            (int)cloth.y, (int)(22.0f*pixel_scale*edge_scale), (int)cloth.height,
            ColorAlpha(background, 0.0f),
            ColorAlpha(loom_thread_color(current, saturation_scale, 1.0f), 0.30f));
    }
    BeginBlendMode(BLEND_ADDITIVE);
    if (frame->audio.onset || current.tension > 0.62f) {
        size_t glints = (size_t)lroundf(10.0f*glint_scale);
        for (size_t i = 0; i < glints; ++i) {
            float x = cloth.x + loom_unit(loom->seed, frame->frame_index + i*7U)*
                                fmaxf(1.0f, woven_width);
            float y = cloth.y + loom_unit(loom->seed, frame->frame_index + i*7U + 1U)*
                                cloth.height;
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
