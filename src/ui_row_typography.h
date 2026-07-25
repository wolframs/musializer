#ifndef MUSIALIZER_UI_ROW_TYPOGRAPHY_H_
#define MUSIALIZER_UI_ROW_TYPOGRAPHY_H_

#include <stdbool.h>
#include <stddef.h>

#include "caption_layout.h"

// Button labels used to be fitted one box at a time: every button shrank its own
// text until it fit, with no floor and no truncation. Neighbouring buttons of
// equal size therefore rendered at unequal sizes as soon as one label was longer
// than the others, which is the single most visible defect in the workspace.
//
// These helpers are deliberately Raylib-free so the fitting rules can be tested
// headlessly, following caption_layout and scene_settings_ui_layout.

// Horizontal room reserved inside a button box, summed across both sides.
#define UI_ROW_LABEL_PADDING 12.0f

// A shared row size stops shrinking here; longer labels ellipsize instead.
// Below roughly this size the UI font stops being readable at 100% scale.
#define UI_ROW_MIN_FONT_SIZE 11.0f

// Bound for a fitted label copy, including the terminator and the ellipsis.
#define UI_ROW_LABEL_CAPACITY 128u

// Size a button of this height starts from, before any label is considered.
// Returns 0 for a non-finite or non-positive height.
float ui_row_base_font_size(float box_height);

// Largest size not exceeding base_size at which every label fits inside its own
// box, floored at min_size (or at base_size when base_size is already smaller).
//
// measure must report the width of a label at exactly base_size, and must be
// linear in font size, which holds for Raylib's MeasureTextEx at zero spacing.
// widths are full box widths; UI_ROW_LABEL_PADDING is removed here. Entries with
// a null, empty, or unmeasurable label are ignored rather than collapsing the
// whole row. A box with no usable room yields the floor, since no size fits.
float ui_row_font_size(const char *const *labels, const float *widths,
                       size_t count, float base_size, float min_size,
                       Caption_Measure_Text measure, void *user_data);

// Writes label into output, replacing the tail with U+2026 when it does not fit
// available_width or the buffer. measure must report widths at the size the
// label will actually be drawn at. Cuts only at UTF-8 sequence boundaries.
//
// Returns true when the result was shortened. output is always terminated when
// capacity is non-zero. A box too narrow even for the ellipsis still gets the
// ellipsis: a missing label reads as a bug, a clipped one reads as a tight box.
// A non-finite available_width is treated as no room at all.
bool ui_row_truncate_label(const char *label, float available_width,
                           Caption_Measure_Text measure, void *user_data,
                           char *output, size_t capacity);

#endif // MUSIALIZER_UI_ROW_TYPOGRAPHY_H_
