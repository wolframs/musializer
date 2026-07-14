#include "scene.h"

#include <limits.h>
#include <math.h>
#include <string.h>

enum {
    ASCII_FIELD_MAX_COLUMNS = 96,
    ASCII_FIELD_MAX_ROWS = 54,
};

typedef struct {
    uint64_t seed;
    double last_sample_time;
    float spectrum_history[ASCII_FIELD_MAX_ROWS][ASCII_FIELD_MAX_COLUMNS];
} Ascii_Field_State;

static float ascii_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static float ascii_audio_band(const Scene_Frame *frame, size_t column, size_t columns)
{
    if (frame->audio.bands == NULL || frame->audio.bands_count == 0 || columns == 0) {
        return 0.0f;
    }
    size_t count = frame->audio.bands_count;
    size_t index = (count/columns)*column + ((count%columns)*column)/columns;
    if (index >= count) index = count - 1;
    return ascii_clamp01(frame->audio.bands[index]);
}

static float ascii_seed_phase(uint64_t seed)
{
    seed ^= seed >> 30;
    seed *= UINT64_C(0xbf58476d1ce4e5b9);
    seed ^= seed >> 27;
    seed *= UINT64_C(0x94d049bb133111eb);
    seed ^= seed >> 31;
    return (float)(seed & UINT64_C(0xffff))/65535.0f*2.0f*PI;
}

static float ascii_lerp(float first, float second, float amount)
{
    return first + (second - first)*ascii_clamp01(amount);
}

static unsigned char ascii_color_byte(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0;
    if (value >= 255.0f) return 255;
    return (unsigned char)(value + 0.5f);
}

static Color ascii_main_color(const AsciiCell *cell,
                              float band,
                              float energy,
                              float semantic_tension,
                              float gain)
{
    float luminance = ascii_clamp01(cell->luminance);
    float edge = ascii_clamp01(cell->edge_strength*3.0f);
    float detail = fmaxf(luminance, edge*0.82f);
    float source_r = (float)cell->foreground.r/255.0f;
    float source_g = (float)cell->foreground.g/255.0f;
    float source_b = (float)cell->foreground.b/255.0f;
    float source_max = fmaxf(source_r, fmaxf(source_g, source_b));
    /* Glyph density already carries the source luminance. Keep every mark
     * bright enough to survive 720p/1080p delivery and use the original hue
     * to distinguish regions instead of multiplying dark ink into oblivion. */
    float target = 0.42f + powf(detail, 0.72f)*0.58f;
    float lift = target/fmaxf(source_max, 0.05f);
    if (lift > 8.5f) lift = 8.5f;
    source_r = ascii_clamp01(source_r*lift);
    source_g = ascii_clamp01(source_g*lift);
    source_b = ascii_clamp01(source_b*lift);
    source_r = fmaxf(source_r, target*0.18f);
    source_g = fmaxf(source_g, target*0.65f);
    source_b = fmaxf(source_b, target*0.78f);

    const float cyan_r = 0.0f;
    const float cyan_g = 240.0f/255.0f;
    const float cyan_b = 1.0f;
    float phosphor = 0.11f + edge*0.12f + band*0.08f;
    source_r = ascii_lerp(source_r, cyan_r, phosphor);
    source_g = ascii_lerp(source_g, cyan_g, phosphor);
    source_b = ascii_lerp(source_b, cyan_b, phosphor);

    float source_alpha = (float)cell->foreground.a/255.0f;
    float alpha = (0.44f + powf(detail, 0.68f)*0.56f)*
                  (0.86f + energy*0.08f + band*0.08f);
    alpha *= 0.30f + source_alpha*0.70f;
    alpha *= 0.94f + semantic_tension*0.06f;
    alpha *= ascii_clamp01(0.62f + 0.38f*gain);
    // Gain scales the delivered light, applied after the phosphor pipeline
    // so it survives the lift/blend stages instead of being renormalized.
    return (Color) {
        ascii_color_byte(ascii_clamp01(source_r*gain)*255.0f),
        ascii_color_byte(ascii_clamp01(source_g*gain)*255.0f),
        ascii_color_byte(ascii_clamp01(source_b*gain)*255.0f),
        ascii_color_byte(ascii_clamp01(alpha)*255.0f),
    };
}

static Vector2 ascii_measure_glyph(Font font, uint32_t glyph, float font_size)
{
    int byte_count = 0;
    const char *text = CodepointToUTF8((int)glyph, &byte_count);
    if (text == NULL || byte_count <= 0) return (Vector2){font_size, font_size};
    return MeasureTextEx(font, text, font_size, 0.0f);
}

static void ascii_draw_glyph(Font font,
                             uint32_t glyph,
                             Vector2 center,
                             float font_size,
                             Vector2 measured,
                             Color color)
{
    Vector2 position = {
        center.x - measured.x*0.5f,
        center.y - measured.y*0.5f,
    };
    DrawTextCodepoint(font, (int)glyph, position, font_size, color);
}

static bool ascii_scissor(Rectangle boundary, int *x, int *y, int *width, int *height)
{
    float right = boundary.x + boundary.width;
    float bottom = boundary.y + boundary.height;
    if (!isfinite(boundary.x) || !isfinite(boundary.y) ||
        !isfinite(right) || !isfinite(bottom) ||
        boundary.width <= 1.0f || boundary.height <= 1.0f ||
        boundary.x < (float)INT_MIN || boundary.y < (float)INT_MIN ||
        right > (float)INT_MAX || bottom > (float)INT_MAX) {
        return false;
    }

    int left_i = (int)ceilf(boundary.x);
    int top_i = (int)ceilf(boundary.y);
    int right_i = (int)floorf(right);
    int bottom_i = (int)floorf(bottom);
    if (right_i <= left_i || bottom_i <= top_i) return false;
    *x = left_i;
    *y = top_i;
    *width = right_i - left_i;
    *height = bottom_i - top_i;
    return true;
}

static Font ascii_grid_font(void)
{
    /* The bundled caption face is intentionally literary and proportional.
     * Raylib's built-in pixel face is monospaced, heavier at tiny sizes, and
     * therefore keeps individual ASCII samples legible after video encoding. */
    return GetFontDefault();
}

static void ascii_field_init(void *state, uint64_t seed)
{
    Ascii_Field_State *field = state;
    memset(field, 0, sizeof(*field));
    field->seed = seed;
    field->last_sample_time = -1.0;
}

static void ascii_field_update(void *state, const Scene_Frame *frame)
{
    Ascii_Field_State *field = state;
    if (field == NULL || frame == NULL || !isfinite(frame->time_seconds)) return;
    double elapsed = field->last_sample_time < 0.0 ? 0.0 :
                     frame->time_seconds - field->last_sample_time;
    if (field->last_sample_time >= 0.0 && (elapsed < 0.0 || elapsed > 0.75)) {
        memset(field->spectrum_history, 0, sizeof(field->spectrum_history));
        field->last_sample_time = -1.0;
    }
    if (field->last_sample_time >= 0.0 && elapsed < 1.0/18.0) return;

    memmove(&field->spectrum_history[1][0],
            &field->spectrum_history[0][0],
            (ASCII_FIELD_MAX_ROWS - 1U)*ASCII_FIELD_MAX_COLUMNS*sizeof(float));
    for (size_t column = 0; column < ASCII_FIELD_MAX_COLUMNS; ++column) {
        float value = ascii_audio_band(frame, column, ASCII_FIELD_MAX_COLUMNS);
        float trail = 0.0f;
        if (frame->audio.trails != NULL && frame->audio.bands_count > 0) {
            size_t index = column*frame->audio.bands_count/
                           ASCII_FIELD_MAX_COLUMNS;
            if (index >= frame->audio.bands_count) index = frame->audio.bands_count - 1;
            trail = ascii_clamp01(frame->audio.trails[index]);
        }
        field->spectrum_history[0][column] =
            ascii_clamp01(value*0.78f + trail*0.22f);
    }
    field->last_sample_time = frame->time_seconds;
}

static void ascii_field_draw(const void *state,
                             const Scene_Frame *frame,
                             const Scene_Renderer *renderer,
                             Rectangle boundary)
{
    if (state == NULL || frame == NULL || renderer == NULL) return;
    int scissor_x = 0;
    int scissor_y = 0;
    int scissor_width = 0;
    int scissor_height = 0;
    if (!ascii_scissor(boundary, &scissor_x, &scissor_y,
                       &scissor_width, &scissor_height)) {
        return;
    }

    const Ascii_Field_State *field = state;
    float energy = ascii_clamp01(frame->audio.rms*2.0f);
    float flux = ascii_clamp01(frame->audio.spectral_flux*5.0f);
    float pulse = frame->audio.onset ? 1.0f : 0.0f;
    float time = isfinite(frame->time_seconds)
        ? (float)fmod(frame->time_seconds, 4096.0) : 0.0f;
    float seed_phase = ascii_seed_phase(field->seed);
    float semantic_weight = frame->semantic.available
        ? ascii_clamp01(frame->semantic.confidence) : 0.0f;
    float semantic_tension = frame->semantic.available
        ? ascii_clamp01(frame->semantic.tension)*semantic_weight : 0.0f;
    float semantic_valence = frame->semantic.available
        ? ascii_clamp01((frame->semantic.valence + 1.0f)*0.5f)*semantic_weight : 0.0f;
    float pixel_scale = renderer->pixel_scale > 0.0f
        ? renderer->pixel_scale : 1.0f;
    float motion_scale = scene_settings_get(
        renderer->settings, SCENE_ASCII_FIELD, ASCII_SETTING_MOTION);
    float cycling_scale = scene_settings_get(
        renderer->settings, SCENE_ASCII_FIELD, ASCII_SETTING_CYCLING);
    float scanline_scale = scene_settings_get(
        renderer->settings, SCENE_ASCII_FIELD, ASCII_SETTING_SCANLINES);
    float split_scale = scene_settings_get(
        renderer->settings, SCENE_ASCII_FIELD, ASCII_SETTING_SPLIT);
    float gain = scene_settings_get(
        renderer->settings, SCENE_ASCII_FIELD, ASCII_SETTING_GAIN);
    float tint = scene_settings_get(
        renderer->settings, SCENE_ASCII_FIELD, ASCII_SETTING_TINT);

    /* Deep navy CRT surface: flat enough to preserve image contrast, but with
     * deterministic phosphor bands so an empty dark source does not become an
     * undifferentiated black rectangle. */
    DrawRectangleRec(boundary, (Color){ 5, 4, 18, 255 });
    float background_step = fmaxf(6.0f*pixel_scale, 2.0f);
    for (float y = boundary.y; y < boundary.y + boundary.height;
         y += background_step) {
        DrawRectangleRec((Rectangle){boundary.x, y, boundary.width,
                                     fmaxf(2.0f*pixel_scale, 1.0f)},
                         (Color){0, 0, 0,
                                 ascii_color_byte(34.0f*scanline_scale)});
    }

    BeginScissorMode(scissor_x, scissor_y, scissor_width, scissor_height);
    bool imported = renderer->ascii_cells != NULL &&
                    renderer->ascii_columns > 0 && renderer->ascii_rows > 0 &&
                    renderer->ascii_columns <= SIZE_MAX/renderer->ascii_rows;
    size_t columns = imported ? renderer->ascii_columns : 80U;
    size_t rows = imported ? renderer->ascii_rows : 42U;
    size_t draw_columns = columns < ASCII_FIELD_MAX_COLUMNS
        ? columns : ASCII_FIELD_MAX_COLUMNS;
    size_t draw_rows = rows < ASCII_FIELD_MAX_ROWS ? rows : ASCII_FIELD_MAX_ROWS;
    float margin = fmaxf(9.0f*pixel_scale,
                         fminf(boundary.width, boundary.height)*0.035f);
    if (boundary.width <= margin*2.0f || boundary.height <= margin*2.0f) {
        EndScissorMode();
        return;
    }
    Rectangle drawing_area = {
        boundary.x + margin,
        boundary.y + margin,
        boundary.width - margin*2.0f,
        boundary.height - margin*2.0f,
    };
    AsciiGridLayout layout;
    if (!ascii_art_grid_layout(drawing_area.width, drawing_area.height,
                               draw_columns, draw_rows, 1.0f, &layout)) {
        EndScissorMode();
        return;
    }
    float origin_x = drawing_area.x + layout.offset_x;
    float origin_y = drawing_area.y + layout.offset_y;
    Font font = ascii_grid_font();
    float font_size = layout.cell_height*1.15f;
    Rectangle field_box = {
        origin_x - 2.0f*pixel_scale,
        origin_y - 2.0f*pixel_scale,
        layout.field_width + 4.0f*pixel_scale,
        layout.field_height + 4.0f*pixel_scale,
    };
    DrawRectangleRec(field_box, (Color){ 2, 5, 16, 126 });
    DrawRectangleLinesEx(field_box, fmaxf(pixel_scale, 1.0f),
                         (Color){ 0, 240, 255, 34 });

    size_t source_y = 0;
    size_t y_error = 0;
    size_t y_step = rows/draw_rows;
    size_t y_remainder = rows%draw_rows;
    for (size_t row = 0; row < draw_rows; ++row) {
        size_t source_x = 0;
        size_t x_error = 0;
        size_t x_step = columns/draw_columns;
        size_t x_remainder = columns%draw_columns;
        for (size_t column = 0; column < draw_columns; ++column) {
            size_t history_row = row*ASCII_FIELD_MAX_ROWS/draw_rows;
            size_t history_column = column*ASCII_FIELD_MAX_COLUMNS/draw_columns;
            float spectrum_density = field->spectrum_history[history_row][history_column];
            AsciiCell blended = {
                .glyph = '#',
                .foreground = { 0, 220, 255, 255 },
                .luminance = spectrum_density,
                .edge_strength = 0.0f,
                .edge_orientation = ASCII_EDGE_NONE,
            };
            if (imported) {
                blended = renderer->ascii_cells[source_y*columns + source_x];
                blended.luminance = ascii_clamp01(
                    blended.luminance*0.78f + spectrum_density*0.42f);
                blended.foreground.g = ascii_color_byte(
                    (float)blended.foreground.g*0.82f + spectrum_density*46.0f);
                blended.foreground.b = ascii_color_byte(
                    (float)blended.foreground.b*0.82f + spectrum_density*64.0f);
            } else {
                // Band hue spread stretches the phosphor palette across the
                // frequency axis: zero is a single-hue CRT, one sweeps
                // bass-to-treble through the whole spectrum.
                Color live = ColorFromHSV(
                    fmodf(188.0f + (float)column/(float)draw_columns*
                          (36.0f + tint*270.0f) +
                          semantic_valence*28.0f, 360.0f),
                    0.78f, 0.30f + powf(spectrum_density, 0.75f)*0.70f);
                blended.foreground = (AsciiRgba){
                    live.r, live.g, live.b,
                    ascii_color_byte((0.34f + spectrum_density*1.30f)*255.0f),
                };
            }
            const AsciiCell *cell = &blended;
            float band = ascii_audio_band(frame, column, draw_columns);
            float horizontal_t = ((float)column + 0.5f)/(float)draw_columns;
            float vertical_t = ((float)row + 0.5f)/(float)draw_rows;
            float phase = seed_phase + horizontal_t*PI*4.0f - vertical_t*PI*1.7f;
            float glyph_wave = sinf(time*motion_scale*(0.66f + energy*0.18f) + phase);
            float counter_wave = sinf(time*motion_scale*0.43f - vertical_t*PI*3.2f +
                                      horizontal_t*PI*0.8f + seed_phase);
            float column_drift = glyph_wave*layout.cell_height*
                                 (0.035f + band*0.060f + energy*0.018f)*motion_scale;
            float row_drift = counter_wave*layout.cell_width*
                              (0.012f + flux*0.030f + pulse*0.016f)*motion_scale;
            float center_x = origin_x + ((float)column + 0.5f)*layout.cell_width;
            center_x += row_drift;
            float center_y = origin_y + ((float)row + 0.5f)*layout.cell_height +
                             column_drift;

            float edge = ascii_clamp01(cell->edge_strength*4.0f);
            float luminance = ascii_clamp01(cell->luminance);
            Color color = ascii_main_color(cell, band, energy,
                                           semantic_tension, gain);

            float glyph_activity = ascii_clamp01((energy*0.48f + band*0.36f +
                                                  flux*0.11f + pulse*0.24f)*
                                                 cycling_scale);
            uint32_t glyph = ascii_art_animated_glyph(
                cell, row, column, frame->time_seconds*cycling_scale,
                glyph_activity,
                field->seed);
            if (glyph != ' ' && glyph != 0) {
                Vector2 center = {center_x, center_y};
                Vector2 measured = ascii_measure_glyph(font, glyph, font_size);
                uint64_t cell_hash = field->seed ^
                    ((uint64_t)row*UINT64_C(0x9e3779b97f4a7c15)) ^
                    ((uint64_t)column*UINT64_C(0xbf58476d1ce4e5b9));
                bool chromatic_detail = edge > 0.18f || luminance > 0.62f ||
                    ((cell_hash >> 59) == 0 && luminance > 0.24f);
                if (chromatic_detail) {
                    float split = pixel_scale*(0.42f + flux*1.20f +
                                  pulse*1.10f + semantic_tension*0.65f)*
                                  split_scale;
                    unsigned char ghost_alpha = ascii_color_byte(
                        (float)color.a*(0.13f + flux*0.10f + pulse*0.07f)*
                        split_scale);
                    Color magenta = {
                        255,
                        ascii_color_byte(18.0f + semantic_valence*28.0f),
                        110,
                        ghost_alpha,
                    };
                    Color cyan = {0, 240, 255, ghost_alpha};
                    ascii_draw_glyph(font, glyph,
                                     (Vector2){center.x - split, center.y},
                                     font_size, measured, magenta);
                    ascii_draw_glyph(font, glyph,
                                     (Vector2){center.x + split, center.y},
                                     font_size, measured, cyan);
                }
                ascii_draw_glyph(font, glyph, center, font_size, measured, color);
            }

            size_t x_advance = x_step;
            x_error += x_remainder;
            if (x_error >= draw_columns) {
                x_advance += 1;
                x_error -= draw_columns;
            }
            source_x += x_advance;
        }
        size_t y_advance = y_step;
        y_error += y_remainder;
        if (y_error >= draw_rows) {
            y_advance += 1;
            y_error -= draw_rows;
        }
        source_y += y_advance;
    }

    /* Two-pixel dark bands plus a faint phosphor lip survive downsampling and
     * ordinary web-video compression better than the old alpha-12 hairlines. */
    for (float y = field_box.y; y < field_box.y + field_box.height;
         y += background_step) {
        DrawRectangleRec((Rectangle){field_box.x, y, field_box.width,
                                     fmaxf(2.0f*pixel_scale, 1.0f)},
                         (Color){0, 0, 0,
                                 ascii_color_byte((42.0f + energy*10.0f)*
                                                  scanline_scale)});
        DrawRectangleRec((Rectangle){field_box.x,
                                     y + fmaxf(2.0f*pixel_scale, 1.0f),
                                     field_box.width,
                                     fmaxf(pixel_scale, 1.0f)},
                         (Color){0, 240, 255,
                                 ascii_color_byte(10.0f*scanline_scale)});
    }
    float sweep_phase = fmodf(time*(0.055f + energy*0.045f) +
                              seed_phase/(2.0f*PI), 1.0f);
    if (sweep_phase < 0.0f) sweep_phase += 1.0f;
    float sweep_y = field_box.y + sweep_phase*field_box.height;
    DrawRectangleRec((Rectangle){field_box.x, sweep_y, field_box.width,
                                 fmaxf(pixel_scale, 1.0f)},
                     (Color){0, 240, 255,
                             ascii_color_byte((18.0f + energy*24.0f +
                                               pulse*25.0f)*scanline_scale)});
    EndScissorMode();
}

const Scene_Descriptor scene_ascii_field_descriptor = {
    .id = SCENE_ASCII_FIELD,
    .name = "ASCII Field",
    .state_version = 4,
    .state_size = sizeof(Ascii_Field_State),
    .init = ascii_field_init,
    .update = ascii_field_update,
    .draw = ascii_field_draw,
};
