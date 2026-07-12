#include "caption_layout.h"
#include "test_support.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Test_Measurement {
    float glyph_width;
    float space_width;
    bool return_nan;
} Test_Measurement;

static float test_measure(const char *text, void *user_data)
{
    Test_Measurement *measurement = (Test_Measurement *)user_data;
    if (measurement->return_nan) return NAN;
    float width = 0.0f;
    const unsigned char *bytes = (const unsigned char *)text;
    for (size_t i = 0; bytes[i] != 0;) {
        if (bytes[i] == ' ') {
            width += measurement->space_width;
            i += 1;
        } else {
            width += measurement->glyph_width;
            if (bytes[i] < 0x80u) i += 1;
            else if ((bytes[i] & 0xE0u) == 0xC0u) i += 2;
            else if ((bytes[i] & 0xF0u) == 0xE0u) i += 3;
            else i += 4;
        }
    }
    return width;
}

static bool contains_codepoint(const int *codepoints, size_t count, int wanted)
{
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low)/2;
        if (codepoints[middle] < wanted) low = middle + 1;
        else high = middle;
    }
    return low < count && codepoints[low] == wanted;
}

TEST(caption_wraps_words_and_reports_center_offsets)
{
    Test_Measurement measurement = {.glyph_width = 10.0f, .space_width = 5.0f};
    Caption_Layout layout = {0};
    REQUIRE_TRUE(caption_layout_utf8("one two three", 70.0f, test_measure,
                                     &measurement, &layout) == CAPTION_LAYOUT_OK);
    EXPECT_EQ_SIZE(layout.line_count, 2);
    EXPECT_TRUE(strcmp(layout.lines[0].text, "one two") == 0);
    EXPECT_TRUE(strcmp(layout.lines[1].text, "three") == 0);
    EXPECT_NEAR(layout.lines[0].width, 65.0, 0.0);
    EXPECT_NEAR(layout.lines[0].centered_offset, 2.5, 0.0);
    EXPECT_NEAR(layout.lines[1].centered_offset, 10.0, 0.0);
    EXPECT_FALSE(layout.ellipsized);
}

TEST(caption_splits_long_unicode_words_only_at_codepoint_boundaries)
{
    Test_Measurement measurement = {.glyph_width = 10.0f, .space_width = 5.0f};
    Caption_Layout layout = {0};
    REQUIRE_TRUE(caption_layout_utf8("αβγδεζηθικ", 30.0f, test_measure,
                                     &measurement, &layout) == CAPTION_LAYOUT_OK);
    EXPECT_EQ_SIZE(layout.line_count, 3);
    EXPECT_TRUE(strcmp(layout.lines[0].text, "αβγ") == 0);
    EXPECT_TRUE(strcmp(layout.lines[1].text, "δεζ") == 0);
    EXPECT_TRUE(strcmp(layout.lines[2].text, "ηθ…") == 0);
    EXPECT_TRUE(layout.ellipsized);
    EXPECT_EQ_SIZE(layout.lines[2].byte_length, strlen("ηθ…"));
}

TEST(caption_normalizes_unicode_whitespace_without_leading_or_trailing_gaps)
{
    Test_Measurement measurement = {.glyph_width = 10.0f, .space_width = 5.0f};
    Caption_Layout layout = {0};
    REQUIRE_TRUE(caption_layout_utf8("  Grüße\xC2\xA0\tκόσμε  ", 300.0f,
                                     test_measure, &measurement, &layout) ==
                 CAPTION_LAYOUT_OK);
    EXPECT_EQ_SIZE(layout.line_count, 1);
    EXPECT_TRUE(strcmp(layout.lines[0].text, "Grüße κόσμε") == 0);
    EXPECT_FALSE(layout.ellipsized);
}

TEST(caption_truncation_is_always_visible_and_prefers_a_word_boundary)
{
    Test_Measurement measurement = {.glyph_width = 10.0f, .space_width = 5.0f};
    Caption_Layout layout = {0};
    REQUIRE_TRUE(caption_layout_utf8(
        "one two three four five six seven eight nine ten", 75.0f,
        test_measure, &measurement, &layout) == CAPTION_LAYOUT_OK);
    EXPECT_EQ_SIZE(layout.line_count, CAPTION_LAYOUT_MAX_LINES);
    EXPECT_TRUE(layout.ellipsized);
    EXPECT_TRUE(strstr(layout.lines[2].text, "…") != NULL);
    EXPECT_TRUE(layout.lines[2].width <= 75.0f);
}

TEST(caption_errors_are_atomic_and_reject_invalid_utf8_or_measurements)
{
    Test_Measurement measurement = {.glyph_width = 10.0f, .space_width = 5.0f};
    Caption_Layout layout = {.line_count = 2, .ellipsized = true};
    snprintf(layout.lines[0].text, sizeof(layout.lines[0].text), "unchanged");
    Caption_Layout before = layout;
    const char invalid[] = {(char)0xC0, (char)0xAF, '\0'};
    EXPECT_TRUE(caption_layout_utf8(invalid, 100.0f, test_measure,
                                    &measurement, &layout) ==
                CAPTION_LAYOUT_ERROR_INVALID_UTF8);
    EXPECT_TRUE(memcmp(&layout, &before, sizeof(layout)) == 0);

    measurement.return_nan = true;
    EXPECT_TRUE(caption_layout_utf8("hello", 100.0f, test_measure,
                                    &measurement, &layout) ==
                CAPTION_LAYOUT_ERROR_MEASUREMENT);
    EXPECT_TRUE(memcmp(&layout, &before, sizeof(layout)) == 0);
}

TEST(caption_rejects_unterminated_or_too_narrow_input_without_clipping)
{
    Test_Measurement measurement = {.glyph_width = 10.0f, .space_width = 5.0f};
    Caption_Layout layout = {0};
    char too_long[CAPTION_LAYOUT_SOURCE_CAPACITY];
    memset(too_long, 'x', sizeof(too_long));
    EXPECT_TRUE(caption_layout_utf8(too_long, 100.0f, test_measure,
                                    &measurement, &layout) ==
                CAPTION_LAYOUT_ERROR_SOURCE_TOO_LONG);
    EXPECT_TRUE(caption_layout_utf8("x", 5.0f, test_measure,
                                    &measurement, &layout) ==
                CAPTION_LAYOUT_ERROR_TOO_NARROW);
}

TEST(caption_font_codepoints_are_bounded_sorted_and_cover_target_scripts)
{
    size_t count = caption_font_codepoint_count();
    REQUIRE_TRUE(count > 0);
    REQUIRE_TRUE(count <= CAPTION_FONT_CODEPOINT_LIMIT);
    int codepoints[CAPTION_FONT_CODEPOINT_LIMIT];
    size_t written = 0;
    REQUIRE_TRUE(caption_font_codepoints(codepoints, count, &written) ==
                 CAPTION_FONT_OK);
    EXPECT_EQ_SIZE(written, count);
    for (size_t i = 1; i < written; ++i) {
        EXPECT_TRUE(codepoints[i - 1] < codepoints[i]);
    }
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x00E9)); // é
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x0141)); // Ł
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x0301)); // combining acute
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x03A9)); // Ω
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x0416)); // Ж
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x1E9E)); // capital sharp s
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x2026)); // ellipsis
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x20AC)); // euro
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x2192)); // right arrow
    EXPECT_TRUE(contains_codepoint(codepoints, written, 0x25A0)); // black square
}

TEST(caption_font_short_buffer_is_reported_without_partial_write)
{
    size_t count = caption_font_codepoint_count();
    REQUIRE_TRUE(count > 1);
    int output[4] = {11, 22, 33, 44};
    int before[4];
    memcpy(before, output, sizeof(output));
    size_t written = 0;
    EXPECT_TRUE(caption_font_codepoints(output, 4, &written) ==
                CAPTION_FONT_ERROR_BUFFER_TOO_SMALL);
    EXPECT_EQ_SIZE(written, count);
    EXPECT_TRUE(memcmp(output, before, sizeof(output)) == 0);
}
