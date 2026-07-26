#include "scene_cadence_timing.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

// Reproduces the reported case: a long line whose final word is short. The
// windows are proportional to glyph count, so a short last word gets a narrow
// slice at the very end of the cue -- exactly where the line's dissolve lives.
#define LONG_LINE_WORDS 12u

static void long_line_windows(float *starts, float *ends)
{
    // Eleven ordinary words and a two-letter closer.
    const unsigned glyphs[LONG_LINE_WORDS] =
        {5, 4, 7, 6, 5, 3, 8, 4, 6, 5, 7, 2};
    REQUIRE_TRUE(cadence_timing_assign_windows(glyphs, LONG_LINE_WORDS,
                                               starts, ends));
}

TEST(cadence_timing_windows_tile_the_cue_and_close_at_its_end)
{
    float starts[LONG_LINE_WORDS];
    float ends[LONG_LINE_WORDS];
    long_line_windows(starts, ends);

    EXPECT_NEAR(starts[0], 0.0f, 0.0001f);
    EXPECT_NEAR(ends[LONG_LINE_WORDS - 1], 1.0f, 0.0f);
    for (size_t i = 0; i < LONG_LINE_WORDS; ++i) {
        EXPECT_TRUE(ends[i] > starts[i]);
        if (i > 0) EXPECT_NEAR(starts[i], ends[i - 1], 0.0001f);
    }

    // Longer words get longer moments; that is the whole point of the weighting.
    EXPECT_TRUE(ends[6] - starts[6] > ends[11] - starts[11]);
}

TEST(cadence_timing_line_hold_dissolves_only_at_the_end)
{
    // Full hold for the bulk of the cue.
    EXPECT_NEAR(cadence_timing_line_hold(0.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(cadence_timing_line_hold(0.5f), 1.0f, 0.0001f);
    // The dissolve begins with 1/9 of the cue left.
    EXPECT_NEAR(cadence_timing_line_hold(1.0f - 1.0f/CADENCE_LINE_DISSOLVE_SPAN),
                1.0f, 0.0001f);
    EXPECT_TRUE(cadence_timing_line_hold(0.95f) < 1.0f);
    EXPECT_NEAR(cadence_timing_line_hold(1.0f), 0.0f, 0.0001f);
    // Crosses the legibility threshold at 17/18, which is what used to make the
    // final word stop being drawn as settled type.
    EXPECT_TRUE(cadence_timing_line_hold(17.0f/18.0f - 0.001f) > CADENCE_HOLD_LEGIBLE);
    EXPECT_TRUE(cadence_timing_line_hold(17.0f/18.0f + 0.001f) < CADENCE_HOLD_LEGIBLE);
    // Degenerate inputs do not produce a NaN hold that would poison focus.
    EXPECT_NEAR(cadence_timing_line_hold(NAN), 0.0f, 0.0f);
    EXPECT_NEAR(cadence_timing_line_hold(-5.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(cadence_timing_line_hold(5.0f), 0.0f, 0.0f);
}

TEST(cadence_timing_keeps_the_final_word_legible_through_its_own_window)
{
    float starts[LONG_LINE_WORDS];
    float ends[LONG_LINE_WORDS];
    long_line_windows(starts, ends);

    const size_t last = LONG_LINE_WORDS - 1u;
    // The reachability condition from the report: this line's final window
    // opens after the dissolve has already started.
    EXPECT_TRUE(starts[last] > 1.0f - 1.0f/CADENCE_LINE_DISSOLVE_SPAN);

    // Across the whole of the final word's own window it stays fully held, so
    // its focus is never scaled down while it is being sung. Sampling the
    // interior rather than only the endpoints is the point: the old behaviour
    // failed in the middle, not at a boundary.
    for (int step = 0; step <= 20; ++step) {
        float position = starts[last] +
            (ends[last] - starts[last])*((float)step/20.0f)*0.999f;
        float line_hold = cadence_timing_line_hold(position);
        float word_hold = cadence_timing_word_hold(position, ends[last], line_hold);
        EXPECT_NEAR(word_hold, 1.0f, 0.0f);
        EXPECT_TRUE(word_hold > CADENCE_HOLD_LEGIBLE);
        // Demonstrates that the line hold alone would have failed here.
        if (position > 17.0f/18.0f) {
            EXPECT_TRUE(line_hold < CADENCE_HOLD_LEGIBLE);
        }
    }
}

TEST(cadence_timing_still_dissolves_words_that_are_already_sung)
{
    float starts[LONG_LINE_WORDS];
    float ends[LONG_LINE_WORDS];
    long_line_windows(starts, ends);

    // The exit is not cancelled: an earlier word, finished long before the end,
    // follows the line's dissolve exactly as it always did.
    float position = 0.99f;
    float line_hold = cadence_timing_line_hold(position);
    EXPECT_TRUE(line_hold < CADENCE_HOLD_LEGIBLE);
    for (size_t i = 0; i < LONG_LINE_WORDS - 1u; ++i) {
        if (ends[i] <= position) {
            EXPECT_NEAR(cadence_timing_word_hold(position, ends[i], line_hold),
                        line_hold, 0.0f);
        }
    }

    // And a word whose window has not opened yet is likewise exempt, so nothing
    // is dissolved before it has had its moment.
    EXPECT_NEAR(cadence_timing_word_hold(0.10f, ends[LONG_LINE_WORDS - 1u],
                                         cadence_timing_line_hold(0.10f)),
                1.0f, 0.0f);
}

TEST(cadence_timing_assign_windows_rejects_unusable_input)
{
    float starts[4];
    float ends[4];
    const unsigned glyphs[4] = {1, 2, 3, 4};
    EXPECT_FALSE(cadence_timing_assign_windows(NULL, 4, starts, ends));
    EXPECT_FALSE(cadence_timing_assign_windows(glyphs, 0, starts, ends));
    EXPECT_FALSE(cadence_timing_assign_windows(glyphs, 4, NULL, ends));
    EXPECT_FALSE(cadence_timing_assign_windows(glyphs, 4, starts, NULL));

    // A single word owns the entire cue.
    const unsigned one[1] = {3};
    REQUIRE_TRUE(cadence_timing_assign_windows(one, 1, starts, ends));
    EXPECT_NEAR(starts[0], 0.0f, 0.0f);
    EXPECT_NEAR(ends[0], 1.0f, 0.0f);

    // Zero-glyph words still get a slice from the +1 breath weight rather than
    // collapsing the division.
    const unsigned empty[2] = {0, 0};
    REQUIRE_TRUE(cadence_timing_assign_windows(empty, 2, starts, ends));
    EXPECT_NEAR(ends[0], 0.5f, 0.0001f);
    EXPECT_NEAR(ends[1], 1.0f, 0.0f);
}
