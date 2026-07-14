#include "scene.h"
#include "scene_event_merge.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

#include "scene_draw.h"
#include "scene_constellation_motion.h"

enum { CONSTELLATION_NODE_COUNT = 72 };

typedef struct Constellation_State {
    uint64_t seed;
    Constellation_Motion motion;
} Constellation_State;

static float constellation_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint64_t constellation_mix(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static float constellation_unit(uint64_t seed, uint64_t salt)
{
    return (float)(constellation_mix(seed ^ (salt + UINT64_C(0x9e3779b97f4a7c15))) &
                   UINT64_C(0xffff))/65535.0f;
}

static void constellation_init(void *state, uint64_t seed)
{
    Constellation_State *constellation = state;
    memset(constellation, 0, sizeof(*constellation));
    constellation->seed = seed;
    constellation_motion_init(&constellation->motion);
}

static void constellation_update(void *state, const Scene_Frame *frame)
{
    Constellation_State *constellation = state;
    Constellation_Motion_Input input = {
        .time_seconds = frame->time_seconds,
        .delta_seconds = frame->delta_seconds,
        .rms = frame->audio.rms,
        .spectral_flux = frame->audio.spectral_flux,
        .onset = frame->audio.onset,
    };
    constellation_motion_update(&constellation->motion, &input);
}

static float constellation_band(const Scene_Frame *frame, size_t index)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;
    return constellation_clamp01(frame->audio.bands[index%frame->audio.bands_count]);
}

static Vector3 constellation_base_position(uint64_t seed, size_t index,
                                           size_t node_count, float time,
                                           float amplitude)
{
    float y = 1.0f - 2.0f*((float)index + 0.5f)/(float)node_count;
    float radius = sqrtf(fmaxf(0.0f, 1.0f - y*y));
    float longitude = (float)index*2.39996323f
                    + constellation_unit(seed, index*3U + 1U)*0.28f;
    float breathing = 1.0f + amplitude*0.18f
                    + sinf(time*0.27f + constellation_unit(seed, index*3U + 2U)*6.2831853f)*0.035f;
    return (Vector3) {
        cosf(longitude)*radius*3.8f*breathing,
        y*3.15f*breathing,
        sinf(longitude)*radius*3.8f*breathing,
    };
}

static float constellation_event_strength(const Scene_Frame *frame, size_t node,
                                           size_t node_count, float event_duration,
                                           size_t event_reach,
                                           uint32_t *dominant_type,
                                           uint64_t *dominant_id)
{
    if (frame->events.events == NULL || frame->events.count == 0) return 0.0f;
    size_t count = frame->events.count;
    if (count > SCENE_EVENT_MERGE_CAPACITY) count = SCENE_EVENT_MERGE_CAPACITY;
    float result = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const Event_Record *event = &frame->events.events[i];
        if (!event_record_is_valid(event)) continue;
        double age = frame->time_seconds - event->timestamp_seconds;
        if (age < 0.0 || age > event_duration) continue;
        size_t event_node = (size_t)(constellation_mix(event->id ^
                                   ((uint64_t)event->type << 48)) % node_count);
        size_t distance = node > event_node ? node - event_node : event_node - node;
        if (distance > event_reach && distance < node_count - event_reach) continue;
        float payload = fabsf(event->values[0]);
        float strength = expf(-(float)age*2.1f)*(0.45f + constellation_clamp01(payload)*0.55f);
        if (distance != 0) strength *= 0.34f;
        if (strength > result) {
            result = strength;
            *dominant_type = event->type;
            *dominant_id = event->id;
        }
    }
    return constellation_clamp01(result);
}

static Color constellation_event_color(uint32_t type, float hue, float brightness)
{
    switch (type) {
    case EVENT_TYPE_LYRIC: hue = 322.0f; break;
    case EVENT_TYPE_SEMANTIC: hue = 42.0f; break;
    case EVENT_TYPE_CUE: hue = 164.0f; break;
    case EVENT_TYPE_CUSTOM: hue = 268.0f; break;
    default: break;
    }
    return ColorFromHSV(hue, type == 0 ? 0.48f : 0.82f,
                        constellation_clamp01(brightness));
}

static void constellation_draw(const void *state, const Scene_Frame *frame,
                               const Scene_Renderer *renderer, Rectangle boundary)
{
    const Constellation_State *constellation = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float motion_scale = scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_MOTION);
    float field_scale = scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_SCALE);
    float glow_scale = scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_GLOW);
    float event_duration = scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_EVENT_DURATION);
    size_t event_reach = (size_t)lroundf(scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_EVENT_REACH));
    float hue_swing = scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_HUE_SWING);
    size_t density = (size_t)lroundf(scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_DENSITY));
    float web = scene_settings_get(
        renderer->settings, SCENE_CONSTELLATION, CONSTELLATION_SETTING_WEB);
    if (density < 1U) density = 1U;
    if (density > 3U) density = 3U;
    size_t node_count = CONSTELLATION_NODE_COUNT*density/3U;
    if (event_reach*2U >= node_count) event_reach = node_count/2U - 1U;
    float time = (float)frame->time_seconds*motion_scale;
    float semantic_weight = frame->semantic.available ? frame->semantic.confidence : 0.0f;
    float base_hue = fmodf(201.0f + constellation_unit(constellation->seed, 9)*95.0f
                         + time*1.8f + frame->semantic.valence*hue_swing*semantic_weight,
                           360.0f);
    Color background = ColorFromHSV(base_hue, 0.72f,
                                    0.035f + constellation->motion.energy*0.035f);
    DrawRectangleRec(boundary, background);
    // A soft off-center nebula gives the star field a deep sky to sit in
    // instead of flat black.
    Vector2 nebula_center = {
        boundary.x + boundary.width*
            (0.36f + constellation_unit(constellation->seed, 11)*0.28f),
        boundary.y + boundary.height*
            (0.34f + constellation_unit(constellation->seed, 12)*0.30f),
    };
    float nebula_radius = fmaxf(boundary.width, boundary.height)*0.62f;
    DrawCircleGradient((int)nebula_center.x, (int)nebula_center.y, nebula_radius,
                       ColorAlpha(ColorFromHSV(fmodf(base_hue + 24.0f, 360.0f),
                                               0.66f, 0.16f +
                                               constellation->motion.energy*0.10f),
                                  0.55f),
                       BLANK);

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

    float orbit = time*(0.035f + constellation->motion.flux*0.055f)
                + constellation_unit(constellation->seed, 10)*6.2831853f;
    Camera3D camera = {
        .position = { cosf(orbit)*8.9f, 1.2f + sinf(orbit*0.61f)*0.8f,
                      sinf(orbit)*8.9f },
        .target = { 0.0f, 0.0f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 56.0f + constellation->motion.onset_pulse*3.0f,
        .projection = CAMERA_PERSPECTIVE,
    };
    BeginMode3D(camera);
    float target_aspect = (float)viewport_width/(float)viewport_height;
    float full_aspect = (float)saved_width/(float)saved_height;
    rlMatrixMode(RL_PROJECTION);
    rlScalef(full_aspect/target_aspect, 1.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);

    Vector3 positions[CONSTELLATION_NODE_COUNT];
    float strengths[CONSTELLATION_NODE_COUNT];
    uint32_t types[CONSTELLATION_NODE_COUNT];
    uint64_t ids[CONSTELLATION_NODE_COUNT];
    for (size_t i = 0; i < node_count; ++i) {
        float band = constellation_band(frame, i);
        types[i] = 0;
        ids[i] = 0;
        strengths[i] = constellation_event_strength(
            frame, i, node_count, event_duration, event_reach,
            &types[i], &ids[i]);
        positions[i] = constellation_base_position(
            constellation->seed, i, node_count, time, band);
        if (strengths[i] > 0.0f) {
            float phase = constellation_unit(ids[i], i + 31U)*6.2831853f;
            float displacement = strengths[i]*(0.4f + constellation->motion.energy*0.55f);
            positions[i].x += cosf(phase)*displacement;
            positions[i].y += sinf(phase*1.7f)*displacement;
            positions[i].z += sinf(phase)*displacement;
        }
        positions[i].x *= field_scale;
        positions[i].y *= field_scale;
        positions[i].z *= field_scale;
    }

    for (size_t i = 0; i < node_count && web > 0.001f; ++i) {
        size_t long_step = node_count > 36U ? 13U : 7U;
        size_t neighbors[2] = { (i + 1)%node_count,
                                (i + long_step)%node_count };
        for (size_t edge = 0; edge < 2; ++edge) {
            size_t other = neighbors[edge];
            float active = fmaxf(strengths[i], strengths[other]);
            Color line = ColorFromHSV(fmodf(base_hue + (float)i*1.7f, 360.0f),
                                      0.48f + active*0.35f,
                                      fminf(1.0f, (0.24f +
                                      constellation->motion.energy*0.18f +
                                      active*0.50f)*web));
            scene_draw_tube(positions[i], positions[other],
                            0.006f + active*0.012f, 5,
                            ColorAlpha(line, fminf(1.0f,
                                       (0.40f + active*0.55f)*web)));
        }
    }

    for (size_t i = 0; i < node_count; ++i) {
        float band = constellation_band(frame, i);
        float brightness = 0.47f + band*0.3f + strengths[i]*0.52f;
        Color color = constellation_event_color(types[i],
                         fmodf(base_hue + (float)i*2.1f, 360.0f), brightness);
        float radius = (0.045f + band*0.075f + strengths[i]*0.16f
                     + constellation->motion.onset_pulse*0.018f)*glow_scale;
        DrawSphere(positions[i], radius, color);
    }
    // Stars glow as camera-facing soft sprites rather than wireframe shells:
    // a wide faint halo, and thin cross-flare streaks on the strongest nodes.
    Texture2D glow_texture = { rlGetTextureIdDefault(), 1, 1, 1,
                               PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Rectangle glow_source = { 0.0f, 0.0f, 1.0f, 1.0f };
    SetShaderValue(renderer->circle_shader, renderer->circle_radius_location,
                   (float[1]){ 0.06f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->circle_shader, renderer->circle_power_location,
                   (float[1]){ 2.6f }, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(renderer->circle_shader);
    for (size_t i = 0; i < node_count; ++i) {
        float band = constellation_band(frame, i);
        float flare = band*0.35f + strengths[i]*0.85f +
                      constellation->motion.onset_pulse*0.12f;
        if (flare < 0.08f || glow_scale <= 0.001f) continue;
        Color glow = constellation_event_color(
            types[i], fmodf(base_hue + (float)i*2.1f, 360.0f), 0.9f);
        float halo = (0.30f + band*0.34f + strengths[i]*0.75f)*glow_scale;
        DrawBillboardRec(camera, glow_texture, glow_source, positions[i],
                         (Vector2){halo, halo},
                         ColorAlpha(glow, fminf(0.60f, 0.22f + flare*0.40f)));
        if (flare > 0.45f) {
            Vector2 streak = {halo*(2.4f + flare), halo*0.30f};
            Color streak_color = ColorAlpha(glow, fminf(0.40f, flare*0.30f));
            DrawBillboardPro(camera, glow_texture, glow_source, positions[i],
                             (Vector3){0.0f, 1.0f, 0.0f}, streak,
                             (Vector2){streak.x*0.5f, streak.y*0.5f},
                             0.0f, streak_color);
            DrawBillboardPro(camera, glow_texture, glow_source, positions[i],
                             (Vector3){0.0f, 1.0f, 0.0f}, streak,
                             (Vector2){streak.x*0.5f, streak.y*0.5f},
                             90.0f, streak_color);
        }
    }
    EndShaderMode();
    EndBlendMode();
    EndMode3D();

    rlDrawRenderBatchActive();
    rlSetFramebufferWidth(saved_width);
    rlSetFramebufferHeight(saved_height);
    rlViewport(0, 0, saved_width, saved_height);
    DrawRectangleRec(boundary, ColorAlpha(background, 0.035f));
}

const Scene_Descriptor scene_constellation_descriptor = {
    .id = SCENE_CONSTELLATION,
    .name = "Constellation",
    .state_version = 2,
    .state_size = sizeof(Constellation_State),
    .init = constellation_init,
    .update = constellation_update,
    .draw = constellation_draw,
};
