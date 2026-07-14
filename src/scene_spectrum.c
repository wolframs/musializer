#include "scene.h"

#include <math.h>

#include <rlgl.h>

static void spectrum_draw(const void *state, const Scene_Frame *frame, const Scene_Renderer *renderer, Rectangle boundary)
{
    (void) state;

    size_t bands_count = frame->audio.bands_count;
    if (bands_count == 0 || frame->audio.bands == NULL || frame->audio.trails == NULL) return;

    const float *bands = frame->audio.bands;
    const float *trails = frame->audio.trails;
    float amplitude_scale = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_AMPLITUDE);
    float trail_scale = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_TRAIL);
    float saturation_scale = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_SATURATION);
    float glow_softness = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_GLOW_SOFTNESS);
    float hue_swing = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_HUE_SWING);
    float core_glow = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_CORE_GLOW);
    float bar_taper = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_BAR_TAPER);
    float reflection = scene_settings_get(
        renderer->settings, SCENE_SPECTRUM, SPECTRUM_SETTING_REFLECTION);
    float cell_width = boundary.width/bands_count;
    // Reflection lifts the bars onto a polished floor strip; zero keeps the
    // original bottom-edge composition.
    float floor_drop = boundary.height*0.13f*fminf(1.0f, reflection*2.0f);
    float base_y = boundary.y + boundary.height - floor_drop;
    float semantic_weight = frame->semantic.available ? frame->semantic.confidence : 0.0f;
    float semantic_hue = frame->semantic.valence*hue_swing*semantic_weight;
    float saturation = (0.75f + frame->semantic.tension*0.2f*semantic_weight)*
                       saturation_scale;
    if (saturation > 1.0f) saturation = 1.0f;
    float value = 1.0f;

    for (size_t i = 0; i < bands_count; ++i) {
        float t = fminf(1.0f, bands[i]*amplitude_scale);
        float hue = (float)i/bands_count;
        Color color = ColorFromHSV(fmodf(hue*360 + semantic_hue + 360.0f, 360.0f), saturation, value);
        Vector2 startPos = {
            boundary.x + i*cell_width + cell_width/2,
            base_y - boundary.height*2/3*t,
        };
        Vector2 endPos = {
            boundary.x + i*cell_width + cell_width/2,
            base_y,
        };
        float thick = cell_width/3*powf(t, bar_taper);
        DrawLineEx(startPos, endPos, thick, color);
        if (reflection > 0.001f && floor_drop > 1.0f) {
            float mirror_height = fminf(floor_drop,
                                        boundary.height*2.0f/3.0f*t*0.45f);
            DrawRectangleGradientV((int)(startPos.x - thick*0.5f), (int)base_y,
                                   (int)fmaxf(1.0f, thick), (int)mirror_height,
                                   ColorAlpha(color, fminf(0.55f,
                                                           0.28f + reflection*0.45f)),
                                   ColorAlpha(color, 0.0f));
        }
    }
    if (reflection > 0.001f && floor_drop > 1.0f) {
        DrawLineEx((Vector2){boundary.x, base_y},
                   (Vector2){boundary.x + boundary.width, base_y},
                   fmaxf(1.0f, renderer->pixel_scale),
                   ColorAlpha(RAYWHITE,
                              (0.14f + frame->audio.rms*0.20f)*
                              fminf(1.0f, reflection*2.0f)));
    }

    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    SetShaderValue(renderer->circle_shader, renderer->circle_radius_location, (float[1]){ 0.3f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->circle_shader, renderer->circle_power_location,
                   &glow_softness, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(renderer->circle_shader);
    for (size_t i = 0; i < bands_count; ++i) {
        float start = fminf(1.0f, trails[i]*amplitude_scale);
        float end = fminf(1.0f, bands[i]*amplitude_scale);
        float hue = (float)i/bands_count;
        Color color = ColorFromHSV(fmodf(hue*360 + semantic_hue + 360.0f, 360.0f), saturation, value);
        Vector2 startPos = {
            boundary.x + i*cell_width + cell_width/2,
            base_y - boundary.height*2/3*start,
        };
        Vector2 endPos = {
            boundary.x + i*cell_width + cell_width/2,
            base_y - boundary.height*2/3*end,
        };
        float radius = cell_width*3*sqrtf(end)*trail_scale;
        Vector2 origin = {0};
        if (endPos.y >= startPos.y) {
            Rectangle dest = {
                .x = startPos.x - radius/2,
                .y = startPos.y,
                .width = radius,
                .height = endPos.y - startPos.y
            };
            Rectangle source = {0, 0, 1, 0.5};
            DrawTexturePro(texture, source, dest, origin, 0, color);
        } else {
            Rectangle dest = {
                .x = endPos.x - radius/2,
                .y = endPos.y,
                .width = radius,
                .height = startPos.y - endPos.y
            };
            Rectangle source = {0, 0.5, 1, 0.5};
            DrawTexturePro(texture, source, dest, origin, 0, color);
        }
    }
    EndShaderMode();

    SetShaderValue(renderer->circle_shader, renderer->circle_radius_location, (float[1]){ 0.07f }, SHADER_UNIFORM_FLOAT);
    float core_softness = glow_softness + 2.0f;
    SetShaderValue(renderer->circle_shader, renderer->circle_power_location,
                   &core_softness, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(renderer->circle_shader);
    for (size_t i = 0; i < bands_count; ++i) {
        float t = fminf(1.0f, bands[i]*amplitude_scale);
        float hue = (float)i/bands_count;
        Color color = ColorFromHSV(fmodf(hue*360 + semantic_hue + 360.0f, 360.0f), saturation, value);
        Vector2 center = {
            boundary.x + i*cell_width + cell_width/2,
            base_y - boundary.height*2/3*t,
        };
        float radius = cell_width*6*sqrtf(t)*core_glow;
        Vector2 position = {
            .x = center.x - radius,
            .y = center.y - radius,
        };
        DrawTextureEx(texture, position, 0, 2*radius, color);
    }
    EndShaderMode();
    if (frame->audio.onset) {
        float flash_height = boundary.height*(0.035f + frame->audio.spectral_flux*0.16f);
        Color flash = ColorAlpha(RAYWHITE,
                                 fminf(0.22f, 0.08f + frame->audio.spectral_flux));
        DrawRectangleGradientV((int)boundary.x,
                               (int)(boundary.y + boundary.height - flash_height),
                               (int)boundary.width, (int)flash_height,
                               ColorAlpha(flash, 0.0f), flash);
    }
    EndBlendMode();
}

const Scene_Descriptor scene_spectrum_descriptor = {
    .id = SCENE_SPECTRUM,
    .name = "Spectrum",
    .state_version = 1,
    .draw = spectrum_draw,
};
