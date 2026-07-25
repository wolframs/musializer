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
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, NAN, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, INFINITY, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, 0.0f, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, -10.0f, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(0.0f, 600.0f, &layout));
    EXPECT_FALSE(workspace_sidebar_layout(240.0f, 600.0f, NULL));
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
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 208.0f, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_HIDDEN);
    EXPECT_NEAR(layout.tracks.height, 0.0f, 0.001f);
    EXPECT_NEAR(layout.scenes.height, 208.0f, 0.001f);

    // Assist confirmation, reachable with two clicks and no model at all.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 242.0f, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_HIDDEN);
    EXPECT_NEAR(layout.tracks.height, 0.0f, 0.001f);

    // Assist ready: 89 px of tracks panel, which does contain its 86 px action
    // row. This one works today and must keep working.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 304.0f, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_SINGLE);
    EXPECT_TRUE(layout.tracks.height >= WORKSPACE_TRACKS_SINGLE_MINIMUM);

    // Lyrics or export panel open at 640: unchanged single-row behaviour.
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 310.0f, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_SINGLE);

    // No bottom panel at 720p: the roomy case keeps two stacked rows.
    REQUIRE_TRUE(workspace_sidebar_layout(320.0f, 460.0f, &layout));
    EXPECT_TRUE(layout.tracks_mode == TRACKS_PANEL_STACKED);
    EXPECT_NEAR(layout.scenes.height, WORKSPACE_SCENES_MAXIMUM, 0.001f);
    EXPECT_NEAR(layout.tracks.height, 168.0f, 0.001f);
}

TEST(workspace_sidebar_layout_tiles_the_budget_and_contains_every_action_row)
{
    for (float height = 1.0f; height <= 1400.0f; height += 1.0f) {
        Workspace_Sidebar layout;
        REQUIRE_TRUE(workspace_sidebar_layout(240.0f, height, &layout));

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

TEST(workspace_sidebar_layout_serves_the_scene_browser_floor_when_it_can)
{
    for (float height = 215.0f; height <= 1400.0f; height += 1.0f) {
        Workspace_Sidebar layout;
        REQUIRE_TRUE(workspace_sidebar_layout(240.0f, height, &layout));
        EXPECT_TRUE(layout.scenes.height >= WORKSPACE_SCENES_MINIMUM);
        EXPECT_TRUE(layout.scenes.height <= WORKSPACE_SCENES_MAXIMUM ||
                    layout.tracks_mode == TRACKS_PANEL_HIDDEN);
    }
    // Below the floor the browser simply takes everything there is.
    Workspace_Sidebar tiny;
    REQUIRE_TRUE(workspace_sidebar_layout(240.0f, 120.0f, &tiny));
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
