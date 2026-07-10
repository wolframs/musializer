#include "scene.h"

#include <limits.h>
#include <math.h>

enum {
    ASCII_FIELD_MAX_COLUMNS = 96,
    ASCII_FIELD_MAX_ROWS = 54,
};

typedef struct {
    uint64_t seed;
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

static Font ascii_font(const Scene_Renderer *renderer)
{
    Font font = renderer->font;
    if (font.texture.id == 0 || font.baseSize <= 0 || font.glyphCount <= 0) {
        font = GetFontDefault();
    }
    return font;
}

static void ascii_draw_empty(const Scene_Renderer *renderer, Rectangle boundary)
{
    Font font = ascii_font(renderer);
    const char *title = "ASCII FIELD // AWAITING IMAGE";
    const char *hint = "drop an image to give the signal a face";
    float title_size = fminf(22.0f, fmaxf(10.0f, boundary.width/26.0f));
    float hint_size = title_size*0.62f;
    Vector2 title_measure = MeasureTextEx(font, title, title_size, 1.0f);
    Vector2 hint_measure = MeasureTextEx(font, hint, hint_size, 0.5f);
    float center_x = boundary.x + boundary.width*0.5f;
    float center_y = boundary.y + boundary.height*0.5f;
    Color title_color = { 140, 224, 218, 205 };
    Color hint_color = { 112, 132, 148, 165 };

    DrawLineEx((Vector2){ center_x - title_measure.x*0.58f, center_y - title_size },
               (Vector2){ center_x + title_measure.x*0.58f, center_y - title_size },
               1.0f, (Color){ 73, 117, 128, 120 });
    DrawTextEx(font, title,
               (Vector2){ center_x - title_measure.x*0.5f, center_y - title_size*0.68f },
               title_size, 1.0f, title_color);
    DrawTextEx(font, hint,
               (Vector2){ center_x - hint_measure.x*0.5f, center_y + title_size*0.62f },
               hint_size, 0.5f, hint_color);
}

static void ascii_field_init(void *state, uint64_t seed)
{
    Ascii_Field_State *field = state;
    field->seed = seed;
}

static void ascii_field_update(void *state, const Scene_Frame *frame)
{
    (void)state;
    (void)frame;
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
    float hue = fmodf(196.0f + seed_phase*9.5f + time*(1.0f + flux*5.0f), 360.0f);
    if (hue < 0.0f) hue += 360.0f;
    Color background = ColorFromHSV(hue, 0.52f, 0.035f + energy*0.025f);
    DrawRectangleRec(boundary, background);

    BeginScissorMode(scissor_x, scissor_y, scissor_width, scissor_height);
    if (renderer->ascii_cells == NULL || renderer->ascii_columns == 0 ||
        renderer->ascii_rows == 0 ||
        renderer->ascii_columns > SIZE_MAX/renderer->ascii_rows) {
        ascii_draw_empty(renderer, boundary);
        EndScissorMode();
        return;
    }

    size_t columns = renderer->ascii_columns;
    size_t rows = renderer->ascii_rows;
    size_t draw_columns = columns < ASCII_FIELD_MAX_COLUMNS
        ? columns : ASCII_FIELD_MAX_COLUMNS;
    size_t draw_rows = rows < ASCII_FIELD_MAX_ROWS ? rows : ASCII_FIELD_MAX_ROWS;
    float cell_extent = fminf(boundary.width/(float)draw_columns,
                              boundary.height/(float)draw_rows);
    if (!isfinite(cell_extent) || cell_extent <= 0.0f) {
        EndScissorMode();
        return;
    }
    float field_width = cell_extent*(float)draw_columns;
    float field_height = cell_extent*(float)draw_rows;
    float origin_x = boundary.x + (boundary.width - field_width)*0.5f;
    float origin_y = boundary.y + (boundary.height - field_height)*0.5f;
    Font font = ascii_font(renderer);

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
            const AsciiCell *cell = &renderer->ascii_cells[source_y*columns + source_x];
            float band = ascii_audio_band(frame, column, draw_columns);
            float horizontal_t = ((float)column + 0.5f)/(float)draw_columns;
            float vertical_t = ((float)row + 0.5f)/(float)draw_rows;
            float phase = seed_phase + horizontal_t*PI*5.0f + vertical_t*PI*2.0f;
            float wave = sinf(time*(1.1f + flux*1.7f) + phase);
            float cross_wave = cosf(time*0.71f - horizontal_t*PI*3.0f + phase*0.3f);
            float displacement = cell_extent*(0.04f + band*0.22f + pulse*0.08f);
            float center_x = origin_x + ((float)column + 0.5f)*cell_extent
                           + cross_wave*displacement*0.35f;
            float center_y = origin_y + ((float)row + 0.5f)*cell_extent
                           + wave*displacement;

            float edge = ascii_clamp01(cell->edge_strength*4.0f);
            float luminance = ascii_clamp01(cell->luminance);
            float font_size = cell_extent*(0.82f + band*0.18f + edge*0.10f + pulse*0.06f);
            float alpha = (0.30f + luminance*0.48f + edge*0.34f)
                        * (0.76f + energy*0.22f + band*0.24f);
            float source_alpha = (float)cell->foreground.a/255.0f;
            alpha *= 0.45f + source_alpha*0.55f;
            alpha = ascii_clamp01(alpha);

            Color color = {
                cell->foreground.r,
                cell->foreground.g,
                cell->foreground.b,
                (unsigned char)(alpha*255.0f + 0.5f),
            };
            if (luminance < 0.35f) {
                float lift = 1.0f - luminance/0.35f;
                lift = 0.72f + lift*0.28f;
                float target_r = 96.0f + 112.0f*band;
                float target_g = 196.0f + 54.0f*band;
                float target_b = 224.0f + 30.0f*band;
                color.r = (unsigned char)(color.r + (target_r - color.r)*lift);
                color.g = (unsigned char)(color.g + (target_g - color.g)*lift);
                color.b = (unsigned char)(color.b + (target_b - color.b)*lift);
                if (color.a < 176) color.a = 176;
            }

            uint32_t glyph = cell->glyph <= 0x10ffffu ? cell->glyph : 0xfffdu;
            if (glyph != ' ' && glyph != 0) {
                Vector2 measured = MeasureTextEx(font, "M", font_size, 0.0f);
                Vector2 position = {
                    center_x - measured.x*0.5f,
                    center_y - measured.y*0.5f,
                };
                DrawTextCodepoint(font, (int)glyph, position, font_size, color);
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
    EndScissorMode();
}

const Scene_Descriptor scene_ascii_field_descriptor = {
    .id = SCENE_ASCII_FIELD,
    .name = "ASCII Field",
    .state_version = 1,
    .state_size = sizeof(Ascii_Field_State),
    .init = ascii_field_init,
    .update = ascii_field_update,
    .draw = ascii_field_draw,
};
