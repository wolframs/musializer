#include "timeline_layout.h"
#include "test_support.h"

#include <math.h>

// The shipped control row: six panel/event buttons then "Clear manual".
static const float SHIPPED_CONTROLS[6] = {74.0f, 74.0f, 90.0f, 74.0f, 82.0f, 86.0f};
#define SHIPPED_CLEAR 112.0f
#define SHIPPED_MARGIN 6.0f
#define SHIPPED_HEIGHT 38.0f
// "0:00 / 0:40" at 22 px. A long track ("1:23:45 / 1:23:45") measures wider,
// which is why the collision was first noticed on long fixtures.
#define SHIPPED_TIMECODE 145.0f

static bool band(float width, float timecode_width, Timeline_Band *out)
{
    return timeline_band_layout(0.0f, 0.0f, width, SHIPPED_HEIGHT, SHIPPED_MARGIN,
                                SHIPPED_CONTROLS, 6, SHIPPED_CLEAR,
                                timecode_width, out);
}

TEST(timeline_band_keeps_the_natural_row_when_the_window_is_wide)
{
    Timeline_Band layout;
    REQUIRE_TRUE(band(1920.0f, SHIPPED_TIMECODE, &layout));
    EXPECT_NEAR(layout.scale, 1.0f, 0.0001f);
    EXPECT_TRUE(layout.timecode_inline);
    EXPECT_TRUE(layout.fits);
    // 480 px of buttons + 6 margins + the 112 px clear button.
    EXPECT_NEAR(layout.controls_width, 628.0f, 0.01f);
    // The declared parent extent is the real one: the clear button ends exactly
    // where the row says it does. This is the property whose absence let the
    // children run 36 px past controls.width.
    EXPECT_NEAR(layout.clear.x + layout.clear.width,
                layout.controls.x + layout.controls_width, 0.01f);
}

TEST(timeline_band_never_lets_the_timecode_touch_the_controls)
{
    // The reported defect, at the narrowest supported band: a 960 px window
    // with the inspector open leaves 680 px here.
    Timeline_Band layout;
    REQUIRE_TRUE(band(680.0f, SHIPPED_TIMECODE, &layout));
    if (layout.timecode_inline) {
        EXPECT_TRUE(layout.controls.x + layout.controls_width + TIMELINE_BAND_GAP <=
                    layout.timecode.x + 0.01f);
    }
    // Whatever it chose, the clear button no longer reaches the timecode.
    EXPECT_TRUE(layout.clear.x + layout.clear.width <= 680.0f - SHIPPED_MARGIN + 0.01f);
}

TEST(timeline_band_moves_the_timecode_before_the_labels_go_unreadable)
{
    Timeline_Band layout;
    // The narrowest supported band with a short timecode: the row gives up a
    // little and both still share the strip. 668 px of content less the 145 px
    // timecode and the 12 px gap leaves 511 for a 628 px row.
    REQUIRE_TRUE(band(680.0f, SHIPPED_TIMECODE, &layout));
    EXPECT_TRUE(layout.timecode_inline);
    EXPECT_NEAR(layout.scale, 511.0f/628.0f, 0.001f);
    EXPECT_TRUE(layout.scale >= TIMELINE_BAND_MIN_SCALE);

    // Same band, but an hour-long track widens the timecode to 232 px. Now
    // seating both would need scale 0.675, past the readability floor, so the
    // timecode relocates rather than the buttons becoming illegible -- and the
    // row goes back to full size because it no longer shares the strip.
    REQUIRE_TRUE(band(680.0f, 232.0f, &layout));
    EXPECT_FALSE(layout.timecode_inline);
    EXPECT_NEAR(layout.scale, 1.0f, 0.0001f);
    EXPECT_TRUE(layout.fits);
    EXPECT_EQ_SIZE((size_t)layout.timecode.width, 0);
}

TEST(timeline_band_reports_running_out_of_room_instead_of_hiding_it)
{
    Timeline_Band layout;
    // 628 px of natural row cannot fit 400 px even at the minimum scale.
    REQUIRE_TRUE(band(400.0f, SHIPPED_TIMECODE, &layout));
    EXPECT_FALSE(layout.fits);
    EXPECT_NEAR(layout.scale, TIMELINE_BAND_MIN_SCALE, 0.0001f);
    // Clamped rather than driven to zero, so the caller still draws something
    // recognisable while `fits` tells it the truth.
    EXPECT_TRUE(layout.controls_width > 0.0f);
}

TEST(timeline_band_holds_its_invariants_across_every_supported_width)
{
    // 680 is the narrowest band a supported window can produce; sweep well past
    // 4K so a future wide-monitor default cannot reintroduce the overlap.
    for (float width = 680.0f; width <= 4000.0f; width += 1.0f) {
        for (int variant = 0; variant < 2; ++variant) {
            // Short and long timecodes; the long form is what made the defect
            // visible in the first place.
            float timecode_width = variant == 0 ? 145.0f : 232.0f;
            Timeline_Band layout;
            REQUIRE_TRUE(band(width, timecode_width, &layout));

            // Children stay inside the parent extent.
            EXPECT_TRUE(layout.clear.x + layout.clear.width <=
                        layout.controls.x + layout.controls_width + 0.01f);
            // The row stays inside the band.
            EXPECT_TRUE(layout.controls.x + layout.controls_width <=
                        width - SHIPPED_MARGIN + 0.01f);
            // The scale never leaves its declared range.
            EXPECT_TRUE(layout.scale >= TIMELINE_BAND_MIN_SCALE);
            EXPECT_TRUE(layout.scale <= 1.0f);

            if (layout.timecode_inline) {
                // The whole point: a gap always survives between the two groups.
                EXPECT_TRUE(layout.controls.x + layout.controls_width +
                            TIMELINE_BAND_GAP <= layout.timecode.x + 0.01f);
                // And the timecode itself stays on the band.
                EXPECT_TRUE(layout.timecode.x + layout.timecode.width <=
                            width - SHIPPED_MARGIN + 0.01f);
                EXPECT_TRUE(layout.timecode.x >= layout.controls.x);
            }
        }
    }
}

TEST(timeline_band_rejects_unusable_input_without_writing_a_result)
{
    Timeline_Band layout = {.scale = 12345.0f};
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, 6, SHIPPED_CONTROLS, 6,
                                      112, 145, NULL));
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, 6, NULL, 6, 112, 145, &layout));
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, 6, SHIPPED_CONTROLS, 0,
                                      112, 145, &layout));
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, 6, SHIPPED_CONTROLS,
                                      TIMELINE_BAND_CONTROL_CAPACITY + 1,
                                      112, 145, &layout));
    EXPECT_FALSE(timeline_band_layout(0, 0, 0, 38, 6, SHIPPED_CONTROLS, 6,
                                      112, 145, &layout));
    EXPECT_FALSE(timeline_band_layout(0, 0, NAN, 38, 6, SHIPPED_CONTROLS, 6,
                                      112, 145, &layout));
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, 6, SHIPPED_CONTROLS, 6,
                                      112, INFINITY, &layout));
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, -1, SHIPPED_CONTROLS, 6,
                                      112, 145, &layout));

    const float negative[6] = {74.0f, -1.0f, 90.0f, 74.0f, 82.0f, 86.0f};
    EXPECT_FALSE(timeline_band_layout(0, 0, 800, 38, 6, negative, 6, 112, 145, &layout));

    // A rejected call must not have written a partial layout.
    EXPECT_NEAR(layout.scale, 12345.0f, 0.0f);
}

TEST(timeline_band_honours_a_band_that_does_not_start_at_the_origin)
{
    Timeline_Band layout;
    REQUIRE_TRUE(timeline_band_layout(320.0f, 40.0f, 1200.0f, SHIPPED_HEIGHT,
                                      SHIPPED_MARGIN, SHIPPED_CONTROLS, 6,
                                      SHIPPED_CLEAR, SHIPPED_TIMECODE, &layout));
    EXPECT_NEAR(layout.controls.x, 326.0f, 0.01f);
    EXPECT_NEAR(layout.controls.y, 40.0f, 0.01f);
    EXPECT_TRUE(layout.timecode_inline);
    EXPECT_NEAR(layout.timecode.x + layout.timecode.width, 320.0f + 1200.0f - 6.0f, 0.01f);
    EXPECT_TRUE(layout.controls.x + layout.controls_width + TIMELINE_BAND_GAP <=
                layout.timecode.x + 0.01f);
}
