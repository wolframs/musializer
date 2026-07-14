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
    float rotation = pulse->rotation + (float)frame->time_seconds*motion*12.0f +
                     frame->audio.spectral_flux*motion*45.0f +
                     interpretation*motion*14.0f;
    for (size_t i = rings; i > 0; --i) {
        size_t band_index = (i - 1)*count/rings;
        float amplitude = frame->audio.bands[band_index];
        float phase = (float)(frame->time_seconds*motion*(0.25 + 0.015*i));
        float wobble = sinf(phase*2.0f*PI + (float)i)*extent*0.025f*amplitude;
        float radius = extent*((float)i/rings) + wobble;
        float start_angle = rotation + (float)i*7.5f;
        float sweep = (220.0f + amplitude*140.0f)*arc;
        if (sweep < 20.0f) sweep = 20.0f;
        if (sweep > 356.0f) sweep = 356.0f;
        float thickness = (1.0f + amplitude*8.0f)*renderer->pixel_scale*weight;
        Color color = ColorFromHSV(fmodf((float)i/rings*280.0f + rotation +
                                        semantic_hue + 360.0f, 360.0f),
                                   0.72f, 0.95f);
        DrawRing(center, radius - thickness*0.5f, radius + thickness*0.5f, start_angle, start_angle + sweep, 96, ColorAlpha(color, 0.25f + amplitude*0.75f));
    }
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
