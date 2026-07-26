#ifndef MUSIALIZER_TIMELINE_LAYOUT_H_
#define MUSIALIZER_TIMELINE_LAYOUT_H_

#include <stdbool.h>
#include <stddef.h>

#include "workspace_layout.h"

// Placement for the timeline's top band: the panel/event control row, the
// "Clear manual" button that trails it, and the right-aligned timecode.
//
// These used to be positioned independently against the same band. The control
// row declared a width it never used and its children ran 36 px past it, while
// the timecode was right-aligned into the same 38 px strip, so below roughly
// 785 px of workspace the timecode printed straight through "+ Custom" and
// "Clear manual". That is reachable in a supported window: at the 960 px
// minimum with the inspector open the band is only 680 px wide.
//
// The rule here is that the parent extent is computed from the children rather
// than declared beside them, so a label or button width change cannot silently
// reintroduce the overlap.

#define TIMELINE_BAND_CONTROL_CAPACITY 8u

// Below this the shared label font (see ui_row_typography.h) has already
// bottomed out, so squeezing further trades collision for illegibility.
#define TIMELINE_BAND_MIN_SCALE 0.72f

// Clear space kept between the control row and the timecode so they read as
// separate groups instead of touching.
#define TIMELINE_BAND_GAP 12.0f

typedef struct Timeline_Band {
    // Multiplier applied to every control width and to the clear button.
    float scale;
    // True extent of the scaled row, measured from its children.
    float controls_width;
    Ui_Rect controls;
    Ui_Rect clear;
    Ui_Rect timecode;
    // False when the band cannot seat the timecode beside the controls; the
    // caller must place it elsewhere (the transport row) rather than draw it
    // over the buttons.
    bool timecode_inline;
    // False when even TIMELINE_BAND_MIN_SCALE overflows: the caller is out of
    // room and something has to be dropped. Reported rather than hidden.
    bool fits;
} Timeline_Band;

// control_widths/control_count describe the buttons before the trailing clear
// button. Returns false only on unusable input, in which case *out is untouched.
bool timeline_band_layout(float band_x, float band_y, float band_width,
                          float band_height, float margin,
                          const float *control_widths, size_t control_count,
                          float clear_width, float timecode_width,
                          Timeline_Band *out);

#endif // MUSIALIZER_TIMELINE_LAYOUT_H_
