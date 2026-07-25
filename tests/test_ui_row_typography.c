#include "ui_row_typography.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

// Stand-in for MeasureTextEx at zero spacing: width is linear in the font size
// and proportional to the codepoint count, which is the property ui_row_font_size
// relies on when it scales a measurement taken at the base size.
typedef struct Row_Measurement {
    float font_size;
    float advance;      // Width of one codepoint at font size 1.
    bool return_nan;
} Row_Measurement;

static size_t row_codepoint_count(const char *text)
{
    size_t count = 0;
    for (const unsigned char *bytes = (const unsigned char *)text; *bytes != 0; ++bytes) {
        if ((*bytes & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

static float row_measure(const char *text, void *user_data)
{
    Row_Measurement *measurement = (Row_Measurement *)user_data;
    if (measurement->return_nan) return NAN;
    return (float)row_codepoint_count(text)*measurement->advance*measurement->font_size;
}

static bool row_is_valid_utf8(const char *text)
{
    const unsigned char *bytes = (const unsigned char *)text;
    while (*bytes != 0) {
        size_t extra;
        if (*bytes < 0x80u) extra = 0;
        else if ((*bytes & 0xE0u) == 0xC0u) extra = 1;
        else if ((*bytes & 0xF0u) == 0xE0u) extra = 2;
        else if ((*bytes & 0xF8u) == 0xF0u) extra = 3;
        else return false;
        ++bytes;
        for (size_t i = 0; i < extra; ++i) {
            if ((*bytes & 0xC0u) != 0x80u) return false;
            ++bytes;
        }
    }
    return true;
}

TEST(ui_row_base_font_size_follows_box_height_and_caps)
{
    EXPECT_NEAR(ui_row_base_font_size(36.0f), 18.72f, 0.001f);
    EXPECT_NEAR(ui_row_base_font_size(30.0f), 15.6f, 0.001f);
    // Tall boxes stop growing so a header never dwarfs the panel it sits in.
    EXPECT_NEAR(ui_row_base_font_size(200.0f), 22.0f, 0.001f);
    EXPECT_NEAR(ui_row_base_font_size(0.0f), 0.0f, 0.001f);
    EXPECT_NEAR(ui_row_base_font_size(-10.0f), 0.0f, 0.001f);
    EXPECT_NEAR(ui_row_base_font_size(NAN), 0.0f, 0.001f);
    EXPECT_NEAR(ui_row_base_font_size(INFINITY), 0.0f, 0.001f);
}

TEST(ui_row_font_size_keeps_base_when_every_label_fits)
{
    Row_Measurement measurement = {18.72f, 0.5f, false};
    const char *labels[] = {"Save", "Load", "Tune"};
    float widths[] = {200.0f, 200.0f, 200.0f};
    float size = ui_row_font_size(labels, widths, 3, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                  row_measure, &measurement);
    EXPECT_NEAR(size, 18.72f, 0.001f);
}

TEST(ui_row_font_size_shrinks_the_whole_row_to_its_longest_label)
{
    // The reported defect: a track action row of equal 72px cells where only
    // "Open project" overflows. Every cell must end up at one size.
    Row_Measurement measurement = {18.72f, 0.4f, false};
    const char *labels[] = {"Open project", "Save", "Save As", "Add"};
    float widths[] = {72.0f, 72.0f, 72.0f, 72.0f};
    float size = ui_row_font_size(labels, widths, 4, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                  row_measure, &measurement);

    EXPECT_TRUE(size < 18.72f);
    EXPECT_TRUE(size > UI_ROW_MIN_FONT_SIZE);
    // The binding label fits exactly, so nothing narrower had to shrink further.
    float available = 72.0f - UI_ROW_LABEL_PADDING;
    EXPECT_NEAR((float)row_codepoint_count("Open project")*0.4f*size, available, 0.01f);
}

TEST(ui_row_font_size_stops_at_the_readable_floor)
{
    Row_Measurement measurement = {18.72f, 0.5f, false};
    const char *labels[] = {"An unreasonably long control label", "Save"};
    float widths[] = {72.0f, 72.0f};
    float size = ui_row_font_size(labels, widths, 2, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                  row_measure, &measurement);
    EXPECT_NEAR(size, UI_ROW_MIN_FONT_SIZE, 0.001f);
}

TEST(ui_row_font_size_floor_never_enlarges_a_small_box)
{
    // A 20px compact button starts below the floor; the floor must not grow it.
    Row_Measurement measurement = {10.4f, 0.5f, false};
    const char *labels[] = {"Way too long for this cell"};
    float widths[] = {40.0f};
    float size = ui_row_font_size(labels, widths, 1, 10.4f, UI_ROW_MIN_FONT_SIZE,
                                  row_measure, &measurement);
    EXPECT_NEAR(size, 10.4f, 0.001f);
}

TEST(ui_row_font_size_ignores_unmeasurable_and_absent_labels)
{
    Row_Measurement measurement = {18.72f, 0.5f, false};
    const char *labels[] = {NULL, "", "Save"};
    float widths[] = {72.0f, 72.0f, 72.0f};
    EXPECT_NEAR(ui_row_font_size(labels, widths, 3, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement),
                18.72f, 0.001f);

    // A measurement that cannot be trusted must not collapse the row.
    Row_Measurement broken = {18.72f, 0.5f, true};
    const char *only[] = {"Open project"};
    float only_widths[] = {72.0f};
    EXPECT_NEAR(ui_row_font_size(only, only_widths, 1, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &broken),
                18.72f, 0.001f);

    // A non-finite width is skipped rather than poisoning the shared size.
    float bad_widths[] = {NAN};
    EXPECT_NEAR(ui_row_font_size(only, bad_widths, 1, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement),
                18.72f, 0.001f);
}

TEST(ui_row_font_size_rejects_degenerate_inputs)
{
    Row_Measurement measurement = {18.72f, 0.5f, false};
    const char *labels[] = {"Save"};
    float widths[] = {72.0f};

    EXPECT_NEAR(ui_row_font_size(labels, widths, 1, 0.0f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement), 0.0f, 0.001f);
    EXPECT_NEAR(ui_row_font_size(labels, widths, 1, NAN, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement), 0.0f, 0.001f);
    EXPECT_NEAR(ui_row_font_size(NULL, widths, 1, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement), 18.72f, 0.001f);
    EXPECT_NEAR(ui_row_font_size(labels, widths, 1, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 NULL, &measurement), 18.72f, 0.001f);
    EXPECT_NEAR(ui_row_font_size(labels, widths, 0, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement), 18.72f, 0.001f);

    // A cell narrower than its own padding has no size that fits.
    float airless[] = {UI_ROW_LABEL_PADDING};
    EXPECT_NEAR(ui_row_font_size(labels, airless, 1, 18.72f, UI_ROW_MIN_FONT_SIZE,
                                 row_measure, &measurement),
                UI_ROW_MIN_FONT_SIZE, 0.001f);
}

TEST(ui_row_truncate_label_leaves_a_fitting_label_alone)
{
    Row_Measurement measurement = {12.0f, 0.5f, false};
    char output[UI_ROW_LABEL_CAPACITY];
    EXPECT_FALSE(ui_row_truncate_label("Save", 100.0f, row_measure, &measurement,
                                       output, sizeof(output)));
    EXPECT_TRUE(strcmp(output, "Save") == 0);
}

TEST(ui_row_truncate_label_ellipsizes_to_the_available_width)
{
    Row_Measurement measurement = {12.0f, 0.5f, false};
    char output[UI_ROW_LABEL_CAPACITY];
    // Six codepoints of room at 6px each.
    EXPECT_TRUE(ui_row_truncate_label("Spectral Terrarium", 36.0f, row_measure,
                                      &measurement, output, sizeof(output)));
    EXPECT_TRUE(row_measure(output, &measurement) <= 36.0f);
    EXPECT_TRUE(strcmp(output, "Spect\xE2\x80\xA6") == 0);
}

TEST(ui_row_truncate_label_cuts_only_at_utf8_boundaries)
{
    Row_Measurement measurement = {12.0f, 0.5f, false};
    char output[UI_ROW_LABEL_CAPACITY];
    // Three-byte codepoints; a naive byte cut would leave a continuation byte.
    const char *label = "\xE6\xBC\xA2\xE5\xAD\x97\xE6\xBC\xA2\xE5\xAD\x97";
    for (float width = 3.0f; width <= 30.0f; width += 3.0f) {
        bool shortened = ui_row_truncate_label(label, width, row_measure,
                                               &measurement, output, sizeof(output));
        EXPECT_TRUE(row_is_valid_utf8(output));
        // Four codepoints at 6px each: everything below 24px has to give.
        EXPECT_TRUE(shortened == (width < 24.0f));
        // The ellipsis alone is 6px, so only the narrowest boxes overhang.
        if (width >= 6.0f) EXPECT_TRUE(row_measure(output, &measurement) <= width);
    }
}

TEST(ui_row_truncate_label_respects_the_output_buffer)
{
    Row_Measurement measurement = {12.0f, 0.5f, false};
    char output[8];
    EXPECT_TRUE(ui_row_truncate_label("Spectral Terrarium", 1000.0f, row_measure,
                                      &measurement, output, sizeof(output)));
    EXPECT_TRUE(strlen(output) < sizeof(output));
    EXPECT_TRUE(strcmp(output, "Spec\xE2\x80\xA6") == 0);

    // Too small even for the ellipsis: an empty, terminated result, never a
    // partially written buffer.
    char tiny[3] = {'x', 'x', 'x'};
    EXPECT_TRUE(ui_row_truncate_label("Save", 1000.0f, row_measure, &measurement,
                                      tiny, sizeof(tiny)));
    EXPECT_TRUE(tiny[0] == '\0');
    EXPECT_FALSE(ui_row_truncate_label("Save", 1000.0f, row_measure, &measurement,
                                       NULL, 0));
}

TEST(ui_row_truncate_label_marks_a_box_with_no_room)
{
    Row_Measurement measurement = {12.0f, 0.5f, false};
    char output[UI_ROW_LABEL_CAPACITY];

    EXPECT_TRUE(ui_row_truncate_label("Save", 0.0f, row_measure, &measurement,
                                      output, sizeof(output)));
    EXPECT_TRUE(strcmp(output, "\xE2\x80\xA6") == 0);

    EXPECT_TRUE(ui_row_truncate_label("Save", NAN, row_measure, &measurement,
                                      output, sizeof(output)));
    EXPECT_TRUE(strcmp(output, "\xE2\x80\xA6") == 0);

    Row_Measurement broken = {12.0f, 0.5f, true};
    EXPECT_TRUE(ui_row_truncate_label("Save", 100.0f, row_measure, &broken,
                                      output, sizeof(output)));
    EXPECT_TRUE(strcmp(output, "\xE2\x80\xA6") == 0);

    EXPECT_FALSE(ui_row_truncate_label(NULL, 100.0f, row_measure, &measurement,
                                       output, sizeof(output)));
    EXPECT_TRUE(output[0] == '\0');
    EXPECT_FALSE(ui_row_truncate_label("", 100.0f, row_measure, &measurement,
                                       output, sizeof(output)));
}
