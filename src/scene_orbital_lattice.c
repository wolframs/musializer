#include "scene.h"

#include <math.h>

#include <rlgl.h>

#include "scene_draw.h"
#include "scene_orbital_lattice_motion.h"

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

static float orbital_time_phase(double time_seconds, double radians_per_second)
{
    if (!isfinite(time_seconds) || !isfinite(radians_per_second)) return 0.0f;
    double phase = fmod(time_seconds*radians_per_second, 2.0*PI);
    if (phase < 0.0) phase += 2.0*PI;
    return (float)phase;
}

static void orbital_draw_swaying_link(Vector3 from, Vector3 to, float radius,
                                      float sway, float phase, Color color)
{
    enum { SEGMENTS = 7 };
    Vector3 previous = from;
    for (int segment = 1; segment <= SEGMENTS; ++segment) {
        float t = (float)segment/(float)SEGMENTS;
        float arch = 4.0f*t*(1.0f - t);
        Vector3 point = {
            from.x + (to.x - from.x)*t,
            from.y + (to.y - from.y)*t + arch*sway,
            from.z + (to.z - from.z)*t +
                arch*sway*0.45f*sinf(phase + t*PI),
        };
        scene_draw_tube(previous, point, radius, 6, color);
        previous = point;
    }
}

static void orbital_lattice_init(void *state, uint64_t seed)
{
    orbital_lattice_motion_init(state, seed);
}

static void orbital_lattice_update(void *state, const Scene_Frame *frame)
{
    if (state == NULL || frame == NULL) return;
    Orbital_Lattice_Motion_Input input = {
        .time_seconds = frame->time_seconds,
        .delta_seconds = frame->delta_seconds,
        .bands = frame->audio.bands,
        .bands_count = frame->audio.bands_count,
        .rms = frame->audio.rms,
        .spectral_flux = frame->audio.spectral_flux,
        .onset = frame->audio.onset,
        .semantic_available = frame->semantic.available,
        .semantic_valence = frame->semantic.valence,
        .semantic_tension = frame->semantic.tension,
        .semantic_confidence = frame->semantic.confidence,
        .motion_rate = scene_settings_get(
            frame->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_MOTION),
    };
    orbital_lattice_motion_update(state, &input);
}

static void orbital_lattice_draw(const void *state, const Scene_Frame *frame,
                                 const Scene_Renderer *renderer, Rectangle boundary)
{
    const Orbital_Lattice_Motion *lattice = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float bass = orbital_clamp01(lattice->bass);
    float mids = orbital_clamp01(lattice->mids);
    float treble = orbital_clamp01(lattice->treble);
    float energy = orbital_clamp01(lattice->energy);
    float flux = orbital_clamp01(lattice->flux);
    float pulse = orbital_clamp01(lattice->onset_pulse);
    float radius_scale = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_RADIUS);
    float depth_scale = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_DEPTH);
    float node_scale = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_NODES);
    float link_scale = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_LINKS);
    float tilt = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_TILT);
    float hue_shift = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_HUE);
    // Reactivity couples the current (damped) audio into motion as additive
    // offsets, and sway drives the audio-independent wander. Neither feeds the
    // accumulating phase, so seeking still lands on the same deterministic orbit.
    float reactivity = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_REACTIVITY);
    float sway = scene_settings_get(
        renderer->settings, SCENE_ORBITAL_LATTICE, ORBITAL_SETTING_SWAY);
    float seed_phase = orbital_hash_unit(lattice->seed, 0, 0)*2.0f*PI;
    float hue_base = fmodf((float)lattice->hue_degrees + hue_shift +
                           lattice->semantic_valence*55.0f + 720.0f, 360.0f);

    Color background = ColorFromHSV(hue_base,
                                    0.58f + lattice->semantic_tension*0.16f,
                                    0.055f + energy*0.035f);
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

    // Tilt swings the camera off the tunnel axis so the ring stack reads as
    // receding 3D geometry; zero restores the original head-on framing.
    float camera_orbit = (float)lattice->camera_phase;
    float camera_radius = 0.42f + mids*0.16f + tilt*(2.5f + mids*0.5f);
    float camera_lift = sinf(camera_orbit + seed_phase*0.37f)*
                        (0.24f + treble*0.09f) +
                        tilt*(1.15f + sinf(camera_orbit*0.7f)*0.45f) +
                        reactivity*flux*0.14f*sinf(camera_orbit*1.3f + seed_phase);
    Camera3D camera = {
        .position = {
            cosf(camera_orbit)*camera_radius,
            camera_lift,
            10.9f - bass*0.28f - pulse*0.12f,
        },
        .target = { 0.0f, 0.0f, -8.5f*depth_scale },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = 55.0f + flux*2.3f + pulse*0.8f,
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

    float breathe_phase = orbital_time_phase(frame->time_seconds, 0.55);
    float drift_x_phase = orbital_time_phase(frame->time_seconds, 0.17);
    float drift_y_phase = orbital_time_phase(frame->time_seconds, 0.13);
    for (int ring = ORBITAL_LATTICE_RING_COUNT - 1; ring >= 0; --ring) {
        Orbital_Lattice_Ring_Motion ring_motion;
        if (!orbital_lattice_motion_ring(lattice, (size_t)ring,
                                         &ring_motion) ||
            ring_motion.visibility < 0.01f) continue;
        float depth_t = ring_motion.depth_t;
        float z = 3.0f - ring_motion.distance*depth_scale;
        float ring_character = orbital_hash_unit(
            lattice->seed, (uint32_t)ring, UINT32_C(0xa7));

        // Beats and flux nudge each ring's rotation and breathing without ever
        // entering the accumulating twist_phase, so the swing is reactive yet
        // seek-stable. Reactivity scales the audio kick; sway the free wander.
        float twist_kick = reactivity*(flux*0.55f + pulse*0.32f)*
                           sinf(seed_phase + (float)ring*0.9f);
        float twist = (float)lattice->twist_phase + (float)ring*0.23f +
                      (ring_character - 0.5f)*0.22f + twist_kick;
        float ring_wave = sinf(breathe_phase - (float)ring*0.55f + seed_phase);
        float radius = (3.12f + bass*0.48f +
                        ring_wave*(0.14f + pulse*0.10f + reactivity*0.08f))*
                       radius_scale;
        float center_x = sinf(drift_x_phase + (float)ring*0.37f + seed_phase)*
                         0.34f*sway;
        float center_y = cosf(drift_y_phase - (float)ring*0.29f + seed_phase)*
                         0.24f*sway;

        Vector3 first = { 0 };
        Vector3 previous = { 0 };
        for (int node = 0; node < ORBITAL_LATTICE_NODES_PER_RING; ++node) {
            float node_t = (float)node/(float)ORBITAL_LATTICE_NODES_PER_RING;
            float angle = node_t*2.0f*PI + twist;
            size_t band_index = ((size_t)node + (size_t)ring*3U)%
                                ORBITAL_LATTICE_NODES_PER_RING;
            float amplitude = orbital_clamp01(lattice->node_bands[band_index]);
            float scatter = orbital_hash_unit(lattice->seed, (uint32_t)ring,
                                              (uint32_t)node) - 0.5f;
            // Onsets shove every node outward together; flux ripples them out of
            // phase around the ring. Both read as the lattice "breathing" to the
            // beat and vanish smoothly when reactivity is dialed down.
            float node_bounce = reactivity*(pulse*0.24f +
                                flux*0.18f*sinf(seed_phase + node_t*2.0f*PI));
            float radial = radius +
                           amplitude*(0.24f + energy*0.42f + reactivity*0.22f) +
                           scatter*0.10f + node_bounce;
            Vector3 position = {
                center_x + cosf(angle)*radial,
                center_y + sinf(angle)*radial,
                z + sinf(angle*2.0f + seed_phase)*0.07f*(0.4f + treble),
            };

            float fog = (1.0f - depth_t*0.78f)*ring_motion.visibility;
            float hue = fmodf(hue_base + node_t*105.0f + (float)ring*5.0f, 360.0f);
            // Directional key light from the upper left so cubes shade around
            // the ring instead of rendering as one flat tone.
            float shade = 0.74f + 0.26f*cosf(angle - 2.35f);
            Color color = ColorFromHSV(hue, 0.64f + amplitude*0.28f,
                                       orbital_clamp01(fog*shade*
                                                       (0.60f + amplitude*0.44f)));
            color = ColorAlpha(color, ring_motion.visibility);
            float size = (0.10f + amplitude*0.23f + energy*0.06f + pulse*0.04f)*
                         node_scale;
            Vector3 cube_size = {
                size*(0.88f + bass*0.30f),
                size*(1.0f + amplitude*0.55f),
                size*(1.45f + treble*0.65f),
            };
            DrawCubeV(position, cube_size, color);
            Vector3 facet_position = {
                position.x - size*0.10f,
                position.y + size*0.13f,
                position.z + size*0.08f,
            };
            Vector3 facet_size = {
                cube_size.x*0.48f,
                cube_size.y*0.42f,
                cube_size.z*0.36f,
            };
            DrawCubeV(facet_position, facet_size,
                      ColorAlpha(ColorBrightness(color, 0.34f),
                                 0.34f + amplitude*0.34f));
            if (ring_motion.distance < 11.0f || amplitude > 0.55f) {
                DrawCubeWiresV(position, cube_size,
                               ColorAlpha(RAYWHITE, fog*0.32f));
            }

            if (node == 0) first = position;
            if (node > 0 && link_scale > 0.001f) {
                Color edge = ColorAlpha(color, fog*(0.22f + energy*0.20f)*
                                               fminf(1.0f, link_scale));
                orbital_draw_swaying_link(
                    previous, position,
                    (0.0085f + energy*0.007f)*link_scale,
                    (0.025f + flux*0.11f)*sinf(angle + breathe_phase),
                    angle + seed_phase, edge);
            }
            previous = position;
        }
        if (link_scale > 0.001f) {
            orbital_draw_swaying_link(
                previous, first, (0.0085f + energy*0.007f)*link_scale,
                (0.025f + flux*0.11f)*sinf(twist + breathe_phase), twist,
                ColorAlpha(RAYWHITE,
                           fminf(1.0f, link_scale)*
                           (1.0f - depth_t)*ring_motion.visibility*0.24f));
        }
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
    .state_version = 2,
    .state_size = sizeof(Orbital_Lattice_Motion),
    .init = orbital_lattice_init,
    .update = orbital_lattice_update,
    .draw = orbital_lattice_draw,
};
