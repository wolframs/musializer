#include "ui_widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

uint64_t ui_widgets_djb2(uint64_t hash, const void *buf, size_t buf_sz)
{
    const uint8_t *bytes = buf;
    for (size_t i = 0; i < buf_sz; ++i) {
        hash = hash*33 + bytes[i];
    }
    return hash;
}

float ui_widgets_signf(float x)
{
    if (x < 0.0) return -1;
    if (x > 0.0) return 1;
    return 0.0;
}

void ui_widgets_snap_segment_inside_other_segment(float ls, float rs,
                                                  float *lt, float *rt)
{
    float dt = *rt - *lt;
    if (rs < *lt || rs < *rt) {
        *rt = rs;
        *lt = rs - dt;
    }

    if (*lt < ls || *rt < ls) {
        *lt = ls;
        *rt = ls + dt;
    }
}

void ui_widgets_snap_boundary_inside_screen(Rectangle *boundary)
{
    float ls = 0;
    float rs = GetScreenWidth();
    float ts = 0;
    float bs = GetScreenHeight();

    float lt = boundary->x;
    float rt = boundary->x + boundary->width;
    float tt = boundary->y;
    float bt = boundary->y + boundary->height;

    ui_widgets_snap_segment_inside_other_segment(ls, rs, &lt, &rt);
    ui_widgets_snap_segment_inside_other_segment(ts, bs, &tt, &bt);

    boundary->x = lt;
    boundary->y = tt;
    boundary->width = rt - lt;
    boundary->height = bt - tt;
}

void ui_widgets_align_to_side_of_rect(Rectangle who, Rectangle *what, Side where)
{
    switch (where) {
        case SIDE_BOTTOM: {
            float cx = who.x + who.width/2;
            float cy = who.y + who.height + TOOLTIP_PADDING;
            what->x = cx - what->width/2;
            what->y = cy;
        } break;

        case SIDE_TOP: {
            float cx = who.x + who.width/2;
            float cy = who.y - TOOLTIP_PADDING - what->height;
            what->x = cx - what->width/2;
            what->y = cy;
        } break;

        case SIDE_RIGHT: {
            float cx = who.x + who.width + TOOLTIP_PADDING;
            float cy = who.y + who.height/2;
            what->x = cx;
            what->y = cy - what->height/2;
        } break;

        case SIDE_LEFT: {
            float cx = who.x - TOOLTIP_PADDING - what->width;
            float cy = who.y + who.height/2;
            what->x = cx;
            what->y = cy - what->height/2;
        } break;

        default: {
        } break;
    }
}

void ui_widgets_begin_tooltip_frame(Ui_Widgets *widgets)
{
    widgets->tooltip_show = false;
}

void ui_widgets_end_tooltip_frame(const Ui_Widgets *widgets, Font font)
{
    if (!widgets->tooltip_show) return;

    float fontSize = 30;
    float spacing = 0.0;
    Vector2 margin = {20.0, 10.0};
    Vector2 text_size = MeasureTextEx(font, widgets->tooltip_buffer, fontSize, spacing);

    Rectangle tooltip_boundary = {
        .width = text_size.x + margin.x*2.0,
        .height = text_size.y + margin.y*2.0,
    };

    Rectangle boundary = tooltip_boundary;
    ui_widgets_align_to_side_of_rect(widgets->tooltip_element_boundary, &boundary,
                                     widgets->tooltip_align);
    ui_widgets_snap_boundary_inside_screen(&boundary);

    DrawRectangleRec(boundary, COLOR_TOOLTIP_BACKGROUND);
    Vector2 position = {
        .x = boundary.x + boundary.width/2 - text_size.x/2,
        .y = boundary.y + boundary.height/2 - text_size.y/2,
    };
    DrawTextEx(font, widgets->tooltip_buffer, position, fontSize, spacing,
               COLOR_TOOLTIP_FOREGROUND);
}

void ui_widgets_tooltip(Ui_Widgets *widgets, Rectangle boundary,
                        const char *text, Side align, bool persists)
{
    if (!(CheckCollisionPointRec(GetMousePosition(), boundary) || persists)) return;
    widgets->tooltip_show = true;
    // TODO: this may not work properly if text contains UTF-8
    snprintf(widgets->tooltip_buffer, sizeof(widgets->tooltip_buffer), "%s", text);
    widgets->tooltip_align = align;
    widgets->tooltip_element_boundary = boundary;
}

int ui_widgets_button_with_id(uint64_t *active_button_id, uint64_t id,
                              Rectangle boundary)
{
    (void)id;
    Vector2 mouse = GetMousePosition();
    int hoverover = CheckCollisionPointRec(mouse, boundary);

    int clicked = 0;
    if (*active_button_id == 0) {
        if (hoverover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            *active_button_id = id;
        }
    } else if (*active_button_id == id) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            *active_button_id = 0;
            if (hoverover) clicked = 1;
        }
    }

    int pressed = hoverover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    return (pressed<<2) | (clicked<<1) | hoverover;
}

// Centres a button label, at a shared row size when one is supplied and at a
// self-fitted size otherwise, and ellipsizes rather than shrinking without end.
static void ui_widgets_draw_button_label(Font font, Rectangle boundary,
                                         const char *label, float font_size,
                                         float press_offset, Color color)
{
    if (label == NULL || label[0] == '\0') return;

    Ui_Widgets_Caption_Measurement measurement = {font, 0.0f, 0.0f};
    float size = font_size;
    if (!(size > 0.0f)) {
        size = ui_row_base_font_size(boundary.height);
        measurement.font_size = size;
        const char *labels[1] = {label};
        float widths[1] = {boundary.width};
        size = ui_row_font_size(labels, widths, 1, size, UI_ROW_MIN_FONT_SIZE,
                                ui_widgets_caption_measure_raylib, &measurement);
    }
    if (!(size > 0.0f)) return;

    measurement.font_size = size;
    char fitted[UI_ROW_LABEL_CAPACITY];
    ui_row_truncate_label(label, boundary.width - UI_ROW_LABEL_PADDING,
                          ui_widgets_caption_measure_raylib, &measurement,
                          fitted, sizeof(fitted));
    if (fitted[0] == '\0') return;

    Vector2 measured = MeasureTextEx(font, fitted, size, 0.0f);
    DrawTextEx(font, fitted,
               (Vector2){boundary.x + (boundary.width - measured.x)*0.5f,
                         boundary.y + (boundary.height - measured.y)*0.5f + press_offset},
               size, 0.0f, color);
}

float ui_widgets_row_font_size(Font font, const char *const *labels,
                               const float *widths, size_t count,
                               float box_height)
{
    float base = ui_row_base_font_size(box_height);
    Ui_Widgets_Caption_Measurement measurement = {font, base, 0.0f};
    return ui_row_font_size(labels, widths, count, base, UI_ROW_MIN_FONT_SIZE,
                            ui_widgets_caption_measure_raylib, &measurement);
}

int ui_widgets_styled_text_button_sized(uint64_t *active_button_id, Font font,
                                        uint64_t id, Rectangle boundary,
                                        const char *label, bool selected,
                                        Button_Style style, float font_size)
{
    int state = ui_widgets_button_with_id(active_button_id, id, boundary);
    Color signal = style == BUTTON_STYLE_DANGER ? COLOR_UI_DANGER :
                                                  COLOR_TRACK_BUTTON_SELECTED;
    Color background = selected ? signal : COLOR_TRACK_BUTTON_BACKGROUND;
    if (state & BS_HOVEROVER) {
        background = selected ? ColorBrightness(signal, 0.12f) :
                                COLOR_TRACK_BUTTON_HOVEROVER;
    }
    if (state & BS_PRESSED) background = ColorBrightness(background, -0.08f);
    DrawRectangleRec(boundary, background);
    DrawRectangleLinesEx(boundary, state & BS_PRESSED ? 2.0f : 1.0f,
                         selected ? signal :
                         style == BUTTON_STYLE_DANGER ? ColorAlpha(signal, 0.72f) :
                                                        COLOR_UI_RULE);
    ui_widgets_draw_button_label(font, boundary, label, font_size,
                                 state & BS_PRESSED ? 1.0f : 0.0f,
                                 selected ? WHITE : COLOR_UI_INK);
    return state;
}

int ui_widgets_styled_text_button(uint64_t *active_button_id, Font font,
                                  uint64_t id, Rectangle boundary,
                                  const char *label, bool selected,
                                  Button_Style style)
{
    return ui_widgets_styled_text_button_sized(active_button_id, font, id, boundary,
                                               label, selected, style, 0.0f);
}

int ui_widgets_text_button_sized(uint64_t *active_button_id, Font font,
                                 uint64_t id, Rectangle boundary,
                                 const char *label, bool selected,
                                 float font_size)
{
    return ui_widgets_styled_text_button_sized(active_button_id, font, id, boundary,
                                               label, selected,
                                               BUTTON_STYLE_NEUTRAL, font_size);
}

int ui_widgets_text_button(uint64_t *active_button_id, Font font, uint64_t id,
                           Rectangle boundary, const char *label, bool selected)
{
    return ui_widgets_text_button_sized(active_button_id, font, id, boundary,
                                        label, selected, 0.0f);
}

int ui_widgets_danger_text_button_sized(uint64_t *active_button_id, Font font,
                                        uint64_t id, Rectangle boundary,
                                        const char *label, bool armed,
                                        float font_size)
{
    return ui_widgets_styled_text_button_sized(active_button_id, font, id, boundary,
                                               label, armed, BUTTON_STYLE_DANGER,
                                               font_size);
}

int ui_widgets_danger_text_button(uint64_t *active_button_id, Font font,
                                  uint64_t id, Rectangle boundary,
                                  const char *label, bool armed)
{
    return ui_widgets_danger_text_button_sized(active_button_id, font, id, boundary,
                                               label, armed, 0.0f);
}

void ui_widgets_disabled_text_button_sized(Font font, Rectangle boundary,
                                           const char *label, bool selected,
                                           float font_size)
{
    Color background = selected ? ColorAlpha(COLOR_TRACK_BUTTON_SELECTED, 0.62f) :
                                  ColorAlpha(COLOR_TRACK_BUTTON_BACKGROUND, 0.72f);
    Color foreground = selected ? ColorAlpha(WHITE, 0.82f) : COLOR_UI_DISABLED;
    DrawRectangleRec(boundary, background);
    DrawRectangleLinesEx(boundary, 1.0f,
                         selected ? ColorAlpha(COLOR_TRACK_BUTTON_SELECTED, 0.7f) :
                                    ColorAlpha(COLOR_UI_RULE, 0.8f));
    ui_widgets_draw_button_label(font, boundary, label, font_size, 0.0f, foreground);
}

void ui_widgets_disabled_text_button(Font font, Rectangle boundary,
                                     const char *label, bool selected)
{
    ui_widgets_disabled_text_button_sized(font, boundary, label, selected, 0.0f);
}

float ui_widgets_slider_get_value(float x, float lox, float hix)
{
    if (x < lox) x = lox;
    if (x > hix) x = hix;
    x -= lox;
    x /= hix - lox;
    return x;
}

float ui_widgets_caption_measure_raylib(const char *text, void *user_data)
{
    Ui_Widgets_Caption_Measurement *measurement = user_data;
    return MeasureTextEx(measurement->font, text, measurement->font_size,
                         measurement->spacing).x;
}

void ui_widgets_draw_wrapped_text(Font font, const char *text, Vector2 position,
                                  float maximum_width, float font_size,
                                  size_t maximum_lines, Color color)
{
    if (text == NULL || text[0] == '\0' || maximum_lines == 0) return;
    Ui_Widgets_Caption_Measurement measurement = {font, font_size, 1.0f};
    Caption_Layout layout;
    if (caption_layout_utf8(text, maximum_width, ui_widgets_caption_measure_raylib,
                            &measurement, &layout) != CAPTION_LAYOUT_OK) return;
    size_t line_count = layout.line_count < maximum_lines ?
                        layout.line_count : maximum_lines;
    for (size_t i = 0; i < line_count; ++i) {
        DrawTextEx(font, layout.lines[i].text,
                   (Vector2){position.x, position.y + (float)i*(font_size + 2.0f)},
                   font_size, 1.0f, color);
    }
}

void ui_widgets_format_timestamp(double seconds, char *output, size_t capacity)
{
    if (seconds < 0.0) seconds = 0.0;
    unsigned minutes = (unsigned)(seconds/60.0);
    double within_minute = seconds - (double)minutes*60.0;
    snprintf(output, capacity, "%02u:%06.3f", minutes, within_minute);
}

void ui_widgets_track_label(Font font, const char *text, Vector2 position,
                            float fontSize, Color tint)
{
    if (font.texture.id == 0) font = GetFontDefault();  // Security check in case of not valid font

    float spacing = 0;

    int size = TextLength(text);    // Total size in bytes of the text, scanned by codepoints in loop

    int textOffsetY = 0;            // Offset between lines (on linebreak '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/font.baseSize;         // Character quad scaling factor

    for (int i = 0; i < size;)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        if (codepoint == '\n') codepoint = ' '; // Treat newlines as spaces

        if ((codepoint != ' ') && (codepoint != '\t'))
        {
            DrawTextCodepoint(font, codepoint, (Vector2){ position.x + textOffsetX, position.y + textOffsetY }, fontSize, tint);
        }

        if (font.glyphs[index].advanceX == 0) textOffsetX += ((float)font.recs[index].width*scaleFactor + spacing);
        else textOffsetX += ((float)font.glyphs[index].advanceX*scaleFactor + spacing);

        i += codepointByteCount;   // Move text bytes counter to next codepoint
    }
}
