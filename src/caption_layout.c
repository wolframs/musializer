#include "caption_layout.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct Caption_Codepoint_Range {
    uint32_t first;
    uint32_t last;
} Caption_Codepoint_Range;

static const Caption_Codepoint_Range caption_codepoint_ranges[] = {
    // Basic Latin, Latin-1, Latin Extended A/B, and IPA Extensions.
    {0x0020u, 0x007Eu},
    {0x00A0u, 0x024Fu},
    // Decomposed accents; important for canonically equivalent lyric text.
    {0x0300u, 0x036Fu},
    // Greek/Coptic, Cyrillic, and Cyrillic Supplement.
    {0x0370u, 0x052Fu},
    // Latin Extended Additional and Greek Extended.
    {0x1E00u, 0x1FFFu},
    // General Punctuation and Currency Symbols (includes U+2026 ellipsis).
    {0x2000u, 0x206Fu},
    {0x20A0u, 0x20CFu},
    // Deliberate symbols present in the bundled Alegreya font rather than
    // enormous blocks that would only produce missing-glyph placeholders.
    {0x2116u, 0x2116u}, // numero sign
    {0x2122u, 0x2122u}, // trademark
    {0x2190u, 0x2199u}, // common arrows
    {0x2212u, 0x2212u}, // mathematical minus
    {0x221Eu, 0x221Eu}, // infinity
    {0x2248u, 0x2248u}, // approximately equal
    {0x2260u, 0x2260u}, // not equal
    {0x2264u, 0x2265u}, // less/greater than or equal
    {0x25A0u, 0x25A1u}, // squares
    {0x25B2u, 0x25B2u}, // up triangle
    {0x25B6u, 0x25B6u}, // play triangle
    {0x25BCu, 0x25BCu}, // down triangle
    {0x25C0u, 0x25C0u}, // reverse triangle
    {0x25C6u, 0x25C6u}, // diamond
};

static bool utf8_decode(const unsigned char *bytes, size_t available,
                        uint32_t *codepoint, size_t *byte_count)
{
    if (bytes == NULL || codepoint == NULL || byte_count == NULL || available == 0) {
        return false;
    }
    unsigned char first = bytes[0];
    size_t continuation = 0;
    uint32_t value = 0;
    if (first < 0x80u) {
        *codepoint = first;
        *byte_count = 1;
        return true;
    } else if (first >= 0xC2u && first <= 0xDFu) {
        continuation = 1;
        value = (uint32_t)(first & 0x1Fu);
    } else if (first >= 0xE0u && first <= 0xEFu) {
        continuation = 2;
        value = (uint32_t)(first & 0x0Fu);
    } else if (first >= 0xF0u && first <= 0xF4u) {
        continuation = 3;
        value = (uint32_t)(first & 0x07u);
    } else {
        return false;
    }
    if (available <= continuation) return false;
    for (size_t i = 1; i <= continuation; ++i) {
        unsigned char byte = bytes[i];
        if ((byte & 0xC0u) != 0x80u) return false;
        value = (value << 6) | (uint32_t)(byte & 0x3Fu);
    }
    if ((continuation == 1 && value < 0x80u) ||
        (continuation == 2 && value < 0x800u) ||
        (continuation == 3 && value < 0x10000u) ||
        (value >= 0xD800u && value <= 0xDFFFu) || value > 0x10FFFFu) {
        return false;
    }
    *codepoint = value;
    *byte_count = continuation + 1;
    return true;
}

static bool codepoint_is_space(uint32_t codepoint)
{
    return codepoint == 0x0009u || codepoint == 0x000Au ||
           codepoint == 0x000Bu || codepoint == 0x000Cu ||
           codepoint == 0x000Du || codepoint == 0x0020u ||
           codepoint == 0x0085u || codepoint == 0x00A0u ||
           codepoint == 0x1680u ||
           (codepoint >= 0x2000u && codepoint <= 0x200Au) ||
           codepoint == 0x2028u || codepoint == 0x2029u ||
           codepoint == 0x202Fu || codepoint == 0x205Fu ||
           codepoint == 0x3000u;
}

static Caption_Layout_Result normalize_text(const char *text,
                                            char normalized[CAPTION_LAYOUT_SOURCE_CAPACITY],
                                            size_t *normalized_length)
{
    size_t length = 0;
    while (length < CAPTION_LAYOUT_SOURCE_CAPACITY && text[length] != '\0') length += 1;
    if (length == CAPTION_LAYOUT_SOURCE_CAPACITY) {
        return CAPTION_LAYOUT_ERROR_SOURCE_TOO_LONG;
    }

    size_t source = 0;
    size_t destination = 0;
    bool pending_space = false;
    while (source < length) {
        uint32_t codepoint = 0;
        size_t byte_count = 0;
        if (!utf8_decode((const unsigned char *)&text[source], length - source,
                         &codepoint, &byte_count)) {
            return CAPTION_LAYOUT_ERROR_INVALID_UTF8;
        }
        if ((codepoint < 0x20u || (codepoint >= 0x7Fu && codepoint <= 0x9Fu)) &&
            !codepoint_is_space(codepoint)) {
            return CAPTION_LAYOUT_ERROR_INVALID_TEXT;
        }
        if (codepoint_is_space(codepoint)) {
            pending_space = destination > 0;
        } else {
            if (pending_space) normalized[destination++] = ' ';
            memcpy(&normalized[destination], &text[source], byte_count);
            destination += byte_count;
            pending_space = false;
        }
        source += byte_count;
    }
    normalized[destination] = '\0';
    *normalized_length = destination;
    return destination == 0 ? CAPTION_LAYOUT_ERROR_EMPTY : CAPTION_LAYOUT_OK;
}

static Caption_Layout_Result measure_bytes(const char *text, size_t length,
                                           Caption_Measure_Text measure,
                                           void *user_data, float *width)
{
    if (length >= CAPTION_LAYOUT_LINE_CAPACITY) {
        return CAPTION_LAYOUT_ERROR_SOURCE_TOO_LONG;
    }
    char candidate[CAPTION_LAYOUT_LINE_CAPACITY];
    memcpy(candidate, text, length);
    candidate[length] = '\0';
    float measured = measure(candidate, user_data);
    if (!isfinite(measured) || measured < 0.0f) {
        return CAPTION_LAYOUT_ERROR_MEASUREMENT;
    }
    *width = measured;
    return CAPTION_LAYOUT_OK;
}

static Caption_Layout_Result measure_with_ellipsis(const char *text, size_t length,
                                                   Caption_Measure_Text measure,
                                                   void *user_data, float *width)
{
    static const char ellipsis[] = "\xE2\x80\xA6";
    if (length + sizeof(ellipsis) > CAPTION_LAYOUT_LINE_CAPACITY) {
        return CAPTION_LAYOUT_ERROR_SOURCE_TOO_LONG;
    }
    char candidate[CAPTION_LAYOUT_LINE_CAPACITY];
    memcpy(candidate, text, length);
    memcpy(&candidate[length], ellipsis, sizeof(ellipsis));
    float measured = measure(candidate, user_data);
    if (!isfinite(measured) || measured < 0.0f) {
        return CAPTION_LAYOUT_ERROR_MEASUREMENT;
    }
    *width = measured;
    return CAPTION_LAYOUT_OK;
}

static void store_line(Caption_Layout *layout, const char *text, size_t length,
                       float width, float max_width)
{
    Caption_Layout_Line *line = &layout->lines[layout->line_count++];
    memcpy(line->text, text, length);
    line->text[length] = '\0';
    line->byte_length = length;
    line->width = width;
    line->centered_offset = (max_width - width)*0.5f;
    if (line->centered_offset < 0.0f) line->centered_offset = 0.0f;
}

static Caption_Layout_Result store_ellipsized_line(
    Caption_Layout *layout, const char *text, size_t length,
    float width, float max_width)
{
    static const char ellipsis[] = "\xE2\x80\xA6";
    char candidate[CAPTION_LAYOUT_LINE_CAPACITY];
    if (length + sizeof(ellipsis) > sizeof(candidate)) {
        return CAPTION_LAYOUT_ERROR_SOURCE_TOO_LONG;
    }
    memcpy(candidate, text, length);
    memcpy(&candidate[length], ellipsis, sizeof(ellipsis));
    store_line(layout, candidate, length + sizeof(ellipsis) - 1, width, max_width);
    layout->ellipsized = true;
    return CAPTION_LAYOUT_OK;
}

Caption_Layout_Result caption_layout_utf8(const char *text,
                                          float max_width,
                                          Caption_Measure_Text measure,
                                          void *user_data,
                                          Caption_Layout *output)
{
    if (text == NULL || measure == NULL || output == NULL) {
        return CAPTION_LAYOUT_ERROR_NULL;
    }
    if (!isfinite(max_width) || max_width <= 0.0f) {
        return CAPTION_LAYOUT_ERROR_WIDTH;
    }

    char normalized[CAPTION_LAYOUT_SOURCE_CAPACITY];
    size_t normalized_length = 0;
    Caption_Layout_Result result = normalize_text(text, normalized, &normalized_length);
    if (result != CAPTION_LAYOUT_OK) return result;

    Caption_Layout layout = {0};
    size_t position = 0;
    while (position < normalized_length &&
           layout.line_count < CAPTION_LAYOUT_MAX_LINES) {
        const char *remaining = &normalized[position];
        size_t remaining_length = normalized_length - position;
        float full_width = 0.0f;
        result = measure_bytes(remaining, remaining_length, measure, user_data, &full_width);
        if (result != CAPTION_LAYOUT_OK) return result;
        if (full_width <= max_width) {
            store_line(&layout, remaining, remaining_length, full_width, max_width);
            position = normalized_length;
            break;
        }

        if (layout.line_count + 1 == CAPTION_LAYOUT_MAX_LINES) {
            float ellipsis_width = 0.0f;
            result = measure_with_ellipsis("", 0, measure, user_data, &ellipsis_width);
            if (result != CAPTION_LAYOUT_OK) return result;
            if (ellipsis_width > max_width) return CAPTION_LAYOUT_ERROR_TOO_NARROW;

            size_t best_any = 0;
            float best_any_width = ellipsis_width;
            size_t best_word = 0;
            float best_word_width = 0.0f;
            for (size_t at = 0; at < remaining_length;) {
                uint32_t codepoint = 0;
                size_t byte_count = 0;
                // normalized text was already validated.
                (void)utf8_decode((const unsigned char *)&remaining[at],
                                  remaining_length - at, &codepoint, &byte_count);
                if (codepoint == 0x20u && at > 0) {
                    float candidate_width = 0.0f;
                    result = measure_with_ellipsis(remaining, at, measure,
                                                   user_data, &candidate_width);
                    if (result != CAPTION_LAYOUT_OK) return result;
                    if (candidate_width <= max_width) {
                        best_word = at;
                        best_word_width = candidate_width;
                    }
                } else {
                    size_t end = at + byte_count;
                    float candidate_width = 0.0f;
                    result = measure_with_ellipsis(remaining, end, measure,
                                                   user_data, &candidate_width);
                    if (result != CAPTION_LAYOUT_OK) return result;
                    if (candidate_width <= max_width) {
                        best_any = end;
                        best_any_width = candidate_width;
                    }
                }
                at += byte_count;
            }
            size_t prefix = best_word > 0 ? best_word : best_any;
            float width = best_word > 0 ? best_word_width : best_any_width;
            result = store_ellipsized_line(&layout, remaining, prefix, width, max_width);
            if (result != CAPTION_LAYOUT_OK) return result;
            position = normalized_length;
            break;
        }

        size_t best_any = 0;
        float best_any_width = 0.0f;
        size_t best_word = 0;
        float best_word_width = 0.0f;
        size_t best_word_consumed = 0;
        for (size_t at = 0; at < remaining_length;) {
            uint32_t codepoint = 0;
            size_t byte_count = 0;
            (void)utf8_decode((const unsigned char *)&remaining[at],
                              remaining_length - at, &codepoint, &byte_count);
            if (codepoint == 0x20u && at > 0) {
                float candidate_width = 0.0f;
                result = measure_bytes(remaining, at, measure, user_data,
                                       &candidate_width);
                if (result != CAPTION_LAYOUT_OK) return result;
                if (candidate_width <= max_width) {
                    best_word = at;
                    best_word_width = candidate_width;
                    best_word_consumed = at + byte_count;
                }
            } else {
                size_t end = at + byte_count;
                float candidate_width = 0.0f;
                result = measure_bytes(remaining, end, measure, user_data,
                                       &candidate_width);
                if (result != CAPTION_LAYOUT_OK) return result;
                if (candidate_width <= max_width) {
                    best_any = end;
                    best_any_width = candidate_width;
                }
            }
            at += byte_count;
        }
        if (best_word > 0) {
            store_line(&layout, remaining, best_word, best_word_width, max_width);
            position += best_word_consumed;
        } else if (best_any > 0) {
            store_line(&layout, remaining, best_any, best_any_width, max_width);
            position += best_any;
        } else {
            return CAPTION_LAYOUT_ERROR_TOO_NARROW;
        }
        while (position < normalized_length && normalized[position] == ' ') position += 1;
    }

    if (position != normalized_length || layout.line_count == 0) {
        return CAPTION_LAYOUT_ERROR_TOO_NARROW;
    }
    *output = layout;
    return CAPTION_LAYOUT_OK;
}

size_t caption_font_codepoint_count(void)
{
    size_t count = 0;
    for (size_t i = 0;
         i < sizeof(caption_codepoint_ranges)/sizeof(caption_codepoint_ranges[0]); ++i) {
        const Caption_Codepoint_Range *range = &caption_codepoint_ranges[i];
        if (range->last < range->first) return 0;
        size_t range_count = (size_t)(range->last - range->first) + 1u;
        if (range_count > CAPTION_FONT_CODEPOINT_LIMIT - count) return 0;
        count += range_count;
    }
    return count;
}

Caption_Font_Result caption_font_codepoints(int *output,
                                            size_t capacity,
                                            size_t *written)
{
    if (written == NULL) return CAPTION_FONT_ERROR_NULL;
    size_t required = caption_font_codepoint_count();
    *written = required;
    if (required == 0 || required > CAPTION_FONT_CODEPOINT_LIMIT) {
        return CAPTION_FONT_ERROR_INTERNAL_RANGE;
    }
    if (output == NULL) return CAPTION_FONT_ERROR_NULL;
    if (capacity < required) return CAPTION_FONT_ERROR_BUFFER_TOO_SMALL;

    size_t destination = 0;
    uint32_t previous = 0;
    for (size_t i = 0;
         i < sizeof(caption_codepoint_ranges)/sizeof(caption_codepoint_ranges[0]); ++i) {
        const Caption_Codepoint_Range *range = &caption_codepoint_ranges[i];
        if (i > 0 && range->first <= previous) {
            return CAPTION_FONT_ERROR_INTERNAL_RANGE;
        }
        for (uint32_t codepoint = range->first; codepoint <= range->last; ++codepoint) {
            output[destination++] = (int)codepoint;
        }
        previous = range->last;
    }
    return destination == required ? CAPTION_FONT_OK : CAPTION_FONT_ERROR_INTERNAL_RANGE;
}
