#include "scene.h"

#include <math.h>

#include <rlgl.h>

enum {
    ORBITAL_RING_COUNT = 12,
    ORBITAL_NODES_PER_RING = 16,
};

typedef struct {
    uint64_t seed;
    float onset_pulse;
    float flux_swell;
} Orbital_Lattice_State;

static float orbital_clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static uint32_t orbital_hash(uint64_t seed, uint32_t ring, uint32_t node)
{
    uint64_t value = seed;
    value ^= (uint64_t)(ring + 1U)*UINT64_C(0x9e3779b97f4a7c15);
    value ^= (uint64_t)(node + 1U)*UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (uint32_t)value;
}

static float orbital_hash_unit(uint64_t seed, uint32_t ring, uint32_t node)
{
    return (float)(orbital_hash(seed, ring, node) & UINT32_C(0xffff))/65535.0f;
}

static float orbital_band(const Scene_Frame *frame, size_t index)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;
    return orbital_clamp01(frame->audio.bands[index%frame->audio.bands_count]);
}

static float orbital_bass(const Scene_Frame *frame)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0) return 0.0f;

    size_t count = frame->audio.bands_count/6;
    if (count < 1) count = 1;
    if (count > 8) count = 8;

    float bass = 0.0f;
    for (size_t i = 0; i < count; ++i) bass += orbital_clamp01(frame->audio.bands[i]);
    return bass/(float)count;
}

static void orbital_lattice_init(void *state, uint64_t seed)
{
    Orbital_Lattice_State *lattice = state;
    lattice->seed = seed;
    lattice->onset_pulse = 0.0f;
    lattice->flux_swell = 0.0f;
}

static void orbital_lattice_update(void *state, const Scene_Frame *frame)
{
    Orbital_Lattice_State *lattice = state;
    float delta = frame->delta_seconds;
    if (delta < 0.0f) delta = 0.0f;
    if (delta > 0.1f) delta = 0.1f;

    lattice->onset_pulse *= expf(-7.5f*delta);
    if (frame->audio.onset) lattice->onset_pulse = 1.0f;

    float flux = orbital_clamp01(frame->audio.spectral_flux*5.0f);
    float blend = 1.0f - expf(-4.0f*delta);
    lattice->flux_swell += (flux - lattice->flux_swell)*blend;
}

static void orbital_lattice_draw(const void *state, const Scene_Frame *frame,
                                 const Scene_Renderer *renderer, Rectangle boundary)
{
    (void)renderer;
    const Orbital_Lattice_State *lattice = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float time = (float)frame->time_seconds;
    float bass = orbital_bass(frame);
    float energy = orbital_clamp01(frame->audio.rms*1.8f);
    float flux = orbital_clamp01(frame->audio.spectral_flux*5.0f);
    float pulse = orbital_clamp01(lattice->onset_pulse);
    float seed_phase = orbital_hash_unit(lattice->seed, 0, 0)*2.0f*PI;
    float hue_base = fmodf(205.0f + orbital_hash_unit(lattice->seed, 1, 0)*110.0f
                           + time*(3.0f + flux*9.0f), 360.0f);

    Color background = ColorFromHSV(hue_base, 0.62f, 0.075f + energy*0.035f);
    DrawRectangleRec(boundary, background);

    int saved_framebuffer_width = rlGetFramebufferWidth();
    int saved_framebuffer_height = rlGetFramebufferHeight();
    if (saved_framebuffer_width < 1 || saved_framebuffer_height < 1) return;

    // UI boundaries use top-left logical window coordinates, while OpenGL
    // viewports use bottom-left framebuffer pixels. Render textures already use
    // framebuffer pixels, so only the default framebuffer needs DPI scaling.
    float coordinate_width = (float)saved_framebuffer_width;
    float coordinate_height = (float)saved_framebuffer_height;
    if (rlGetActiveFramebuffer() == 0) {
        int screen_width = GetScreenWidth();
        int screen_height = GetScreenHeight();
        if (screen_width < 1 || screen_height < 1) return;
        coordinate_width = (float)screen_width;
        coordinate_height = (float)screen_height;
    }

    float scale_x = (float)saved_framebuffer_width/coordinate_width;
    float scale_y = (float)saved_framebuffer_height/coordinate_height;
    int viewport_x = (int)roundf(boundary.x*scale_x);
    int viewport_width = (int)roundf(boundary.width*scale_x);
    int viewport_height = (int)roundf(boundary.height*scale_y);
    int viewport_top = (int)roundf((boundary.y + boundary.height)*scale_y);
    int viewport_y = saved_framebuffer_height - viewport_top;
    if (viewport_width < 1 || viewport_height < 1) return;

    // Flush 2D work before changing viewport state. rlgl's framebuffer size is
    // also changed because its batch and stereo paths treat it as authoritative.
    rlDrawRenderBatchActive();
    rlViewport(viewport_x, viewport_y, viewport_width, viewport_height);
    rlSetFramebufferWidth(viewport_width);
    rlSetFramebufferHeight(viewport_height);

    float camera_orbit = time*(0.09f + lattice->flux_swell*0.12f) + seed_phase;
    Camera3D camera = {
        .position = {
            cosf(camera_orbit)*(0.65f + energy*0.4f),
            sinf(camera_orbit*0.73f)*(0.5f + bass*0.35f),
            10.5f - pulse*0.65f,
        },
        .target = { 0.0f, 0.0f, -8.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 58.0f + flux*7.0f + pulse*4.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    BeginMode3D(camera);

    // raylib 5.5's BeginMode3D() reads its private full-target aspect rather
    // than rlgl's framebuffer dimensions. Correct only the projection X axis
    // so perspective is centered and proportioned for this viewport.
    float target_aspect = (float)viewport_width/(float)viewport_height;
    float full_aspect = (float)saved_framebuffer_width/(float)saved_framebuffer_height;
    rlMatrixMode(RL_PROJECTION);
    rlScalef(full_aspect/target_aspect, 1.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);

    for (int ring = ORBITAL_RING_COUNT - 1; ring >= 0; --ring) {
        float depth_t = (float)ring/(float)(ORBITAL_RING_COUNT - 1);
        float z = 3.0f - (float)ring*2.25f;
        float travel = fmodf(time*(1.2f + energy*1.8f), 2.25f);
        z += travel;

        float twist = time*(0.22f + flux*0.75f)
                    + (float)ring*(0.19f + lattice->flux_swell*0.28f)
                    + seed_phase;
        float ring_wave = sinf(time*0.8f - (float)ring*0.65f + seed_phase);
        float radius = 3.15f + bass*0.9f + ring_wave*(0.12f + pulse*0.35f);
        float center_x = sinf(time*0.31f + (float)ring*0.37f + seed_phase)*0.38f;
        float center_y = cosf(time*0.27f - (float)ring*0.29f + seed_phase)*0.28f;

        Vector3 first = { 0 };
        Vector3 previous = { 0 };
        for (int node = 0; node < ORBITAL_NODES_PER_RING; ++node) {
            float node_t = (float)node/(float)ORBITAL_NODES_PER_RING;
            float angle = node_t*2.0f*PI + twist;
            size_t band_index = ((size_t)node*frame->audio.bands_count
                               / ORBITAL_NODES_PER_RING + (size_t)ring);
            float amplitude = orbital_band(frame, band_index);
            float scatter = orbital_hash_unit(lattice->seed, (uint32_t)ring,
                                              (uint32_t)node) - 0.5f;
            float radial = radius + amplitude*(0.35f + energy*0.8f) + scatter*0.15f;
            Vector3 position = {
                center_x + cosf(angle)*radial,
                center_y + sinf(angle)*radial,
                z + sinf(angle*3.0f + seed_phase)*0.11f*(0.3f + flux),
            };

            float fog = 1.0f - depth_t*0.74f;
            float hue = fmodf(hue_base + node_t*105.0f + (float)ring*5.0f, 360.0f);
            Color color = ColorFromHSV(hue, 0.64f + amplitude*0.28f,
                                       orbital_clamp01(fog*(0.56f + amplitude*0.44f)));
            float size = 0.11f + amplitude*0.42f + energy*0.12f + pulse*0.08f;
            Vector3 cube_size = {
                size*(0.8f + bass*0.55f),
                size*(1.0f + amplitude*0.9f),
                size*(1.7f + flux*1.4f),
            };
            DrawCubeV(position, cube_size, color);
            if (ring < 5 || amplitude > 0.48f) {
                DrawCubeWiresV(position, cube_size, ColorAlpha(RAYWHITE, fog*0.42f));
            }

            if (node == 0) first = position;
            if (node > 0) {
                Color edge = ColorAlpha(color, fog*(0.12f + energy*0.18f));
                DrawLine3D(previous, position, edge);
            }
            previous = position;
        }
        DrawLine3D(previous, first, ColorAlpha(RAYWHITE, (1.0f - depth_t)*0.16f));
    }
    EndMode3D();

    // EndMode3D flushes scene geometry. Flush once more before restoring the
    // viewport contract expected by subsequent 2D UI and texture rendering.
    rlDrawRenderBatchActive();
    rlSetFramebufferWidth(saved_framebuffer_width);
    rlSetFramebufferHeight(saved_framebuffer_height);
    rlViewport(0, 0, saved_framebuffer_width, saved_framebuffer_height);

    Color veil = ColorAlpha(background, 0.08f + (1.0f - energy)*0.07f);
    DrawRectangleRec(boundary, veil);
}

const Scene_Descriptor scene_orbital_lattice_descriptor = {
    .id = SCENE_ORBITAL_LATTICE,
    .name = "Orbital Lattice",
    .state_version = 1,
    .state_size = sizeof(Orbital_Lattice_State),
    .init = orbital_lattice_init,
    .update = orbital_lattice_update,
    .draw = orbital_lattice_draw,
};
