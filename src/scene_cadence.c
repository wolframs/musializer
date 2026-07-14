#include "scene.h"

#include <math.h>
#include <string.h>

enum {
    CADENCE_MAX_WORDS = 32,
    CADENCE_AMBIENT_PARTICLES = 96,
};

typedef struct Cadence_Word {
    const char *text;
    size_t bytes;
    size_t glyphs;
} Cadence_Word;

typedef struct Cadence_State {
    uint64_t seed;
} Cadence_State;

static float cadence_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint64_t cadence_mix(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static float cadence_unit(uint64_t seed, uint64_t salt)
{
    return (float)(cadence_mix(seed ^ salt) & UINT64_C(0xffff))/65535.0f;
}

static size_t cadence_count_glyphs(const char *text, size_t bytes)
{
    size_t count = 0;
    size_t offset = 0;
    while (offset < bytes) {
        int size = 0;
        (void)GetCodepointNext(text + offset, &size);
        if (size <= 0 || (size_t)size > bytes - offset) size = 1;
        offset += (size_t)size;
        count += 1;
    }
    return count;
}

static size_t cadence_split_words(const char *text, Cadence_Word *words,
                                  size_t capacity)
{
    if (text == NULL || words == NULL || capacity == 0) return 0;
    size_t count = 0;
    const char *at = text;
    while (*at != '\0' && count < capacity) {
        while (*at != '\0' && (unsigned char)*at <= 0x20u) at += 1;
        if (*at == '\0') break;
        const char *start = at;
        while (*at != '\0' && (unsigned char)*at > 0x20u) at += 1;
        size_t bytes = (size_t)(at - start);
        words[count++] = (Cadence_Word){
            .text = start,
            .bytes = bytes,
            .glyphs = cadence_count_glyphs(start, bytes),
        };
    }
    return count;
}

static void cadence_init(void *state, uint64_t seed)
{
    Cadence_State *cadence = state;
    cadence->seed = seed;
}

static void cadence_draw_ambient(const Cadence_State *cadence,
                                 const Scene_Frame *frame, Rectangle boundary,
                                 Color color, float swarm, float pixel_scale)
{
    float phase = frame->audio.beat_phase*2.0f*PI;
    BeginBlendMode(BLEND_ADDITIVE);
    for (size_t i = 0; i < CADENCE_AMBIENT_PARTICLES; ++i) {
        float angle = cadence_unit(cadence->seed, i*3U + 1U)*2.0f*PI +
                      phase*0.12f;
        float radius = sqrtf(cadence_unit(cadence->seed, i*3U + 2U))*
                       fminf(boundary.width, boundary.height)*0.38f;
        float breathing = 0.84f + 0.16f*sinf(
            (float)frame->time_seconds*0.43f + (float)i*0.31f);
        Vector2 point = {
            boundary.x + boundary.width*0.5f + cosf(angle)*radius*breathing,
            boundary.y + boundary.height*0.5f + sinf(angle)*radius*breathing,
        };
        float size = (0.7f + cadence_unit(cadence->seed, i*3U + 3U)*1.8f +
                      frame->audio.rms*2.5f)*pixel_scale*swarm;
        DrawCircleV(point, size, ColorAlpha(color, 0.10f + frame->audio.rms*0.16f));
    }
    EndBlendMode();
}

static void cadence_draw(const void *state, const Scene_Frame *frame,
                         const Scene_Renderer *renderer, Rectangle boundary)
{
    const Cadence_State *cadence = state;
    if (cadence == NULL || frame == NULL || renderer == NULL ||
        boundary.width <= 1.0f || boundary.height <= 1.0f) return;

    float scale = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_SCALE);
    float swarm = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_SWARM);
    float focus_speed = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_FOCUS);
    float beat_response = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_BEAT);
    float glow = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_GLOW);
    float spacing_scale = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_SPACING);
    float hue_swing = scene_settings_get(
        renderer->settings, SCENE_CADENCE, CADENCE_SETTING_HUE_SWING);
    float pixel_scale = renderer->pixel_scale > 0.0f ? renderer->pixel_scale : 1.0f;
    float semantic_weight = frame->semantic.available ?
                            cadence_clamp01(frame->semantic.confidence) : 0.0f;
    float hue = fmodf(205.0f + frame->semantic.valence*hue_swing*semantic_weight +
                      frame->audio.spectral_flux*72.0f + 360.0f, 360.0f);
    Color background = ColorFromHSV(hue, 0.62f, 0.025f + frame->audio.rms*0.025f);
    Color ink = ColorFromHSV(fmodf(hue + 118.0f, 360.0f),
                             0.48f + frame->semantic.tension*0.28f*semantic_weight,
                             0.94f);
    DrawRectangleRec(boundary, background);

    if (frame->lyric == NULL || frame->lyric->text[0] == '\0') {
        cadence_draw_ambient(cadence, frame, boundary, ink, swarm, pixel_scale);
        return;
    }

    Cadence_Word words[CADENCE_MAX_WORDS];
    size_t word_count = cadence_split_words(
        frame->lyric->text, words, CADENCE_MAX_WORDS);
    if (word_count == 0) return;
    size_t total_weight = 0;
    for (size_t i = 0; i < word_count; ++i) total_weight += words[i].glyphs + 1U;
    double duration = frame->lyric->end_seconds - frame->lyric->start_seconds;
    double cue_position = duration > 0.0 ?
        (frame->time_seconds - frame->lyric->start_seconds)/duration : 0.0;
    cue_position = fmin(1.0, fmax(0.0, cue_position));
    double weighted_position = cue_position*(double)total_weight;
    size_t word_index = word_count - 1U;
    size_t weight_before = 0;
    for (size_t i = 0; i < word_count; ++i) {
        size_t weight = words[i].glyphs + 1U;
        if (weighted_position < (double)(weight_before + weight) ||
            i + 1U == word_count) {
            word_index = i;
            break;
        }
        weight_before += weight;
    }
    Cadence_Word word = words[word_index];
    char text[LYRICS_TEXT_CAPACITY];
    size_t bytes = word.bytes < sizeof(text) - 1U ? word.bytes : sizeof(text) - 1U;
    memcpy(text, word.text, bytes);
    text[bytes] = '\0';

    double word_span = (double)(word.glyphs + 1U);
    float word_progress = cadence_clamp01(
        (float)((weighted_position - (double)weight_before)/word_span));
    float attack = cadence_clamp01(word_progress*(3.2f + focus_speed*2.0f));
    float release = cadence_clamp01((1.0f - word_progress)*(3.8f + focus_speed));
    float focus = attack*release;
    if (frame->audio.onset) focus = fmaxf(focus, 0.92f);
    float anticipation = 1.0f - beat_response*0.12f*
                         sinf(frame->audio.beat_phase*PI);
    focus = cadence_clamp01(focus*anticipation);

    Font font = renderer->font.texture.id != 0 ? renderer->font : GetFontDefault();
    float font_size = boundary.height*0.27f*scale;
    float spacing = font_size*0.035f*spacing_scale;
    Vector2 measurement = MeasureTextEx(font, text, font_size, spacing);
    float maximum_width = boundary.width*0.78f;
    if (measurement.x > maximum_width && measurement.x > 0.0f) {
        font_size *= maximum_width/measurement.x;
        spacing = font_size*0.035f*spacing_scale;
        measurement = MeasureTextEx(font, text, font_size, spacing);
    }
    float origin_x = boundary.x + (boundary.width - measurement.x)*0.5f;
    float baseline_y = boundary.y + (boundary.height - font_size)*0.49f;

    float cursor = origin_x;
    size_t offset = 0;
    size_t glyph_index = 0;
    while (offset < bytes) {
        int codepoint_bytes = 0;
        int codepoint = GetCodepointNext(text + offset, &codepoint_bytes);
        if (codepoint_bytes <= 0 || (size_t)codepoint_bytes > bytes - offset) {
            codepoint_bytes = 1;
            codepoint = '?';
        }
        int encoded_size = 0;
        const char *encoded = CodepointToUTF8(codepoint, &encoded_size);
        Vector2 glyph_measure = MeasureTextEx(font, encoded, font_size, 0.0f);
        Vector2 target = {cursor, baseline_y};
        uint64_t salt = frame->lyric->id*UINT64_C(0x9e3779b97f4a7c15) + glyph_index;
        float angle = cadence_unit(cadence->seed, salt)*2.0f*PI +
                      frame->audio.beat_phase*0.35f;
        float distance = (0.14f + cadence_unit(cadence->seed, salt + 1U)*0.25f)*
                         fminf(boundary.width, boundary.height)*swarm;
        Vector2 scattered = {
            boundary.x + boundary.width*0.5f + cosf(angle)*distance,
            boundary.y + boundary.height*0.5f + sinf(angle)*distance,
        };
        Vector2 position = {
            scattered.x + (target.x - scattered.x)*focus,
            scattered.y + (target.y - scattered.y)*focus,
        };
        float band = frame->audio.bands_count > 0 && frame->audio.bands != NULL ?
            frame->audio.bands[glyph_index%frame->audio.bands_count] : 0.0f;
        float jitter = (1.0f - focus)*(2.0f + band*8.0f)*pixel_scale*swarm;
        position.x += sinf((float)frame->time_seconds*2.7f + (float)glyph_index)*jitter;
        position.y += cosf((float)frame->time_seconds*2.1f + (float)glyph_index)*jitter;

        BeginBlendMode(BLEND_ADDITIVE);
        for (int particle = 1; particle <= 3; ++particle) {
            float t = (float)particle/4.0f;
            Vector2 point = {
                scattered.x + (position.x - scattered.x)*t,
                scattered.y + (position.y - scattered.y)*t,
            };
            DrawCircleV(point, (0.8f + band*1.8f)*pixel_scale*glow,
                        ColorAlpha(ink, (1.0f - focus)*0.18f));
        }
        DrawTextCodepoint(font, codepoint,
                          (Vector2){position.x - 1.5f*pixel_scale,
                                    position.y},
                          font_size, ColorAlpha(ink, 0.16f*glow));
        EndBlendMode();
        DrawTextCodepoint(font, codepoint, position, font_size,
                          ColorAlpha(ink, 0.38f + focus*0.62f));
        cursor += glyph_measure.x + spacing;
        offset += (size_t)codepoint_bytes;
        glyph_index += 1;
    }
}

const Scene_Descriptor scene_cadence_descriptor = {
    .id = SCENE_CADENCE,
    .name = "Cadence",
    .state_version = 1,
    .state_size = sizeof(Cadence_State),
    .init = cadence_init,
    .draw = cadence_draw,
};
