#include "scene.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

// The scene draws the phase portrait of the Lyness recurrence
//
//     y[k+1] = (1 + y[k])/y[k-1]
//
// whose phase-plane map (x, y) -> (y, (1 + y)/x) returns every positive
// orbit to its start after exactly five steps (Zamolodchikov periodicity of
// the A2 Y-system). Each orbit is five stations on one level curve of the
// conserved quantity K = (x+1)(y+1)(x+y+1)/(x*y); the chords between
// consecutive stations inscribe a pentagram in that oval. Music drives the
// hop around the five-cycle, so every traversal closes exactly.
enum {
    PENTAGRAM_CURVE_CAPACITY = 12,
    PENTAGRAM_CURVE_SAMPLES = 96,
    PENTAGRAM_ORBIT_CAPACITY = 24,
    PENTAGRAM_ORBIT_PERIOD = 5,
};

// ln(golden ratio): the unique fixed point of the Lyness map is x = y = phi,
// which in centered log coordinates places the whole nest around the origin.
#define PENTAGRAM_LOG_PHI 0.48121182506f
// How far above min(ln K) the outermost traced level curve sits.
#define PENTAGRAM_LEVEL_SPREAD 1.75f

typedef struct Pentagram_State {
    uint64_t seed;
    // Level curves of K in centered log coordinates, innermost first.
    Vector2 curves[PENTAGRAM_CURVE_CAPACITY][PENTAGRAM_CURVE_SAMPLES];
    // Five phase-plane stations per orbit, in visiting order.
    Vector2 stations[PENTAGRAM_ORBIT_CAPACITY][PENTAGRAM_ORBIT_PERIOD];
    float orbit_offset[PENTAGRAM_ORBIT_CAPACITY];
    float orbit_rate[PENTAGRAM_ORBIT_CAPACITY];
    float orbit_depth[PENTAGRAM_ORBIT_CAPACITY];
    float extent;
} Pentagram_State;

static float pentagram_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint64_t pentagram_mix(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static float pentagram_unit(uint64_t seed, uint64_t salt)
{
    return (float)(pentagram_mix(seed ^ (salt + UINT64_C(0x9e3779b97f4a7c15))) &
                   UINT64_C(0xffff))/65535.0f;
}

static float pentagram_log_invariant(float u, float v)
{
    // ln K with x = e^u, y = e^v. Every term is softplus or log-sum-exp
    // minus a linear part, so the function is convex with a single minimum
    // at u = v = ln(phi); a ray from the minimum crosses each level set
    // exactly once, which pentagram_level_radius depends on.
    float x = expf(u);
    float y = expf(v);
    return logf(1.0f + x) + logf(1.0f + y) + logf(1.0f + x + y) - u - v;
}

static float pentagram_level_radius(float cos_a, float sin_a, float target)
{
    float low = 0.0f;
    float high = 0.25f;
    while (high < 8.0f &&
           pentagram_log_invariant(PENTAGRAM_LOG_PHI + cos_a*high,
                                   PENTAGRAM_LOG_PHI + sin_a*high) < target) {
        low = high;
        high *= 2.0f;
    }
    for (int i = 0; i < 34; ++i) {
        float mid = 0.5f*(low + high);
        if (pentagram_log_invariant(PENTAGRAM_LOG_PHI + cos_a*mid,
                                    PENTAGRAM_LOG_PHI + sin_a*mid) < target) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return 0.5f*(low + high);
}

static float pentagram_level_target(float depth, float floor_level)
{
    return floor_level + PENTAGRAM_LEVEL_SPREAD*powf(pentagram_clamp01(depth), 1.35f);
}

static void pentagram_init(void *state, uint64_t seed)
{
    Pentagram_State *pentagram = state;
    memset(pentagram, 0, sizeof(*pentagram));
    pentagram->seed = seed;

    float floor_level = pentagram_log_invariant(PENTAGRAM_LOG_PHI, PENTAGRAM_LOG_PHI);
    float extent = 0.0f;
    for (size_t curve = 0; curve < PENTAGRAM_CURVE_CAPACITY; ++curve) {
        float depth = ((float)curve + 0.6f)/(float)PENTAGRAM_CURVE_CAPACITY;
        float target = pentagram_level_target(depth, floor_level);
        for (size_t sample = 0; sample < PENTAGRAM_CURVE_SAMPLES; ++sample) {
            float angle = (float)sample*(2.0f*PI/(float)PENTAGRAM_CURVE_SAMPLES);
            float cos_a = cosf(angle);
            float sin_a = sinf(angle);
            float radius = pentagram_level_radius(cos_a, sin_a, target);
            Vector2 point = { cos_a*radius, sin_a*radius };
            if (!isfinite(point.x) || !isfinite(point.y)) point = (Vector2){0};
            pentagram->curves[curve][sample] = point;
            extent = fmaxf(extent, fmaxf(fabsf(point.x), fabsf(point.y)));
        }
    }
    pentagram->extent = fmaxf(extent, 0.5f);

    for (size_t orbit = 0; orbit < PENTAGRAM_ORBIT_CAPACITY; ++orbit) {
        float depth = pentagram_unit(seed, orbit*4U + 1U);
        float target = pentagram_level_target(0.10f + 0.88f*depth, floor_level);
        float angle = pentagram_unit(seed, orbit*4U + 2U)*2.0f*PI;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);
        float radius = pentagram_level_radius(cos_a, sin_a, target);
        // Seed one phase point on the level curve, then let the recurrence
        // itself place the remaining four stations; after five steps the
        // orbit is provably back at the start.
        float x = expf(PENTAGRAM_LOG_PHI + cos_a*radius);
        float y = expf(PENTAGRAM_LOG_PHI + sin_a*radius);
        for (size_t step = 0; step < PENTAGRAM_ORBIT_PERIOD; ++step) {
            Vector2 station = {
                logf(x) - PENTAGRAM_LOG_PHI,
                logf(y) - PENTAGRAM_LOG_PHI,
            };
            if (!isfinite(station.x) || !isfinite(station.y)) station = (Vector2){0};
            pentagram->stations[orbit][step] = station;
            float next = (1.0f + y)/x;
            x = y;
            y = next;
        }
        pentagram->orbit_offset[orbit] = pentagram_unit(seed, orbit*4U + 3U)*
                                         (float)PENTAGRAM_ORBIT_PERIOD;
        pentagram->orbit_rate[orbit] = 0.75f + pentagram_unit(seed, orbit*4U + 4U)*0.5f;
        pentagram->orbit_depth[orbit] = depth;
    }
}

static float pentagram_band(const float *values, size_t count, size_t index)
{
    if (values == NULL || count == 0) return 0.0f;
    return pentagram_clamp01(values[index%count]);
}

static Vector2 pentagram_project(Vector2 point, Vector2 center,
                                 float cos_r, float sin_r, float scale)
{
    return (Vector2){
        center.x + (point.x*cos_r - point.y*sin_r)*scale,
        center.y + (point.x*sin_r + point.y*cos_r)*scale,
    };
}

// Dwell on a station, then hop: a smoothstep of a smoothstep keeps the spark
// parked near integer positions and quick across the chord between them.
static float pentagram_hop_ease(float fraction)
{
    float smooth = fraction*fraction*(3.0f - 2.0f*fraction);
    return smooth*smooth*(3.0f - 2.0f*smooth);
}

static void pentagram_draw(const void *state, const Scene_Frame *frame,
                           const Scene_Renderer *renderer, Rectangle boundary)
{
    const Pentagram_State *pentagram = state;
    if (boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float motion = scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_MOTION);
    size_t nest_count = (size_t)lroundf(scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_NEST));
    size_t orbit_count = (size_t)lroundf(scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_ORBITS));
    float glow_scale = scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_GLOW);
    float chord_scale = scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_CHORDS);
    float hue_shift = scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_HUE);
    float pulse_scale = scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_PULSE);
    float field_scale = scene_settings_get(
        renderer->settings, SCENE_PENTAGRAM, PENTAGRAM_SETTING_ZOOM);
    if (nest_count > PENTAGRAM_CURVE_CAPACITY) nest_count = PENTAGRAM_CURVE_CAPACITY;
    if (orbit_count > PENTAGRAM_ORBIT_CAPACITY) orbit_count = PENTAGRAM_ORBIT_CAPACITY;

    float time = (float)frame->time_seconds;
    float flux = pentagram_clamp01(frame->audio.spectral_flux);
    float rms = pentagram_clamp01(frame->audio.rms);
    float beat_phase = pentagram_clamp01(frame->audio.beat_phase);
    float beat_pop = (1.0f - beat_phase)*(1.0f - beat_phase);
    float onset_flash = frame->audio.onset ? 1.0f : 0.0f;
    float drive = pentagram_clamp01(0.4f*rms + 0.6f*flux);

    float semantic_weight = frame->semantic.available ? frame->semantic.confidence : 0.0f;
    float base_hue = fmodf(38.0f + pentagram_unit(pentagram->seed, 7)*30.0f + hue_shift
                         + frame->semantic.valence*45.0f*semantic_weight + 1440.0f,
                           360.0f);

    Color background = ColorFromHSV(fmodf(base_hue + 226.0f, 360.0f), 0.62f,
                                    0.032f + drive*0.030f);
    DrawRectangleRec(boundary, background);
    Vector2 center = {
        boundary.x + boundary.width*0.5f,
        boundary.y + boundary.height*0.5f,
    };
    float span = fminf(boundary.width, boundary.height);
    DrawCircleGradient((int)center.x, (int)center.y, span*0.72f,
                       ColorAlpha(ColorFromHSV(fmodf(base_hue + 208.0f, 360.0f),
                                               0.58f, 0.11f + drive*0.07f), 0.6f),
                       BLANK);

    float rotation = time*0.042f*motion + pentagram_unit(pentagram->seed, 8)*2.0f*PI;
    float cos_r = cosf(rotation);
    float sin_r = sinf(rotation);
    float scale = 0.44f*span/pentagram->extent*field_scale;

    BeginBlendMode(BLEND_ADDITIVE);

    for (size_t curve = 0; curve < nest_count; ++curve) {
        float trail = pentagram_band(frame->audio.trails, frame->audio.bands_count,
                                     curve);
        float depth = ((float)curve + 0.6f)/(float)PENTAGRAM_CURVE_CAPACITY;
        Color line = ColorFromHSV(fmodf(base_hue + depth*58.0f, 360.0f),
                                  0.58f + trail*0.24f,
                                  fminf(1.0f, 0.30f + trail*0.55f + onset_flash*0.10f));
        float alpha = fminf(0.85f, 0.32f + trail*0.48f + flux*0.10f);
        float thickness = fmaxf(1.0f, span*0.0019f*(0.60f + trail*0.85f));
        Vector2 previous = pentagram_project(
            pentagram->curves[curve][PENTAGRAM_CURVE_SAMPLES - 1],
            center, cos_r, sin_r, scale);
        for (size_t sample = 0; sample < PENTAGRAM_CURVE_SAMPLES; ++sample) {
            Vector2 point = pentagram_project(pentagram->curves[curve][sample],
                                              center, cos_r, sin_r, scale);
            DrawLineEx(previous, point, thickness, ColorAlpha(line, alpha));
            previous = point;
        }
    }

    for (size_t orbit = 0; orbit < orbit_count; ++orbit) {
        Vector2 points[PENTAGRAM_ORBIT_PERIOD];
        for (size_t step = 0; step < PENTAGRAM_ORBIT_PERIOD; ++step) {
            points[step] = pentagram_project(pentagram->stations[orbit][step],
                                             center, cos_r, sin_r, scale);
        }
        float trail = pentagram_band(frame->audio.trails, frame->audio.bands_count,
                                     orbit);
        float hue = fmodf(base_hue + pentagram->orbit_depth[orbit]*58.0f, 360.0f);
        // Phase along the five-cycle is a pure function of seed and time, so
        // seeking and offline export land on identical frames.
        float hops = time*motion*(0.42f + pentagram->orbit_rate[orbit]*0.38f)
                   + pentagram->orbit_offset[orbit];
        size_t active = (size_t)((uint64_t)hops%PENTAGRAM_ORBIT_PERIOD);

        for (size_t edge = 0; edge < PENTAGRAM_ORBIT_PERIOD && chord_scale > 0.001f;
             ++edge) {
            float highlight = edge == active ? 0.30f + flux*0.30f : 0.0f;
            float alpha = fminf(0.8f, (0.09f + trail*0.14f + highlight)*chord_scale);
            float thickness = fmaxf(1.0f, span*0.0012f*(1.0f + highlight*2.2f));
            DrawLineEx(points[edge], points[(edge + 1)%PENTAGRAM_ORBIT_PERIOD],
                       thickness,
                       ColorAlpha(ColorFromHSV(hue, 0.46f, 0.9f), alpha));
        }
        for (size_t step = 0; step < PENTAGRAM_ORBIT_PERIOD; ++step) {
            DrawCircleV(points[step], fmaxf(1.0f, span*0.0021f*(1.0f + trail*0.8f)),
                        ColorAlpha(ColorFromHSV(hue, 0.38f, 0.95f),
                                   fminf(0.75f, 0.28f + trail*0.40f)));
        }
    }

    // Sparks and the golden fixed point render as soft shader glows on top.
    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1,
                          PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Rectangle source = { 0.0f, 0.0f, 1.0f, 1.0f };
    SetShaderValue(renderer->circle_shader, renderer->circle_radius_location,
                   (float[1]){ 0.05f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->circle_shader, renderer->circle_power_location,
                   (float[1]){ 2.3f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(renderer->circle_shader);
    for (size_t orbit = 0; orbit < orbit_count && glow_scale > 0.001f; ++orbit) {
        float band = pentagram_band(frame->audio.bands, frame->audio.bands_count,
                                    orbit);
        float hops = time*motion*(0.42f + pentagram->orbit_rate[orbit]*0.38f)
                   + pentagram->orbit_offset[orbit];
        size_t active = (size_t)((uint64_t)hops%PENTAGRAM_ORBIT_PERIOD);
        float eased = pentagram_hop_ease(hops - floorf(hops));
        Vector2 from = pentagram_project(pentagram->stations[orbit][active],
                                         center, cos_r, sin_r, scale);
        Vector2 to = pentagram_project(
            pentagram->stations[orbit][(active + 1)%PENTAGRAM_ORBIT_PERIOD],
            center, cos_r, sin_r, scale);
        Vector2 head = {
            from.x + (to.x - from.x)*eased,
            from.y + (to.y - from.y)*eased,
        };
        // Audio pushes the spark radially as an additive offset from the
        // current frame only; nothing is integrated, so it stays seek-safe.
        float push_x = head.x - center.x;
        float push_y = head.y - center.y;
        float push_length = sqrtf(push_x*push_x + push_y*push_y);
        if (push_length > 1.0f) {
            float push = pulse_scale*(flux*0.55f + beat_pop*0.30f)*span*0.016f;
            head.x += push_x/push_length*push;
            head.y += push_y/push_length*push;
        }
        float hue = fmodf(base_hue + pentagram->orbit_depth[orbit]*58.0f, 360.0f);
        float size = span*(0.048f + band*0.056f + beat_pop*pulse_scale*0.024f
                         + onset_flash*0.012f)*glow_scale;
        Rectangle dest = { head.x - size*0.5f, head.y - size*0.5f, size, size };
        DrawTexturePro(texture, source, dest, (Vector2){0}, 0,
                       ColorAlpha(ColorFromHSV(hue, 0.42f, 1.0f),
                                  fminf(0.9f, 0.46f + band*0.32f + flux*0.18f)));
    }
    // The map's fixed point sits at x = y = phi: a quiet golden ember marks
    // the golden ratio at the heart of the nest.
    float ember = span*(0.082f + rms*0.075f + beat_pop*pulse_scale*0.030f)*
                  fmaxf(glow_scale, 0.25f);
    Rectangle ember_dest = { center.x - ember*0.5f, center.y - ember*0.5f,
                             ember, ember };
    DrawTexturePro(texture, source, ember_dest, (Vector2){0}, 0,
                   ColorAlpha(ColorFromHSV(fmodf(base_hue + 6.0f, 360.0f),
                                           0.55f, 1.0f),
                              fminf(0.8f, 0.30f + rms*0.40f)));
    EndShaderMode();
    EndBlendMode();
}

const Scene_Descriptor scene_pentagram_descriptor = {
    .id = SCENE_PENTAGRAM,
    .name = "Pentagram Orbits",
    .state_version = 1,
    .state_size = sizeof(Pentagram_State),
    .init = pentagram_init,
    .draw = pentagram_draw,
};
