#include "timeline_layout.h"

#include <math.h>

static bool timeline_band_finite(float value)
{
    return isfinite(value);
}

bool timeline_band_layout(float band_x, float band_y, float band_width,
                          float band_height, float margin,
                          const float *control_widths, size_t control_count,
                          float clear_width, float timecode_width,
                          Timeline_Band *out)
{
    if (out == NULL || control_widths == NULL) return false;
    if (control_count == 0 || control_count > TIMELINE_BAND_CONTROL_CAPACITY) return false;
    if (!timeline_band_finite(band_x) || !timeline_band_finite(band_y) ||
        !timeline_band_finite(band_width) || !timeline_band_finite(band_height) ||
        !timeline_band_finite(margin) || !timeline_band_finite(clear_width) ||
        !timeline_band_finite(timecode_width)) {
        return false;
    }
    if (band_width <= 0.0f || band_height <= 0.0f || margin < 0.0f ||
        clear_width < 0.0f || timecode_width < 0.0f) {
        return false;
    }

    float natural = clear_width;
    for (size_t index = 0; index < control_count; ++index) {
        if (!timeline_band_finite(control_widths[index]) || control_widths[index] < 0.0f) {
            return false;
        }
        // Each control is followed by a margin, including the last one, because
        // the clear button trails the row rather than abutting it.
        natural += control_widths[index] + margin;
    }
    if (natural <= 0.0f) return false;

    const float content_x = band_x + margin;
    const float content_width = band_width - margin*2.0f;

    // First choice: controls and timecode share the band.
    float inline_room = content_width - timecode_width - TIMELINE_BAND_GAP;
    float scale = 1.0f;
    bool timecode_inline = true;
    if (inline_room < natural) {
        scale = inline_room > 0.0f ? inline_room/natural : 0.0f;
        if (scale < TIMELINE_BAND_MIN_SCALE) {
            // Shrinking the row far enough to seat the timecode would make the
            // labels unreadable, so the timecode moves instead of the buttons
            // becoming illegible.
            timecode_inline = false;
            scale = content_width < natural ? content_width/natural : 1.0f;
        }
    }
    bool fits = scale >= TIMELINE_BAND_MIN_SCALE;
    if (!fits) scale = TIMELINE_BAND_MIN_SCALE;
    if (scale > 1.0f) scale = 1.0f;

    float controls_width = natural*scale;
    float cursor = content_x;
    Timeline_Band band = {
        .scale = scale,
        .controls_width = controls_width,
        .controls = {content_x, band_y, controls_width, band_height},
        .timecode_inline = timecode_inline,
        .fits = fits,
    };
    for (size_t index = 0; index < control_count; ++index) {
        cursor += control_widths[index]*scale + margin*scale;
    }
    band.clear = (Ui_Rect){cursor, band_y, clear_width*scale, band_height};

    if (timecode_inline) {
        band.timecode = (Ui_Rect){
            band_x + band_width - margin - timecode_width, band_y,
            timecode_width, band_height,
        };
    } else {
        band.timecode = (Ui_Rect){0.0f, 0.0f, 0.0f, 0.0f};
    }

    *out = band;
    return true;
}
