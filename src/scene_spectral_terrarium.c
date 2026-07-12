#include "scene.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

#include "scene_draw.h"

enum {
    TERRARIUM_PARTICLE_COUNT = 56,
    TERRARIUM_PLANT_COUNT = 24,
    TERRARIUM_CREATURE_COUNT = 10,
    TERRARIUM_MAX_STEPS = 8,
};

#define TERRARIUM_FIXED_STEP (1.0f/60.0f)

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float phase;
    float size;
} Terrarium_Particle;

typedef struct {
    Vector3 root;
    float height;
    float phase;
    float lean;
    uint8_t band;
} Terrarium_Plant;

typedef struct {
    float angle;
    float radius;
    float height;
    float speed;
    float phase;
    uint8_t band;
} Terrarium_Creature;

typedef struct {
    uint64_t seed;
    double last_frame_time;
    double simulation_time;
    float accumulator;
    float energy;
    float bass;
    float treble;
    float flux;
    float onset_pulse;
    Terrarium_Particle particles[TERRARIUM_PARTICLE_COUNT];
    Terrarium_Plant plants[TERRARIUM_PLANT_COUNT];
    Terrarium_Creature creatures[TERRARIUM_CREATURE_COUNT];
} Spectral_Terrarium_State;

static float terrarium_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint32_t terrarium_hash(uint64_t seed, uint32_t salt)
{
    uint64_t value = seed ^ ((uint64_t)salt + UINT64_C(0x9e3779b97f4a7c15));
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (uint32_t)value;
}

static float terrarium_hash_unit(uint64_t seed, uint32_t salt)
{
    return (float)(terrarium_hash(seed, salt) & UINT32_C(0xffff))/65535.0f;
}

static float terrarium_band(const Scene_Frame *frame, size_t index)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;
    return terrarium_clamp01(frame->audio.bands[index%frame->audio.bands_count]);
}

static float terrarium_band_range(const Scene_Frame *frame, size_t begin, size_t end)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;
    if (begin >= frame->audio.bands_count) begin = frame->audio.bands_count - 1;
    if (end > frame->audio.bands_count) end = frame->audio.bands_count;
    if (end <= begin) end = begin + 1;
    float total = 0.0f;
    for (size_t i = begin; i < end; ++i) total += terrarium_clamp01(frame->audio.bands[i]);
    return total/(float)(end - begin);
}

static void terrarium_seed_world(Spectral_Terrarium_State *terrarium, double song_time)
{
    for (size_t i = 0; i < TERRARIUM_PARTICLE_COUNT; ++i) {
        float angle = terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 1U)*2.0f*PI;
        float radius = sqrtf(terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 2U))*3.65f;
        Terrarium_Particle *particle = &terrarium->particles[i];
        particle->position = (Vector3) {
            cosf(angle)*radius,
            -1.55f + terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 3U)*4.4f,
            sinf(angle)*radius,
        };
        particle->velocity = (Vector3) {
            (terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 4U) - 0.5f)*0.24f,
            0.10f + terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 5U)*0.18f,
            (terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 6U) - 0.5f)*0.24f,
        };
        particle->phase = terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 7U)*2.0f*PI;
        particle->size = 0.025f + terrarium_hash_unit(terrarium->seed, (uint32_t)i*7U + 8U)*0.055f;
    }

    for (size_t i = 0; i < TERRARIUM_PLANT_COUNT; ++i) {
        float angle = ((float)i + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 300U)*0.65f)
                    *2.0f*PI/(float)TERRARIUM_PLANT_COUNT;
        float radius = 1.0f + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 400U)*2.45f;
        Terrarium_Plant *plant = &terrarium->plants[i];
        plant->root = (Vector3) { cosf(angle)*radius, -1.72f, sinf(angle)*radius };
        plant->height = 0.45f + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 500U)*1.35f;
        plant->phase = terrarium_hash_unit(terrarium->seed, (uint32_t)i + 600U)*2.0f*PI;
        plant->lean = 0.12f + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 700U)*0.28f;
        plant->band = (uint8_t)i;
    }

    for (size_t i = 0; i < TERRARIUM_CREATURE_COUNT; ++i) {
        Terrarium_Creature *creature = &terrarium->creatures[i];
        creature->angle = terrarium_hash_unit(terrarium->seed, (uint32_t)i + 800U)*2.0f*PI;
        creature->radius = 0.7f + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 900U)*2.55f;
        creature->height = -0.85f + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 1000U)*2.7f;
        creature->speed = (0.22f + terrarium_hash_unit(terrarium->seed, (uint32_t)i + 1100U)*0.42f)
                        *((i & 1U) ? -1.0f : 1.0f);
        creature->phase = terrarium_hash_unit(terrarium->seed, (uint32_t)i + 1200U)*2.0f*PI;
        creature->band = (uint8_t)(i*3U + 2U);
    }

    terrarium->simulation_time = floor(song_time*60.0)/60.0;
    terrarium->accumulator = 0.0f;
    terrarium->energy = 0.0f;
    terrarium->bass = 0.0f;
    terrarium->treble = 0.0f;
    terrarium->flux = 0.0f;
    terrarium->onset_pulse = 0.0f;
}

static void spectral_terrarium_init(void *state, uint64_t seed)
{
    Spectral_Terrarium_State *terrarium = state;
    memset(terrarium, 0, sizeof(*terrarium));
    terrarium->seed = seed;
    terrarium->last_frame_time = -1.0;
    terrarium_seed_world(terrarium, 0.0);
}

static void terrarium_simulate(Spectral_Terrarium_State *terrarium, float step)
{
    float time = (float)terrarium->simulation_time;
    for (size_t i = 0; i < TERRARIUM_PARTICLE_COUNT; ++i) {
        Terrarium_Particle *particle = &terrarium->particles[i];
        float curl = sinf(time*0.61f + particle->phase);
        particle->position.x += (particle->velocity.x + curl*0.08f*(0.3f + terrarium->flux))*step;
        particle->position.z += (particle->velocity.z - curl*0.07f*(0.3f + terrarium->flux))*step;
        particle->position.y += particle->velocity.y*step*(0.55f + terrarium->energy);
        float radius_squared = particle->position.x*particle->position.x
                             + particle->position.z*particle->position.z;
        if (particle->position.y > 3.05f || radius_squared > 14.6f) {
            float angle = particle->phase + time*0.13f;
            float radius = 0.35f + fmodf((float)i*0.6180339f, 1.0f)*3.0f;
            particle->position = (Vector3) { cosf(angle)*radius, -1.58f, sinf(angle)*radius };
        }
    }

    for (size_t i = 0; i < TERRARIUM_CREATURE_COUNT; ++i) {
        Terrarium_Creature *creature = &terrarium->creatures[i];
        float activity = 0.7f + terrarium->energy*0.8f + terrarium->flux*0.65f;
        creature->angle = fmodf(creature->angle + creature->speed*activity*step, 2.0f*PI);
    }
    terrarium->simulation_time += step;
}

static void spectral_terrarium_update(void *state, const Scene_Frame *frame)
{
    Spectral_Terrarium_State *terrarium = state;
    double elapsed = terrarium->last_frame_time < 0.0
                   ? 0.0 : frame->time_seconds - terrarium->last_frame_time;
    bool discontinuity = terrarium->last_frame_time >= 0.0
                      && (!isfinite(elapsed) || elapsed < -0.001 || elapsed > 0.25);
    if (discontinuity) terrarium_seed_world(terrarium, frame->time_seconds);

    float delta = frame->delta_seconds;
    if (!isfinite(delta) || delta < 0.0f) delta = 0.0f;
    if (delta > 0.133333f) delta = 0.133333f;

    size_t low_end = frame->audio.bands_count/6U;
    if (low_end < 1U) low_end = 1U;
    size_t high_begin = frame->audio.bands_count*2U/3U;
    float target_energy = terrarium_clamp01(frame->audio.rms*1.9f);
    float target_bass = terrarium_band_range(frame, 0, low_end);
    float target_treble = terrarium_band_range(frame, high_begin, frame->audio.bands_count);
    float target_flux = terrarium_clamp01(frame->audio.spectral_flux*5.0f);
    float blend = 1.0f - expf(-5.5f*delta);
    terrarium->energy += (target_energy - terrarium->energy)*blend;
    terrarium->bass += (target_bass - terrarium->bass)*blend;
    terrarium->treble += (target_treble - terrarium->treble)*blend;
    terrarium->flux += (target_flux - terrarium->flux)*blend;
    terrarium->onset_pulse *= expf(-6.8f*delta);
    if (frame->audio.onset) terrarium->onset_pulse = 1.0f;

    terrarium->accumulator += delta;
    int steps = 0;
    while (terrarium->accumulator >= TERRARIUM_FIXED_STEP && steps < TERRARIUM_MAX_STEPS) {
        terrarium_simulate(terrarium, TERRARIUM_FIXED_STEP);
        terrarium->accumulator -= TERRARIUM_FIXED_STEP;
        steps += 1;
    }
    if (steps == TERRARIUM_MAX_STEPS && terrarium->accumulator >= TERRARIUM_FIXED_STEP) {
        terrarium->accumulator = fmodf(terrarium->accumulator, TERRARIUM_FIXED_STEP);
    }
    terrarium->last_frame_time = frame->time_seconds;
}

static Vector3 terrarium_creature_position(const Terrarium_Creature *creature,
                                           const Spectral_Terrarium_State *terrarium)
{
    float y = creature->height + sinf((float)terrarium->simulation_time*1.3f + creature->phase)
                            *(0.12f + terrarium->treble*0.18f);
    return (Vector3) { cosf(creature->angle)*creature->radius, y,
                       sinf(creature->angle)*creature->radius };
}

static void terrarium_draw_world(const Spectral_Terrarium_State *terrarium,
                                 const Scene_Frame *frame, float hue)
{
    Color soil = ColorFromHSV(fmodf(hue + 115.0f, 360.0f), 0.55f, 0.16f + terrarium->bass*0.08f);
    DrawCylinder((Vector3) { 0.0f, -1.82f, 0.0f }, 4.15f, 3.9f, 0.22f, 48, soil);
    DrawCircle3D((Vector3) { 0.0f, -1.69f, 0.0f }, 4.0f, (Vector3) { 1, 0, 0 }, 90.0f,
                 ColorAlpha(ColorFromHSV(hue, 0.52f, 0.28f), 0.55f));

    for (size_t i = 0; i < TERRARIUM_PLANT_COUNT; ++i) {
        const Terrarium_Plant *plant = &terrarium->plants[i];
        float amplitude = terrarium_band(frame, plant->band);
        float height = plant->height*(0.72f + amplitude*0.75f + terrarium->bass*0.18f);
        float sway = sinf((float)terrarium->simulation_time*(0.75f + terrarium->flux)
                         + plant->phase)*plant->lean*(0.45f + amplitude);
        Vector3 middle = { plant->root.x + sway*0.42f, plant->root.y + height*0.54f,
                           plant->root.z + cosf(plant->phase)*sway*0.25f };
        Vector3 tip = { plant->root.x + sway, plant->root.y + height,
                        plant->root.z + sinf(plant->phase)*sway*0.55f };
        Color stem = ColorFromHSV(fmodf(hue + 72.0f + (float)i*2.7f, 360.0f),
                                  0.72f, 0.38f + amplitude*0.48f);
        float stem_radius = 0.012f + amplitude*0.012f;
        scene_draw_tube(plant->root, middle, stem_radius, 6,
                        ColorAlpha(stem, 0.78f));
        scene_draw_tube(middle, tip, stem_radius*0.72f, 6, stem);
        DrawSphere(tip, 0.055f + amplitude*0.14f + terrarium->onset_pulse*0.025f,
                   ColorFromHSV(fmodf(hue + 145.0f + (float)i*8.0f, 360.0f),
                                0.62f, 0.55f + amplitude*0.42f));
    }

    for (size_t i = 0; i < TERRARIUM_PARTICLE_COUNT; ++i) {
        const Terrarium_Particle *particle = &terrarium->particles[i];
        float shimmer = 0.5f + 0.5f*sinf((float)terrarium->simulation_time*2.1f + particle->phase);
        Color color = ColorFromHSV(fmodf(hue + 35.0f + (float)i*4.7f, 360.0f), 0.38f,
                                   0.48f + shimmer*0.45f);
        DrawSphere(particle->position, particle->size*(0.8f + terrarium->treble*1.3f),
                   ColorAlpha(color, 0.38f + shimmer*0.52f));
    }

    for (size_t i = 0; i < TERRARIUM_CREATURE_COUNT; ++i) {
        const Terrarium_Creature *creature = &terrarium->creatures[i];
        float amplitude = terrarium_band(frame, creature->band);
        Vector3 position = terrarium_creature_position(creature, terrarium);
        Vector3 tangent = { -sinf(creature->angle), 0.0f, cosf(creature->angle) };
        float length = 0.18f + amplitude*0.32f;
        Vector3 head = { position.x + tangent.x*length, position.y,
                         position.z + tangent.z*length };
        Color color = ColorFromHSV(fmodf(hue + 190.0f + (float)i*13.0f, 360.0f),
                                   0.58f, 0.58f + amplitude*0.38f);
        scene_draw_tube(position, head, 0.014f + amplitude*0.012f, 6,
                        ColorAlpha(color, 0.8f));
        DrawSphere(head, 0.07f + amplitude*0.09f, color);
        DrawSphere(position, 0.045f + terrarium->energy*0.035f, ColorAlpha(RAYWHITE, 0.72f));
    }

    // Sparse latitude rings imply a glass habitat without hiding its contents.
    Color glass = ColorAlpha(ColorFromHSV(fmodf(hue + 25.0f, 360.0f), 0.2f, 0.9f), 0.13f);
    for (int ring = 0; ring < 4; ++ring) {
        float y = -1.35f + (float)ring*1.15f;
        float radius = sqrtf(fmaxf(0.0f, 16.0f - (y + 1.65f)*(y + 1.65f)));
        DrawCircle3D((Vector3) { 0.0f, y, 0.0f }, radius, (Vector3) { 1, 0, 0 }, 90.0f, glass);
    }
}

static void spectral_terrarium_draw(const void *state, const Scene_Frame *frame,
                                    const Scene_Renderer *renderer, Rectangle boundary)
{
    (void)renderer;
    const Spectral_Terrarium_State *terrarium = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float semantic_weight = frame->semantic.available ? frame->semantic.confidence : 0.0f;
    float hue = fmodf(145.0f + terrarium_hash_unit(terrarium->seed, 1500U)*125.0f
                    + (float)frame->time_seconds*(1.2f + terrarium->flux*2.5f)
                    + frame->semantic.valence*75.0f*semantic_weight, 360.0f);
    Color background = ColorFromHSV(hue, 0.68f, 0.035f + terrarium->energy*0.035f);
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

    float orbit = (float)frame->time_seconds*0.075f
                + terrarium_hash_unit(terrarium->seed, 1600U)*2.0f*PI;
    Camera3D camera = {
        .position = { cosf(orbit)*7.6f, 3.3f + sinf(orbit*0.7f)*0.45f,
                      sinf(orbit)*7.6f },
        .target = { 0.0f, -0.15f, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 52.0f + terrarium->flux*5.0f + terrarium->onset_pulse*2.0f,
        .projection = CAMERA_PERSPECTIVE,
    };
    BeginMode3D(camera);
    float viewport_aspect = (float)viewport_width/(float)viewport_height;
    float full_aspect = (float)saved_width/(float)saved_height;
    rlMatrixMode(RL_PROJECTION);
    rlScalef(full_aspect/viewport_aspect, 1.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);
    terrarium_draw_world(terrarium, frame, hue);
    EndMode3D();

    rlDrawRenderBatchActive();
    rlSetFramebufferWidth(saved_width);
    rlSetFramebufferHeight(saved_height);
    rlViewport(0, 0, saved_width, saved_height);
    DrawRectangleRec(boundary, ColorAlpha(background, 0.035f));
}

const Scene_Descriptor scene_spectral_terrarium_descriptor = {
    .id = SCENE_SPECTRAL_TERRARIUM,
    .name = "Spectral Terrarium",
    .state_version = 1,
    .state_size = sizeof(Spectral_Terrarium_State),
    .init = spectral_terrarium_init,
    .update = spectral_terrarium_update,
    .draw = spectral_terrarium_draw,
};
