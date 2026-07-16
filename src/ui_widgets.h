#ifndef MUSIALIZER_UI_WIDGETS_H_
#define MUSIALIZER_UI_WIDGETS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <raylib.h>

#include "caption_layout.h"
#include "ui_theme.h"

// Side of an anchor rectangle on which a tooltip is aligned. Shared by the
// tooltip widget and any panel that positions popovers.
typedef enum {
    SIDE_LEFT,
    SIDE_RIGHT,
    SIDE_TOP,
    SIDE_BOTTOM,
} Side;

// Mutable widget state that must survive a hot reload. This lives inside the
// global Plug struct and is passed to every widget call by pointer; the module
// keeps no file-scope state of its own (see AGENTS.md hot-reload invariants).
typedef struct {
    bool tooltip_show;
    char tooltip_buffer[256];
    Side tooltip_align;
    Rectangle tooltip_element_boundary;
} Ui_Widgets;

// Button interaction state bitmask returned by the button widgets.
typedef enum {
    BS_NONE      = 0,
    BS_HOVEROVER = 1,
    BS_CLICKED   = 2,
    BS_PRESSED   = 4,
} Button_State;

typedef enum {
    BUTTON_STYLE_NEUTRAL,
    BUTTON_STYLE_DANGER,
} Button_Style;

#define UI_WIDGETS_DJB2_INIT 5381

uint64_t ui_widgets_djb2(uint64_t hash, const void *buf, size_t buf_sz);

float ui_widgets_signf(float x);
void ui_widgets_snap_segment_inside_other_segment(float ls, float rs,
                                                  float *lt, float *rt);
void ui_widgets_snap_boundary_inside_screen(Rectangle *boundary);
void ui_widgets_align_to_side_of_rect(Rectangle who, Rectangle *what, Side where);

void ui_widgets_begin_tooltip_frame(Ui_Widgets *widgets);
void ui_widgets_end_tooltip_frame(const Ui_Widgets *widgets, Font font);
void ui_widgets_tooltip(Ui_Widgets *widgets, Rectangle boundary,
                        const char *text, Side align, bool persists);

int ui_widgets_button_with_id(uint64_t *active_button_id, uint64_t id,
                              Rectangle boundary);
int ui_widgets_styled_text_button(uint64_t *active_button_id, Font font,
                                  uint64_t id, Rectangle boundary,
                                  const char *label, bool selected,
                                  Button_Style style);
int ui_widgets_text_button(uint64_t *active_button_id, Font font, uint64_t id,
                           Rectangle boundary, const char *label, bool selected);
int ui_widgets_danger_text_button(uint64_t *active_button_id, Font font,
                                  uint64_t id, Rectangle boundary,
                                  const char *label, bool armed);
void ui_widgets_disabled_text_button(Font font, Rectangle boundary,
                                     const char *label, bool selected);

float ui_widgets_slider_get_value(float x, float lox, float hix);

// Raylib-backed text measurement adapter for caption_layout, shared by the
// notice tray and any panel that wraps UTF-8 text to a pixel width.
typedef struct {
    Font font;
    float font_size;
    float spacing;
} Ui_Widgets_Caption_Measurement;

float ui_widgets_caption_measure_raylib(const char *text, void *user_data);
void ui_widgets_draw_wrapped_text(Font font, const char *text, Vector2 position,
                                  float maximum_width, float font_size,
                                  size_t maximum_lines, Color color);

void ui_widgets_format_timestamp(double seconds, char *output, size_t capacity);
void ui_widgets_track_label(Font font, const char *text, Vector2 position,
                            float fontSize, Color tint);

#endif // MUSIALIZER_UI_WIDGETS_H_