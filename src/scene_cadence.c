#include "scene.h"
#include "scene_cadence_timing.h"

#include <math.h>
#include <string.h>

enum {
    // A full 511-byte cue can contain 256 one-byte words separated by spaces.
    // Derive the fixed bound from the lyric contract so no valid cue is
    // silently truncated when its timed word sequence is assembled.
    CADENCE_MAX_WORDS = (LYRICS_TEXT_CAPACITY + 1U)/2U,
    CADENCE_AMBIENT_PARTICLES = 96,
    CADENCE_MAX_LAYOUT_ROWS = 24,
    CADENCE_LAYOUT_ATTEMPTS = 6,
    CADENCE_PARTICLES_PER_GLYPH = 20,
    // Global per-frame particle ceiling so a pathological cue (hundreds of
    // words dispersing at once) degrades to plain text instead of unbounded
    // draw submissions.
    CADENCE_PARTICLE_BUDGET = 1400,
    CADENCE_INK_PROBES = 8,
    CADENCE_INK_ALPHA_THRESHOLD = 96,
};

typedef struct Cadence_Word {
    const char *text;
    size_t bytes;
    size_t glyphs;
    // Estimated singing window, normalized to [0,1] across the cue span.
    float window_start;
    float window_end;
    // Layout slot assigned for the current frame's boundary.
    float x;
    float y;
    float width;
    size_t row;
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

static float cadence_smooth(float value)
{
    value = cadence_clamp01(value);
    return value*value*(3.0f - 2.0f*value);
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

// Estimated word timing: split the line's span proportionally by glyph
// count (+1 for the breath between words). This is a derived estimate over
// line-level cues, never measured word timestamps.
static void cadence_assign_windows(Cadence_Word *words, size_t count)
{
    unsigned glyph_counts[CADENCE_MAX_WORDS];
    float starts[CADENCE_MAX_WORDS];
    float ends[CADENCE_MAX_WORDS];
    if (count == 0 || count > CADENCE_MAX_WORDS) return;
    for (size_t i = 0; i < count; ++i) glyph_counts[i] = (unsigned)words[i].glyphs;
    if (!cadence_timing_assign_windows(glyph_counts, count, starts, ends)) return;
    for (size_t i = 0; i < count; ++i) {
        words[i].window_start = starts[i];
        words[i].window_end = ends[i];
    }
}

static void cadence_word_text(const Cadence_Word *word, char *text, size_t capacity)
{
    size_t bytes = word->bytes < capacity - 1U ? word->bytes : capacity - 1U;
    memcpy(text, word->text, bytes);
    text[bytes] = '\0';
}

// Wrap the whole line into centered rows so every word owns a stable slot
// for the duration of the cue; shrink the type until the block fits.
static float cadence_layout(Cadence_Word *words, size_t count, Font font,
                            Rectangle boundary, float scale,
                            float spacing_scale, float *spacing_out)
{
    float font_size = fmaxf(10.0f, boundary.height*0.20f*scale);
    float max_width = boundary.width*0.84f;
    float max_height = boundary.height*0.76f;
    float spacing = font_size*0.03f*spacing_scale;
    float line_advance = font_size*1.16f;
    size_t rows = 1;
    for (int attempt = 0; attempt < CADENCE_LAYOUT_ATTEMPTS; ++attempt) {
        spacing = font_size*0.03f*spacing_scale;
        line_advance = font_size*1.16f;
        float space_width = font_size*0.34f;
        float cursor = 0.0f;
        rows = 1;
        bool fits = true;
        for (size_t i = 0; i < count; ++i) {
            char text[LYRICS_TEXT_CAPACITY];
            cadence_word_text(&words[i], text, sizeof(text));
            words[i].width = MeasureTextEx(font, text, font_size, spacing).x;
            if (words[i].width > max_width) fits = false;
            if (cursor > 0.0f && cursor + space_width + words[i].width > max_width) {
                rows += 1;
                cursor = 0.0f;
            }
            words[i].x = cursor > 0.0f ? cursor + space_width : 0.0f;
            words[i].row = rows - 1U;
            cursor = words[i].x + words[i].width;
        }
        if (fits && rows <= CADENCE_MAX_LAYOUT_ROWS &&
            (float)rows*line_advance <= max_height) break;
        font_size *= 0.82f;
    }
    float row_extent[CADENCE_MAX_LAYOUT_ROWS] = {0};
    for (size_t i = 0; i < count; ++i) {
        if (words[i].row >= CADENCE_MAX_LAYOUT_ROWS) {
            words[i].row = CADENCE_MAX_LAYOUT_ROWS - 1U;
        }
        float extent = words[i].x + words[i].width;
        if (extent > row_extent[words[i].row]) row_extent[words[i].row] = extent;
    }
    size_t used_rows = rows < CADENCE_MAX_LAYOUT_ROWS ? rows : CADENCE_MAX_LAYOUT_ROWS;
    float block_top = boundary.y +
                      (boundary.height - (float)used_rows*line_advance)*0.5f;
    for (size_t i = 0; i < count; ++i) {
        words[i].x += boundary.x + (boundary.width - row_extent[words[i].row])*0.5f;
        words[i].y = block_top + (float)words[i].row*line_advance;
    }
    *spacing_out = spacing;
    return font_size;
}

static float cadence_glyph_alpha_at(Image image, int x, int y)
{
    const unsigned char *data = image.data;
    size_t index = (size_t)y*(size_t)image.width + (size_t)x;
    switch (image.format) {
    case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: return (float)data[index];
    case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA: return (float)data[index*2U + 1U];
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: return (float)data[index*4U + 3U];
    default: return 0.0f;
    }
}

// Deterministically sample points inside the glyph's inked pixels (fonts
// loaded from TTF retain CPU-side glyph bitmaps) so particles condense onto
// the letterform itself, not a bounding box. Falls back to the glyph cell
// center when a bitmap is unavailable.
static size_t cadence_glyph_ink(Font font, int codepoint, uint64_t seed,
                                uint64_t salt, size_t want, Vector2 *out,
                                size_t capacity)
{
    if (want > capacity) want = capacity;
    int glyph = font.glyphs != NULL ? GetGlyphIndex(font, codepoint) : 0;
    GlyphInfo info = font.glyphs != NULL ? font.glyphs[glyph] : (GlyphInfo){0};
    Image image = info.image;
    bool sampled = image.data != NULL && image.width > 0 && image.height > 0 &&
                   (image.format == PIXELFORMAT_UNCOMPRESSED_GRAYSCALE ||
                    image.format == PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA ||
                    image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    for (size_t k = 0; k < want; ++k) {
        Vector2 point = {
            (float)info.offsetX + (float)image.width*0.5f,
            (float)info.offsetY + (float)image.height*0.5f,
        };
        if (sampled) {
            for (size_t probe = 0; probe < CADENCE_INK_PROBES; ++probe) {
                uint64_t probe_salt = salt + k*(CADENCE_INK_PROBES*2U) + probe*2U;
                int x = (int)(cadence_unit(seed, probe_salt)*
                              (float)(image.width - 1) + 0.5f);
                int y = (int)(cadence_unit(seed, probe_salt + 1U)*
                              (float)(image.height - 1) + 0.5f);
                if (cadence_glyph_alpha_at(image, x, y) >
                    (float)CADENCE_INK_ALPHA_THRESHOLD) {
                    point = (Vector2){(float)info.offsetX + (float)x + 0.5f,
                                      (float)info.offsetY + (float)y + 0.5f};
                    break;
                }
            }
        }
        out[k] = point;
    }
    return want;
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

// Word focus envelope: 0 = loose particle cloud hovering at its slot,
// 1 = settled legible type. Words gather slightly as their window
// approaches, snap into formation while sung, and hold afterward.
static float cadence_word_focus(const Cadence_Word *word, float cue_position,
                                float focus_speed, bool onset, bool *active_out)
{
    *active_out = false;
    if (cue_position >= word->window_end) return 1.0f;
    if (cue_position < word->window_start) {
        float lead = word->window_start - cue_position;
        return 0.14f*cadence_clamp01(1.0f - lead/0.30f);
    }
    *active_out = true;
    float span = word->window_end - word->window_start;
    if (span < 0.0001f) span = 0.0001f;
    float progress = (cue_position - word->window_start)/span;
    float focus = cadence_clamp01(progress*(2.4f + focus_speed*1.8f));
    if (onset) focus = fmaxf(focus, 0.93f);
    return focus;
}

static void cadence_draw_word(const Cadence_State *cadence,
                              const Scene_Frame *frame, Font font,
                              const Cadence_Word *word, size_t word_index,
                              float font_size, float spacing,
                              Rectangle boundary, Color ink, float focus,
                              bool active, float swarm, float beat_response,
                              float glow, float pixel_scale,
                              size_t *particle_budget)
{
    char text[LYRICS_TEXT_CAPACITY];
    cadence_word_text(word, text, sizeof(text));
    size_t bytes = strlen(text);

    float ink_alpha = active ? 0.55f + focus*0.45f :
                      focus >= 1.0f ? 0.62f :
                      cadence_smooth((focus - 0.55f)/0.45f)*0.5f;
    // The beat coil: the swarm tightens toward the letterform approaching
    // the beat and relaxes just after the phase wraps.
    float coil = 1.0f - beat_response*0.15f*sinf(frame->audio.beat_phase*PI);
    float scatter_reach = fminf(boundary.width, boundary.height)*swarm*coil;
    Vector2 word_center = {word->x + word->width*0.5f, word->y + font_size*0.5f};
    float particle_alpha = (1.0f - cadence_smooth((focus - 0.60f)/0.35f))*0.88f;
    bool wants_particles = focus < 0.985f && particle_alpha > 0.01f;

    float cursor = word->x;
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
        Vector2 pen = {cursor, word->y};
        float band = frame->audio.bands_count > 0 && frame->audio.bands != NULL ?
            frame->audio.bands[(word_index + glyph_index)%frame->audio.bands_count] :
            0.0f;
        uint64_t glyph_salt = (frame->lyric->id + 1U)*UINT64_C(0x9e3779b97f4a7c15) +
                              word_index*UINT64_C(0x2545f4914f6cdd1d) + glyph_index;

        if (wants_particles && *particle_budget > 0) {
            size_t want = word->glyphs > 0 ?
                320U/word->glyphs : CADENCE_PARTICLES_PER_GLYPH;
            if (want < 6U) want = 6U;
            if (want > CADENCE_PARTICLES_PER_GLYPH) want = CADENCE_PARTICLES_PER_GLYPH;
            if (want > *particle_budget) want = *particle_budget;
            Vector2 ink_points[CADENCE_PARTICLES_PER_GLYPH];
            want = cadence_glyph_ink(font, codepoint, cadence->seed,
                                     glyph_salt*64U, want, ink_points,
                                     CADENCE_PARTICLES_PER_GLYPH);
            *particle_budget -= want;
            float glyph_scale = font_size/
                                (float)(font.baseSize > 0 ? font.baseSize : 1);
            BeginBlendMode(BLEND_ADDITIVE);
            for (size_t k = 0; k < want; ++k) {
                uint64_t salt = glyph_salt*64U + 40U + k;
                Vector2 target = {
                    pen.x + ink_points[k].x*glyph_scale,
                    pen.y + ink_points[k].y*glyph_scale,
                };
                float angle = cadence_unit(cadence->seed, salt)*2.0f*PI +
                              frame->audio.beat_phase*0.4f;
                float distance = (0.10f + cadence_unit(cadence->seed, salt + 1U)*
                                  0.24f)*scatter_reach;
                Vector2 home = {
                    word_center.x + cosf(angle)*distance,
                    word_center.y + sinf(angle)*distance,
                };
                // Stagger arrivals so the word condenses organically
                // instead of translating as one rigid clump.
                float arrive = cadence_smooth(
                    focus*1.35f - cadence_unit(cadence->seed, salt + 2U)*0.35f);
                Vector2 position = {
                    home.x + (target.x - home.x)*arrive,
                    home.y + (target.y - home.y)*arrive,
                };
                float jitter = (1.0f - arrive)*(1.5f + band*7.0f)*pixel_scale;
                position.x += sinf((float)frame->time_seconds*2.7f +
                                   (float)(k + glyph_index))*jitter;
                position.y += cosf((float)frame->time_seconds*2.1f +
                                   (float)(k + glyph_index)*1.3f)*jitter;
                float size = (1.1f + band*2.0f)*(1.35f - 0.55f*arrive)*
                             pixel_scale*fmaxf(glow, 0.25f);
                DrawCircleV(position, size,
                            ColorAlpha(ink, particle_alpha*
                                            (0.35f + 0.65f*arrive)));
            }
            EndBlendMode();
        }

        if (ink_alpha > 0.01f) {
            if (active && glow > 0.01f) {
                BeginBlendMode(BLEND_ADDITIVE);
                DrawTextCodepoint(font, codepoint,
                                  (Vector2){pen.x - 1.5f*pixel_scale, pen.y},
                                  font_size, ColorAlpha(ink, 0.16f*glow*focus));
                EndBlendMode();
            }
            DrawTextCodepoint(font, codepoint, pen, font_size,
                              ColorAlpha(ink, ink_alpha));
        }
        cursor += glyph_measure.x + spacing;
        offset += (size_t)codepoint_bytes;
        glyph_index += 1;
    }
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
    cadence_assign_windows(words, word_count);

    double duration = frame->lyric->end_seconds - frame->lyric->start_seconds;
    float cue_position = duration > 0.0 ?
        (float)fmin(1.0, fmax(0.0,
            (frame->time_seconds - frame->lyric->start_seconds)/duration)) : 1.0f;
    // The whole line loosens back into particles over the cue's final beats.
    float hold = cadence_timing_line_hold(cue_position);

    Font font = renderer->font.texture.id != 0 ? renderer->font : GetFontDefault();
    float spacing = 0.0f;
    float font_size = cadence_layout(words, word_count, font, boundary, scale,
                                     spacing_scale, &spacing);

    size_t particle_budget = CADENCE_PARTICLE_BUDGET;
    for (size_t i = 0; i < word_count; ++i) {
        bool active = false;
        float focus = cadence_word_focus(&words[i], cue_position, focus_speed,
                                         frame->audio.onset, &active);
        // Per word, not per line: the last word's window ends at exactly the
        // end of the cue, so the line's dissolve used to scale its focus toward
        // zero over the whole of its own moment and it never settled into type.
        float word_hold = cadence_timing_word_hold(cue_position,
                                                   words[i].window_end, hold);
        focus *= word_hold;
        cadence_draw_word(cadence, frame, font, &words[i], i, font_size,
                          spacing, boundary, ink, focus,
                          active && word_hold > CADENCE_HOLD_LEGIBLE,
                          swarm, beat_response, glow, pixel_scale,
                          &particle_budget);
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
