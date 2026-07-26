#include "workspace_layout.h"
#include "test_support.h"

#include <math.h>

static Ui_Rect rect(float x, float y, float width, float height)
{
    Ui_Rect r = {x, y, width, height};
    return r;
}

TEST(ui_rect_containment_is_inclusive_and_rejects_degenerate_input)
{
    Ui_Rect outer = rect(0.0f, 0.0f, 100.0f, 100.0f);
    EXPECT_TRUE(ui_rect_contains(outer, rect(10.0f, 10.0f, 50.0f, 50.0f)));
    // Flush with every edge still counts.
    EXPECT_TRUE(ui_rect_contains(outer, rect(0.0f, 0.0f, 100.0f, 100.0f)));
    EXPECT_FALSE(ui_rect_contains(outer, rect(-1.0f, 0.0f, 10.0f, 10.0f)));
    EXPECT_FALSE(ui_rect_contains(outer, rect(0.0f, 95.0f, 10.0f, 10.0f)));
    // The defect this module exists for: a control below a zero-height panel.
    EXPECT_FALSE(ui_rect_contains(rect(0.0f, 0.0f, 240.0f, 0.0f),
                                  rect(10.0f, 50.0f, 52.0f, 36.0f)));
    EXPECT_FALSE(ui_rect_contains(outer, rect(10.0f, 10.0f, 0.0f, 10.0f)));
    EXPECT_FALSE(ui_rect_contains(outer, rect(NAN, 10.0f, 10.0f, 10.0f)));
    EXPECT_FALSE(ui_rect_contains(rect(0.0f, 0.0f, INFINITY, 10.0f),
                                  rect(1.0f, 1.0f, 2.0f, 2.0f)));
}

TEST(ui_rect_overlap_excludes_touching_edges)
{
    EXPECT_TRUE(ui_rect_overlaps(rect(0.0f, 0.0f, 10.0f, 10.0f),
                                 rect(5.0f, 5.0f, 10.0f, 10.0f)));
    // Stacked panels share an edge and must not count as overlapping.
    EXPECT_FALSE(ui_rect_overlaps(rect(0.0f, 0.0f, 10.0f, 10.0f),
                                  rect(0.0f, 10.0f, 10.0f, 10.0f)));
    EXPECT_FALSE(ui_rect_overlaps(rect(0.0f, 0.0f, 10.0f, 0.0f),
                                  rect(0.0f, 0.0f, 10.0f, 10.0f)));
}

TEST(ui_rect_intersect_clamps_to_the_shared_area)
{
    Ui_Rect got = ui_rect_intersect(rect(0.0f, 0.0f, 10.0f, 10.0f),
                                    rect(5.0f, 5.0f, 10.0f, 10.0f));
    EXPECT_NEAR(got.x, 5.0f, 0.001f);
    EXPECT_NEAR(got.y, 5.0f, 0.001f);
    EXPECT_NEAR(got.width, 5.0f, 0.001f);
    EXPECT_NEAR(got.height, 5.0f, 0.001f);

    Ui_Rect none = ui_rect_intersect(rect(0.0f, 0.0f, 10.0f, 10.0f),
                                     rect(50.0f, 50.0f, 10.0f, 10.0f));
    EXPECT_TRUE(ui_rect_is_empty(none));
}

TEST(workspace_sidebar_layout_rejects_degenerate_input)
{
    Ui_Rect sentinel = rect(1.0f, 2.0f, 3.0f, 4.0f);
    Workspace_Sidebar layout = {.tracks = sentinel, .scenes = sentinel,
                                .tracks_mode = TRACKS_PANEL_STACKED};
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, NAN, 1, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, INFINITY, 1, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, 0.0f, 1, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, -10.0f, 1, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(0.0f, 600.0f, 1, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, 600.0f, 1, NULL));
    // Rejection must leave the caller's layout untouched.
    EXPECT_NEAR(layout.tracks.width, 3.0f, 0.001f);
}

// The states measured from the real application at the 960x640 minimum window.
// content_y is 146 and assist_timeline_height caps at screen - toolbar - 150.
TEST(workspace_sidebar_layout_hides_the_tracks_panel_when_it_cannot_host_its_row)
{
    Workspace_Sidebar layout;

    // Assist with a staged candidate: the sidebar budget is exactly zero after
    // the scene browser is served. This is the state that stole scene clicks.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 208.0f, 1, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_HIDDEN);
    EXPECT_NEAR(layout.tracks.height, 0.0f, 0.001f);
    EXPECT_NEAR(layout.scenes.height, 208.0f, 0.001f);

    // Assist confirmation, reachable with two clicks and no model at all.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 242.0f, 1, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_HIDDEN);
    EXPECT_NEAR(layout.tracks.height, 0.0f, 0.001f);

    // Assist ready: 89 px of tracks panel, which does contain its 86 px action
    // row. This one works today and must keep working.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 304.0f, 1, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_SINGLE);
    EXPECT_TRUE(layout.tracks.height >= WORKSPACE_TRACKS_SINGLE_MINIMUM);

    // Lyrics or export panel open at 640: unchanged single-row behaviour.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 310.0f, 1, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_SINGLE);

    // No bottom panel at 720p: the roomy case keeps two stacked rows. The
    // tracks panel takes what one 64 px row plus its chrome needs (188) rather
    // than the old fixed 168, and the browser takes the rest instead of being
    // pinned at its cap with the surplus left as whitespace under the list.
    REQUIRE_TRUE(workspace_sidebar_layout(320.0f, 460.0f, 1, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_STACKED);
    EXPECT_NEAR(layout.tracks.height, 188.0f, 0.001f);
    EXPECT_NEAR(layout.scenes.height, 272.0f, 0.001f);
}

TEST(workspace_sidebar_layout_spends_surplus_on_the_scene_browser)
{
    // The inversion this module was changed for. With a short track list, extra
    // sidebar height used to land in the gap below the tracks because the
    // browser was already at its cap; it now reaches the scene grid until the
    // browser is full, and only then goes to the tracks panel.
    Workspace_Sidebar small;
    Workspace_Sidebar large;
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 460.0f, 1, &small));
    // 500 is below the point where the browser hits its cap (172 + 355 = 527);
    // past that the surplus correctly goes to the tracks panel instead.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 500.0f, 1, &large));
    EXPECT_TRUE(large.scenes.height > small.scenes.height);
    EXPECT_NEAR(large.tracks.height, small.tracks.height, 0.001f);

    // Past the browser's cap the tracks panel gets the remainder, so nothing is
    // lost -- the priority simply reversed.
    Workspace_Sidebar huge;
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 900.0f, 1, &huge));
    EXPECT_NEAR(huge.scenes.height, WORKSPACE_SCENES_MAXIMUM, 0.001f);
    EXPECT_TRUE(huge.tracks.height > large.tracks.height);

    // A longer list asks for more and gets it, at the browser's expense down to
    // its floor but never below.
    Workspace_Sidebar many;
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 500.0f, 8, &many));
    EXPECT_TRUE(many.tracks.height > large.tracks.height);
    EXPECT_TRUE(many.scenes.height >= WORKSPACE_SCENES_MINIMUM);

    // And an absurd list cannot starve the browser below its floor.
    Workspace_Sidebar absurd;
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 500.0f, 4096, &absurd));
    EXPECT_TRUE(absurd.scenes.height >= WORKSPACE_SCENES_MINIMUM);
    EXPECT_NEAR(absurd.tracks.height + absurd.scenes.height, 500.0f, 0.01f);
}

TEST(workspace_sidebar_layout_tiles_the_budget_and_contains_every_action_row)
{
    for (float height = 1.0f; height <= 1400.0f; height += 1.0f) {
      // Track counts spanning empty, typical, and far past what fits.
      const size_t counts[] = {0, 1, 3, 12, 512};
      for (size_t c = 0; c < sizeof(counts)/sizeof(counts[0]); ++c) {
        Workspace_Sidebar layout;
        REQUIRE_TRUE(workspace_sidebar_layout(240.0f, height, counts[c], &layout));

        // The two panels tile the sidebar exactly: no gap, no overlap.
        EXPECT_NEAR(layout.tracks.height + layout.scenes.height, height, 0.01f);
        EXPECT_NEAR(layout.scenes.y, layout.tracks.height, 0.001f);
        EXPECT_FALSE(ui_rect_overlaps(layout.tracks, layout.scenes));
        EXPECT_TRUE(layout.tracks.height >= 0.0f);
        EXPECT_TRUE(layout.scenes.height >= 0.0f);

        if (layout.tracks_mode == TRACKS_PANEL_HIDDEN) {
            EXPECT_NEAR(layout.tracks.height, 0.0f, 0.001f);
            continue;
        }

        // The invariant the click hijack violated: the panel contains the row it
        // draws, so no action button can register a hit box over the scene grid.
        float top = 0.0f;
        float row_height = 0.0f;
        REQUIRE_TRUE(workspace_tracks_action_row(layout.tracks_mode, &top, &row_height));
        Ui_Rect row = {layout.tracks.x + 10.0f, layout.tracks.y + top,
                       layout.tracks.width - 20.0f, row_height};
        EXPECT_TRUE(ui_rect_contains(layout.tracks, row));
        EXPECT_FALSE(ui_rect_overlaps(row, layout.scenes));

        if (layout.tracks_mode == TRACKS_PANEL_STACKED) {
            EXPECT_TRUE(layout.tracks.height >= WORKSPACE_TRACKS_STACKED_MINIMUM);
        } else {
            EXPECT_TRUE(layout.tracks.height >= WORKSPACE_TRACKS_SINGLE_MINIMUM);
        }
      }
    }
}

TEST(workspace_sidebar_layout_serves_the_scene_browser_floor_when_it_can)
{
    for (float height = 215.0f; height <= 1400.0f; height += 1.0f) {
        Workspace_Sidebar layout;
        REQUIRE_TRUE(workspace_sidebar_layout(240.0f, height, 1, &layout));
        EXPECT_TRUE(layout.scenes.height >= WORKSPACE_SCENES_MINIMUM);
        EXPECT_TRUE(layout.scenes.height <= WORKSPACE_SCENES_MAXIMUM ||
                    layout.tracks_mode == TRACKS_PANEL_HIDDEN);
    }
    // Below the floor the browser simply takes everything there is.
    Workspace_Sidebar tiny;
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 120.0f, 1, &tiny));
    EXPECT_TRUE(tiny.tracks_mode == TRACKS_PANEL_HIDDEN);
    EXPECT_NEAR(tiny.scenes.height, 120.0f, 0.001f);
}

TEST(workspace_tracks_action_row_has_no_row_when_hidden)
{
    float top = -1.0f;
    float height = -1.0f;
    EXPECT_FALSE(workspace_tracks_action_row(TRACKS_PANEL_HIDDEN, &top, &height));
    EXPECT_TRUE(workspace_tracks_action_row(TRACKS_PANEL_SINGLE, &top, &height));
    EXPECT_NEAR(top + height, WORKSPACE_TRACKS_SINGLE_MINIMUM, 0.001f);
    EXPECT_FALSE(workspace_tracks_action_row(TRACKS_PANEL_SINGLE, NULL, &height));
}
