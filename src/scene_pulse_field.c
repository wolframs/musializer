#include "scene.h"

#include <math.h>

typedef struct {
    float rotation;
} Pulse_Field_State;

static void pulse_field_init(void *state, uint64_t seed)
{
    Pulse_Field_State *pulse = state;
    pulse->rotation = (float)(seed%360);
}

static void pulse_field_update(void *state, const Scene_Frame *frame)
{
    (void)state;
    (void)frame;
}

static void pulse_field_draw(const void *state, const Scene_Frame *frame, const Scene_Renderer *renderer, Rectangle boundary)
{
    const Pulse_Field_State *pulse = state;
    float scale = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_SCALE);
    float ring_setting = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_RINGS);
    float motion = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_MOTION);
    float arc = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_ARC);
    float weight = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_WEIGHT);
    float petal_setting = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_PETALS);
    float hue_shift = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_HUE);
    float bloom = scene_settings_get(
        renderer->settings, SCENE_PULSE_FIELD, PULSE_SETTING_GLOW);

    Vector2 center = {
        boundary.x + boundary.width*0.5f,
        boundary.y + boundary.height*0.5f,
    };
    float extent = fminf(boundary.width, boundary.height)*0.45f*scale;
    size_t count = frame->audio.bands_count;
    if (count == 0 || frame->audio.bands == NULL) return;

    size_t requested_rings = (size_t)floorf(ring_setting + 0.5f);
    size_t rings = count < requested_rings ? count : requested_rings;
    if (rings == 0) return;
    float semantic_hue = frame->semantic.available ?
                         frame->semantic.valence*60.0f*frame->semantic.confidence : 0.0f;
    float interpretation = frame->semantic.available ?
                           frame->semantic.tension*frame->semantic.confidence : 0.0f;
    size_t low_count = count/5U;
    if (low_count < 1U) low_count = 1U;
    float bass = 0.0f;
    float treble = 0.0f;
    for (size_t i = 0; i < low_count; ++i) bass += frame->audio.bands[i];
    for (size_t i = count - low_count; i < count; ++i) treble += frame->audio.bands[i];
    bass /= (float)low_count;
    treble /= (float)low_count;
    // Zero keeps the audio-driven fold (bass/treble balance chooses the rose);
    // any explicit petal count pins the silhouette for a consistent look.
    int fold = petal_setting >= 0.5f ? (int)lroundf(petal_setting) :
               3 + (int)lroundf(fminf(1.0f, treble/(bass + treble + 0.001f))*6.0f);
    float rotation = pulse->rotation + (float)frame->time_seconds*motion*12.0f +
                     frame->audio.spectral_flux*motion*45.0f +
                     interpretation*motion*14.0f;
    BeginBlendMode(BLEND_ADDITIVE);
    // A bass-breathing bloom anchors the rose's heart so the center never
    // reads as an empty hole between petal passes.
    if (bloom > 0.001f) {
        float bloom_radius = extent*(0.26f + bass*0.44f)*bloom;
        Color bloom_color = ColorFromHSV(
            fmodf(rotation + semantic_hue + hue_shift + 720.0f, 360.0f),
            0.58f, 0.85f);
        DrawCircleGradient((int)center.x, (int)center.y, bloom_radius,
                           ColorAlpha(bloom_color,
                                      0.24f + bass*0.36f +
                                      frame->audio.spectral_flux*0.22f),
                           BLANK);
    }
    for (size_t i = rings; i > 0; --i) {
        size_t band_index = (i - 1)*count/rings;
        float amplitude = frame->audio.bands[band_index];
        float phase = (float)(frame->time_seconds*motion*(0.25 + 0.015*i));
        float wobble = sinf(phase*2.0f*PI + (float)i)*extent*0.025f*amplitude;
        float base_radius = extent*((float)i/rings) + wobble;
        float start_angle = rotation + (float)i*7.5f;
        float sweep = (220.0f + amplitude*140.0f)*arc;
        if (sweep < 20.0f) sweep = 20.0f;
        if (sweep > 356.0f) sweep = 356.0f;
        float thickness = (1.0f + amplitude*8.0f)*renderer->pixel_scale*weight;
        Color color = ColorFromHSV(fmodf((float)i/rings*280.0f + rotation +
                                        semantic_hue + hue_shift + 720.0f, 360.0f),
                                   0.72f, 0.95f);
        int segments = 112;
        float rose_depth = 0.045f + amplitude*0.17f + interpretation*0.06f;
        Vector2 previous = {0};
        for (int segment = 0; segment <= segments; ++segment) {
            float amount = (float)segment/(float)segments;
            float degrees = start_angle + sweep*amount;
            float theta = degrees*DEG2RAD;
            float petals = cosf((float)fold*theta + (float)i*0.19f);
            float spiral = sinf(theta*0.5f + frame->audio.beat_phase*2.0f*PI);
            float radius = base_radius*(1.0f + petals*rose_depth) +
                           spiral*extent*0.018f*interpretation;
            Vector2 point = {
                center.x + cosf(theta)*radius,
                center.y + sinf(theta)*radius,
            };
            if (segment > 0) {
                DrawLineEx(previous, point, thickness,
                           ColorAlpha(color, 0.18f + amplitude*0.62f));
            }
            previous = point;
        }
    }
    EndBlendMode();
}

const Scene_Descriptor scene_pulse_field_descriptor = {
    .id = SCENE_PULSE_FIELD,
    .name = "Pulse Field",
    .state_version = 1,
    .state_size = sizeof(Pulse_Field_State),
    .init = pulse_field_init,
    .update = pulse_field_update,
    .draw = pulse_field_draw,
};
