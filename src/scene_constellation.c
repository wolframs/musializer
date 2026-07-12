#include "scene.h"
#include "scene_event_merge.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

#include "scene_draw.h"

enum { CONSTELLATION_NODE_COUNT = 72 };

typedef struct Constellation_State {
    uint64_t seed;
    float energy;
    float flux;
    float onset_pulse;
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
}

static void constellation_update(void *state, const Scene_Frame *frame)
{
    Constellation_State *constellation = state;
    float delta = frame->delta_seconds;
    if (!isfinite(delta) || delta < 0.0f) delta = 0.0f;
    if (delta > 0.1f) delta = 0.1f;
    float blend = 1.0f - expf(-5.0f*delta);
    float energy = constellation_clamp01(frame->audio.rms*1.8f);
    float flux = constellation_clamp01(frame->audio.spectral_flux*5.0f);
    constellation->energy += (energy - constellation->energy)*blend;
    constellation->flux += (flux - constellation->flux)*blend;
    constellation->onset_pulse *= expf(-7.0f*delta);
    if (frame->audio.onset) constellation->onset_pulse = 1.0f;
}

static float constellation_band(const Scene_Frame *frame, size_t index)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;
    return constellation_clamp01(frame->audio.bands[index%frame->audio.bands_count]);
}

static Vector3 constellation_base_position(uint64_t seed, size_t index, float time,
                                           float amplitude)
{
    float y = 1.0f - 2.0f*((float)index + 0.5f)/(float)CONSTELLATION_NODE_COUNT;
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
                                           uint32_t *dominant_type, uint64_t *dominant_id)
{
    if (frame->events.events == NULL || frame->events.count == 0) return 0.0f;
    size_t count = frame->events.count;
    if (count > SCENE_EVENT_MERGE_CAPACITY) count = SCENE_EVENT_MERGE_CAPACITY;
    float result = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const Event_Record *event = &frame->events.events[i];
        if (!event_record_is_valid(event)) continue;
        double age = frame->time_seconds - event->timestamp_seconds;
        if (age < 0.0 || age > 2.4) continue;
        size_t event_node = (size_t)(constellation_mix(event->id ^
                                   ((uint64_t)event->type << 48)) % CONSTELLATION_NODE_COUNT);
        size_t distance = node > event_node ? node - event_node : event_node - node;
        if (distance > 2 && distance < CONSTELLATION_NODE_COUNT - 2) continue;
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
    (void)renderer;
    const Constellation_State *constellation = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float time = (float)frame->time_seconds;
    float semantic_weight = frame->semantic.available ? frame->semantic.confidence : 0.0f;
    float base_hue = fmodf(201.0f + constellation_unit(constellation->seed, 9)*95.0f
                         + time*1.8f + frame->semantic.valence*70.0f*semantic_weight,
                           360.0f);
    Color background = ColorFromHSV(base_hue, 0.72f,
                                    0.035f + constellation->energy*0.035f);
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

    float orbit = time*(0.035f + constellation->flux*0.055f)
                + constellation_unit(constellation->seed, 10)*6.2831853f;
    Camera3D camera = {
        .position = { cosf(orbit)*8.9f, 1.2f + sinf(orbit*0.61f)*0.8f,
                      sinf(orbit)*8.9f },
        .target = { 0.0f, 0.0f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 56.0f + constellation->onset_pulse*3.0f,
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
    for (size_t i = 0; i < CONSTELLATION_NODE_COUNT; ++i) {
        float band = constellation_band(frame, i);
        types[i] = 0;
        ids[i] = 0;
        strengths[i] = constellation_event_strength(frame, i, &types[i], &ids[i]);
        positions[i] = constellation_base_position(constellation->seed, i, time, band);
        if (strengths[i] > 0.0f) {
            float phase = constellation_unit(ids[i], i + 31U)*6.2831853f;
            float displacement = strengths[i]*(0.4f + constellation->energy*0.55f);
            positions[i].x += cosf(phase)*displacement;
            positions[i].y += sinf(phase*1.7f)*displacement;
            positions[i].z += sinf(phase)*displacement;
        }
    }

    for (size_t i = 0; i < CONSTELLATION_NODE_COUNT; ++i) {
        size_t neighbors[2] = { (i + 1)%CONSTELLATION_NODE_COUNT,
                                (i + 13)%CONSTELLATION_NODE_COUNT };
        for (size_t edge = 0; edge < 2; ++edge) {
            size_t other = neighbors[edge];
            float active = fmaxf(strengths[i], strengths[other]);
            Color line = ColorFromHSV(fmodf(base_hue + (float)i*1.7f, 360.0f),
                                      0.48f + active*0.35f,
                                      0.16f + constellation->energy*0.13f + active*0.53f);
            scene_draw_tube(positions[i], positions[other],
                            0.006f + active*0.012f, 5,
                            ColorAlpha(line, 0.32f + active*0.58f));
        }
    }

    for (size_t i = 0; i < CONSTELLATION_NODE_COUNT; ++i) {
        float band = constellation_band(frame, i);
        float brightness = 0.47f + band*0.3f + strengths[i]*0.52f;
        Color color = constellation_event_color(types[i],
                         fmodf(base_hue + (float)i*2.1f, 360.0f), brightness);
        float radius = 0.045f + band*0.075f + strengths[i]*0.16f
                     + constellation->onset_pulse*0.018f;
        DrawSphere(positions[i], radius, color);
        if (strengths[i] > 0.12f) {
            DrawSphereWires(positions[i], radius*(1.5f + strengths[i]), 5, 8,
                            ColorAlpha(RAYWHITE, strengths[i]*0.6f));
        }
    }
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
    .state_version = 1,
    .state_size = sizeof(Constellation_State),
    .init = constellation_init,
    .update = constellation_update,
    .draw = constellation_draw,
};
