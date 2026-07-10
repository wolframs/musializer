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
    float cell_width = boundary.width/bands_count;
    float saturation = 0.75f;
    float value = 1.0f;

    for (size_t i = 0; i < bands_count; ++i) {
        float t = bands[i];
        float hue = (float)i/bands_count;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 startPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*t,
        };
        Vector2 endPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height,
        };
        float thick = cell_width/3*sqrtf(t);
        DrawLineEx(startPos, endPos, thick, color);
    }

    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    SetShaderValue(renderer->circle_shader, renderer->circle_radius_location, (float[1]){ 0.3f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(renderer->circle_shader, renderer->circle_power_location, (float[1]){ 3.0f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(renderer->circle_shader);
    for (size_t i = 0; i < bands_count; ++i) {
        float start = trails[i];
        float end = bands[i];
        float hue = (float)i/bands_count;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 startPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*start,
        };
        Vector2 endPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*end,
        };
        float radius = cell_width*3*sqrtf(end);
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
    SetShaderValue(renderer->circle_shader, renderer->circle_power_location, (float[1]){ 5.0f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(renderer->circle_shader);
    for (size_t i = 0; i < bands_count; ++i) {
        float t = bands[i];
        float hue = (float)i/bands_count;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 center = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*t,
        };
        float radius = cell_width*6*sqrtf(t);
        Vector2 position = {
            .x = center.x - radius,
            .y = center.y - radius,
        };
        DrawTextureEx(texture, position, 0, 2*radius, color);
    }
    EndShaderMode();
}

const Scene_Descriptor scene_spectrum_descriptor = {
    .id = SCENE_SPECTRUM,
    .name = "Spectrum",
    .state_version = 1,
    .draw = spectrum_draw,
};
