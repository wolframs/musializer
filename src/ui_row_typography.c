#include "ui_row_typography.h"

#include <math.h>
#include <string.h>

// U+2026 HORIZONTAL ELLIPSIS. The UI font's curated codepoint set includes it
// (see caption_font_codepoints), so it renders rather than falling back to a box.
static const char UI_ROW_ELLIPSIS[] = "\xE2\x80\xA6";

// Largest index not exceeding limit that starts a UTF-8 sequence. Reading the
// terminator is intentional: a limit equal to the length is already a boundary.
static size_t ui_row_utf8_floor(const char *text, size_t limit)
{
    while (limit > 0 && ((unsigned char)text[limit] & 0xC0u) == 0x80u) --limit;
    return limit;
}

float ui_row_base_font_size(float box_height)
{
    if (!isfinite(box_height) || box_height <= 0.0f) return 0.0f;
    float size = box_height*0.52f;
    return size < 22.0f ? size : 22.0f;
}

float ui_row_font_size(const char *const *labels, const float *widths,
                       size_t count, float base_size, float min_size,
                       Caption_Measure_Text measure, void *user_data)
{
    if (!isfinite(base_size) || base_size <= 0.0f) return 0.0f;

    // A caller asking for a floor above the base must not have the text grow.
    float floor_size = 0.0f;
    if (isfinite(min_size) && min_size > 0.0f) {
        floor_size = min_size < base_size ? min_size : base_size;
    }
    if (labels == NULL || widths == NULL || measure == NULL) return base_size;

    float size = base_size;
    for (size_t i = 0; i < count; ++i) {
        if (labels[i] == NULL || labels[i][0] == '\0') continue;
        if (!isfinite(widths[i])) continue;

        float available = widths[i] - UI_ROW_LABEL_PADDING;
        if (available <= 0.0f) return floor_size;

        float measured = measure(labels[i], user_data);
        if (!isfinite(measured) || measured <= 0.0f) continue;
        if (measured <= available) continue;

        float fitted = base_size*(available/measured);
        if (fitted < size) size = fitted;
    }

    return size < floor_size ? floor_size : size;
}

bool ui_row_truncate_label(const char *label, float available_width,
                           Caption_Measure_Text measure, void *user_data,
                           char *output, size_t capacity)
{
    if (output == NULL || capacity == 0) return false;
    output[0] = '\0';
    if (label == NULL || label[0] == '\0') return false;

    size_t length = strlen(label);
    bool fits_buffer = length < capacity;

    if (fits_buffer) {
        if (measure == NULL) {
            memcpy(output, label, length + 1);
            return false;
        }
        float width = measure(label, user_data);
        if (isfinite(width) && isfinite(available_width) &&
            width <= available_width) {
            memcpy(output, label, length + 1);
            return false;
        }
    }

    // sizeof covers the three ellipsis bytes plus the terminator.
    if (capacity < sizeof(UI_ROW_ELLIPSIS)) return true;
    size_t prefix_limit = capacity - sizeof(UI_ROW_ELLIPSIS);
    size_t prefix = ui_row_utf8_floor(label, length < prefix_limit ? length
                                                                   : prefix_limit);

    for (;;) {
        memcpy(output, label, prefix);
        memcpy(output + prefix, UI_ROW_ELLIPSIS, sizeof(UI_ROW_ELLIPSIS));
        if (prefix == 0) break;
        if (measure != NULL && isfinite(available_width)) {
            float width = measure(output, user_data);
            if (isfinite(width) && width <= available_width) break;
        } else if (measure == NULL) {
            break;  // Buffer-bound truncation only; nothing to measure against.
        }
        prefix = ui_row_utf8_floor(label, prefix - 1);
    }
    return true;
}
