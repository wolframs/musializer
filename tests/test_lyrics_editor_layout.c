#include "lyrics_editor_layout.h"
#include "test_support.h"

#include <math.h>

// The panel is drawn inside a timeline of LYRIC_EDITOR_TIMELINE_CHROME + panel,
// which is anchored to the bottom of the window. This mirrors the application's
// own arithmetic so the assertions below are about real screen positions.
static float panel_bottom_overflow(float screen_height, size_t cue_count)
{
    float panel = lyric_editor_panel_height(screen_height, cue_count);
    // Where the form's last control ends, measured from the panel top.
    float form_extent = LYRIC_EDITOR_FORM_MINIMUM;
    return form_extent - panel;
}

TEST(lyric_editor_form_never_extends_past_its_panel)
{
    // This is the assertion that fails against the shipped 172 px panel at every
    // supported window size: Apply, Discard and Delete were drawn below it.
    const float heights[] = {640.0f, 720.0f, 800.0f, 900.0f, 1080.0f, 1440.0f, 2160.0f};
    const size_t counts[] = {0, 1, 3, 4, 8, 12, 64, 1024};
    for (size_t h = 0; h < sizeof(heights)/sizeof(heights[0]); ++h) {
        for (size_t c = 0; c < sizeof(counts)/sizeof(counts[0]); ++c) {
            float panel = lyric_editor_panel_height(heights[h], counts[c]);
            EXPECT_TRUE(lyric_editor_form_fits(panel));
            EXPECT_TRUE(panel_bottom_overflow(heights[h], counts[c]) <= 0.0f);
        }
    }
}

TEST(lyric_editor_panel_grows_with_the_window_instead_of_staying_fixed)
{
    // Eight cues, the fixture's count. The shipped panel showed three rows at
    // every size; taller windows must now show more.
    size_t at_640 = lyric_editor_visible_rows(lyric_editor_panel_height(640.0f, 8));
    size_t at_720 = lyric_editor_visible_rows(lyric_editor_panel_height(720.0f, 8));
    size_t at_1080 = lyric_editor_visible_rows(lyric_editor_panel_height(1080.0f, 8));

    EXPECT_TRUE(at_640 >= 4);
    EXPECT_TRUE(at_720 > at_640);
    EXPECT_TRUE(at_1080 > at_720);
    // A full-HD window shows the whole fixture rather than a third of it.
    EXPECT_EQ_SIZE(at_1080, 8);
}

TEST(lyric_editor_panel_protects_the_sidebar_where_it_can)
{
    // At 720p and above the sidebar keeps its scene browser floor and a tracks
    // panel that can host its action row.
    for (float height = 720.0f; height <= 2160.0f; height += 1.0f) {
        float panel = lyric_editor_panel_height(height, 8);
        float sidebar = height - (panel + LYRIC_EDITOR_TIMELINE_CHROME);
        EXPECT_TRUE(sidebar >= LYRIC_EDITOR_SIDEBAR_MINIMUM - 0.01f);
    }
    // At the 640 minimum the form wins and the sidebar yields; the tracks panel
    // collapses cleanly instead of overflowing. See workspace_sidebar_layout.
    float panel = lyric_editor_panel_height(640.0f, 8);
    EXPECT_NEAR(panel, LYRIC_EDITOR_FORM_MINIMUM, 0.001f);
}

TEST(lyric_editor_required_height_clamps_the_row_count)
{
    // Fewer than four cues still needs a panel that can host the form.
    EXPECT_NEAR(lyric_editor_required_height(0), LYRIC_EDITOR_FORM_MINIMUM, 0.001f);
    EXPECT_NEAR(lyric_editor_required_height(1), LYRIC_EDITOR_FORM_MINIMUM, 0.001f);
    EXPECT_NEAR(lyric_editor_required_height(4), LYRIC_EDITOR_FORM_MINIMUM, 0.001f);

    float eight = lyric_editor_required_height(8);
    EXPECT_NEAR(eight, LYRIC_EDITOR_LIST_CHROME + 8.0f*LYRIC_EDITOR_ROW_HEIGHT, 0.001f);

    // A thousand cues must not ask for a thousand rows of panel.
    EXPECT_NEAR(lyric_editor_required_height(1024),
                lyric_editor_required_height(LYRIC_EDITOR_MAX_ROWS), 0.001f);
}

TEST(lyric_editor_visible_rows_agrees_with_the_panel_it_is_given)
{
    // Round trip: a panel sized for N rows must actually show N.
    for (size_t rows = LYRIC_EDITOR_MIN_ROWS; rows <= LYRIC_EDITOR_MAX_ROWS; ++rows) {
        float panel = LYRIC_EDITOR_LIST_CHROME + (float)rows*LYRIC_EDITOR_ROW_HEIGHT;
        EXPECT_EQ_SIZE(lyric_editor_visible_rows(panel), rows);
    }
    EXPECT_EQ_SIZE(lyric_editor_visible_rows(0.0f), 0);
    EXPECT_EQ_SIZE(lyric_editor_visible_rows(-100.0f), 0);
    EXPECT_EQ_SIZE(lyric_editor_visible_rows(NAN), 0);
}

TEST(lyric_editor_layout_rejects_degenerate_windows)
{
    EXPECT_NEAR(lyric_editor_panel_height(NAN, 8), LYRIC_EDITOR_FORM_MINIMUM, 0.001f);
    EXPECT_NEAR(lyric_editor_panel_height(0.0f, 8), LYRIC_EDITOR_FORM_MINIMUM, 0.001f);
    EXPECT_NEAR(lyric_editor_panel_height(-720.0f, 8), LYRIC_EDITOR_FORM_MINIMUM, 0.001f);
    EXPECT_FALSE(lyric_editor_form_fits(NAN));
    EXPECT_FALSE(lyric_editor_form_fits(172.0f));  // The shipped height.
}
