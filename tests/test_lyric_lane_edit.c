#include "lyric_lane_edit.h"
#include "test_support.h"

#include <math.h>
#include <stdio.h>

#define LANE_X 20.0
#define LANE_WIDTH 1000.0
#define TRACK 100.0

// Five one-second cues at 10 s intervals in a 100 s track. Unzoomed, the lane is
// 10 px per second, so each block is 10 px wide and the numbers below are easy
// to check: cue 1 spans x = 120 to 130.
static void build_lane(Lyrics_Document *document)
{
    REQUIRE_TRUE(lyrics_document_init(document, TRACK) == LYRICS_OK);
    for (int i = 0; i < 5; ++i) {
        Lyric_Cue cue = {
            .id = 0,
            .start_seconds = 10.0 + i*10.0,
            .end_seconds = 11.0 + i*10.0,
        };
        snprintf(cue.text, sizeof(cue.text), "line %d", i + 1);
        REQUIRE_TRUE(lyrics_insert(document, &cue, NULL) == LYRICS_OK);
    }
}

static Timeline_View whole_view(void)
{
    Timeline_View view;
    timeline_view_reset(&view, TRACK);
    return view;
}

TEST(lyric_lane_hit_test_finds_the_block_under_the_pointer)
{
    Lyrics_Document document;
    build_lane(&document);
    Timeline_View view = whole_view();

    Lyric_Lane_Hit hit = lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, 125.0);
    EXPECT_EQ_U64(hit.id, 1);
    EXPECT_TRUE(hit.zone == LYRIC_LANE_ZONE_BODY);

    // Between blocks, and outside the lane entirely.
    EXPECT_TRUE(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, 160.0).zone ==
                LYRIC_LANE_ZONE_NONE);
    EXPECT_TRUE(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, 5.0).zone ==
                LYRIC_LANE_ZONE_NONE);
    EXPECT_TRUE(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, 5000.0).zone ==
                LYRIC_LANE_ZONE_NONE);
}

TEST(lyric_lane_hit_test_withholds_edge_handles_from_a_block_too_small_to_aim_at)
{
    Lyrics_Document document;
    build_lane(&document);
    Timeline_View view = whole_view();
    // A 10 px block: every pixel of it is within the 5 px grab of one edge or
    // the other, so offering handles would make it impossible to move.
    for (double x = 120.0; x <= 130.0; x += 1.0) {
        Lyric_Lane_Hit hit = lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, x);
        EXPECT_EQ_U64(hit.id, 1);
        EXPECT_TRUE(hit.zone == LYRIC_LANE_ZONE_BODY);
    }
}

TEST(lyric_lane_hit_test_offers_edges_once_the_block_is_wide_enough)
{
    Lyrics_Document document;
    build_lane(&document);
    Timeline_View view = whole_view();
    // Zoom until cue 1 is comfortably wide. At 8x the span is 12.5 s, so the
    // lane is 80 px per second and the one-second block is 80 px.
    timeline_view_zoom(&view, TRACK, 8.0, 10.5);
    double left = timeline_view_x_at(&view, 10.0, LANE_X, LANE_WIDTH);
    double right = timeline_view_x_at(&view, 11.0, LANE_X, LANE_WIDTH);
    EXPECT_NEAR(right - left, 80.0, 0.5);

    EXPECT_TRUE(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH,
                                    left + 2.0).zone == LYRIC_LANE_ZONE_START_EDGE);
    EXPECT_TRUE(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH,
                                    right - 2.0).zone == LYRIC_LANE_ZONE_END_EDGE);
    EXPECT_TRUE(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH,
                                    (left + right)*0.5).zone == LYRIC_LANE_ZONE_BODY);
}

TEST(lyric_lane_hit_test_offers_no_handle_for_a_boundary_that_scrolled_away)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, TRACK) == LYRICS_OK);
    Lyric_Cue wide = {.id = 0, .start_seconds = 5.0, .end_seconds = 60.0};
    snprintf(wide.text, sizeof(wide.text), "%s", "a long held note");
    REQUIRE_TRUE(lyrics_insert(&document, &wide, NULL) == LYRICS_OK);

    Timeline_View view = whole_view();
    timeline_view_zoom(&view, TRACK, 10.0, 30.0);
    // The window is 10 s wide around 30 s, so both boundaries are off-screen.
    EXPECT_TRUE(view.start_seconds > 5.0);
    EXPECT_TRUE(view.start_seconds + view.span_seconds < 60.0);
    // The block fills the lane. Grabbing near the lane border must give the
    // body, not a handle for a boundary that is not being shown.
    Lyric_Lane_Hit at_left = lyric_lane_hit_test(&document, &view, LANE_X,
                                                 LANE_WIDTH, LANE_X + 1.0);
    Lyric_Lane_Hit at_right = lyric_lane_hit_test(&document, &view, LANE_X,
                                                  LANE_WIDTH, LANE_X + LANE_WIDTH - 1.0);
    EXPECT_EQ_U64(at_left.id, 1);
    EXPECT_TRUE(at_left.zone == LYRIC_LANE_ZONE_BODY);
    EXPECT_TRUE(at_right.zone == LYRIC_LANE_ZONE_BODY);
}

TEST(lyric_lane_hit_test_prefers_the_block_painted_on_top)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, TRACK) == LYRICS_OK);
    Lyric_Cue first = {.id = 0, .start_seconds = 10.0, .end_seconds = 30.0};
    Lyric_Cue second = {.id = 0, .start_seconds = 20.0, .end_seconds = 40.0};
    snprintf(first.text, sizeof(first.text), "%s", "under");
    snprintf(second.text, sizeof(second.text), "%s", "over");
    REQUIRE_TRUE(lyrics_insert(&document, &first, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&document, &second, NULL) == LYRICS_OK);
    Timeline_View view = whole_view();
    // x for 25 s, inside both. The later cue is drawn last, so it is the one
    // the eye sees and must be the one the press selects.
    double x = timeline_view_x_at(&view, 25.0, LANE_X, LANE_WIDTH);
    EXPECT_EQ_U64(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, x).id, 2);
    double only_first = timeline_view_x_at(&view, 15.0, LANE_X, LANE_WIDTH);
    EXPECT_EQ_U64(lyric_lane_hit_test(&document, &view, LANE_X, LANE_WIDTH, only_first).id, 1);
}

TEST(lyric_lane_selection_replace_toggle_and_extend)
{
    Lyrics_Document document;
    build_lane(&document);
    Lyric_Lane_Selection selection;
    lyric_lane_selection_clear(&selection);

    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 2,
                                            LYRIC_LANE_CLICK_REPLACE));
    EXPECT_EQ_SIZE(selection.count, 1);
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 2));

    // Ctrl adds without disturbing the rest.
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 4,
                                            LYRIC_LANE_CLICK_TOGGLE));
    EXPECT_EQ_SIZE(selection.count, 2);
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 2));
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 4));

    // Ctrl again removes exactly that one.
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 2,
                                            LYRIC_LANE_CLICK_TOGGLE));
    EXPECT_EQ_SIZE(selection.count, 1);
    EXPECT_FALSE(lyric_lane_selection_contains(&selection, 2));

    // Shift extends from the anchor, which the last ctrl+click left at cue 4.
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 1,
                                            LYRIC_LANE_CLICK_EXTEND));
    EXPECT_EQ_SIZE(selection.count, 4);
    for (uint64_t id = 1; id <= 4; ++id) {
        EXPECT_TRUE(lyric_lane_selection_contains(&selection, id));
    }
    // Shifting the other way from the same anchor replaces the range rather
    // than accumulating: the anchor did not move.
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 5,
                                            LYRIC_LANE_CLICK_EXTEND));
    EXPECT_EQ_SIZE(selection.count, 2);
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 4));
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 5));

    // A plain click collapses back to one.
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 3,
                                            LYRIC_LANE_CLICK_REPLACE));
    EXPECT_EQ_SIZE(selection.count, 1);
}

TEST(lyric_lane_selection_rejects_what_it_cannot_hold_or_find)
{
    Lyrics_Document document;
    build_lane(&document);
    Lyric_Lane_Selection selection;
    lyric_lane_selection_clear(&selection);

    EXPECT_FALSE(lyric_lane_selection_apply(&selection, &document, 99,
                                            LYRIC_LANE_CLICK_REPLACE));
    EXPECT_EQ_SIZE(selection.count, 0);
    EXPECT_FALSE(lyric_lane_selection_apply(&selection, &document, 0,
                                            LYRIC_LANE_CLICK_REPLACE));

    // Shift with no anchor behaves as a plain click instead of doing nothing.
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 3,
                                            LYRIC_LANE_CLICK_EXTEND));
    EXPECT_EQ_SIZE(selection.count, 1);
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 3));
}

TEST(lyric_lane_selection_range_beyond_capacity_leaves_the_selection_alone)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 1000.0) == LYRICS_OK);
    for (size_t i = 0; i < LYRIC_LANE_SELECTION_CAPACITY + 4u; ++i) {
        Lyric_Cue cue = {
            .id = 0,
            .start_seconds = (double)i*2.0,
            .end_seconds = (double)i*2.0 + 1.0,
        };
        snprintf(cue.text, sizeof(cue.text), "cue %zu", i);
        REQUIRE_TRUE(lyrics_insert(&document, &cue, NULL) == LYRICS_OK);
    }
    Lyric_Lane_Selection selection;
    lyric_lane_selection_clear(&selection);
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 1,
                                            LYRIC_LANE_CLICK_REPLACE));
    uint64_t last = document.cues[document.count - 1].id;
    // Silently keeping the first sixty-four would look like a successful drag
    // and then move the wrong set of cues.
    EXPECT_FALSE(lyric_lane_selection_apply(&selection, &document, last,
                                            LYRIC_LANE_CLICK_EXTEND));
    EXPECT_EQ_SIZE(selection.count, 1);
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 1));
}

TEST(lyric_lane_selection_prune_drops_deleted_cues)
{
    Lyrics_Document document;
    build_lane(&document);
    Lyric_Lane_Selection selection;
    lyric_lane_selection_clear(&selection);
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 2,
                                            LYRIC_LANE_CLICK_REPLACE));
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 3,
                                            LYRIC_LANE_CLICK_TOGGLE));
    REQUIRE_TRUE(lyrics_delete(&document, 3) == LYRICS_OK);

    // Without this, lyrics_shift_many rejects the whole move on the stale id
    // and dragging simply stops working with nothing on screen to explain it.
    lyric_lane_selection_prune(&selection, &document);
    EXPECT_EQ_SIZE(selection.count, 1);
    EXPECT_TRUE(lyric_lane_selection_contains(&selection, 2));
    EXPECT_EQ_U64(selection.anchor_id, 2);
    EXPECT_TRUE(lyrics_shift_many(&document, selection.ids, selection.count, 1.0) ==
                LYRICS_OK);
}

TEST(lyric_lane_clamp_move_stops_at_the_ends_of_the_track)
{
    Lyrics_Document document;
    build_lane(&document);
    Lyric_Lane_Selection selection;
    lyric_lane_selection_clear(&selection);
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 1,
                                            LYRIC_LANE_CLICK_REPLACE));
    REQUIRE_TRUE(lyric_lane_selection_apply(&selection, &document, 5,
                                            LYRIC_LANE_CLICK_TOGGLE));

    // Cue 1 starts at 10 s and cue 5 ends at 51 s in a 100 s track.
    EXPECT_NEAR(lyric_lane_clamp_move(&document, &selection, -3.0), -3.0, 1e-9);
    EXPECT_NEAR(lyric_lane_clamp_move(&document, &selection, -500.0), -10.0, 1e-9);
    EXPECT_NEAR(lyric_lane_clamp_move(&document, &selection, 500.0), 49.0, 1e-9);
    EXPECT_NEAR(lyric_lane_clamp_move(&document, &selection, NAN), 0.0, 1e-9);

    // The clamped value is exactly what the commit accepts, which is the
    // property that stops blocks following the pointer and then snapping back.
    double clamped = lyric_lane_clamp_move(&document, &selection, -500.0);
    EXPECT_TRUE(lyrics_shift_many(&document, selection.ids, selection.count,
                                  clamped) == LYRICS_OK);

    Lyric_Lane_Selection empty;
    lyric_lane_selection_clear(&empty);
    EXPECT_NEAR(lyric_lane_clamp_move(&document, &empty, 5.0), 0.0, 1e-9);
}

TEST(lyric_lane_clamp_resize_keeps_a_cue_grabbable)
{
    Lyrics_Document document;
    build_lane(&document);
    double start = 0.0, end = 0.0;

    // Dragging the trailing edge out is unrestricted until the track ends.
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, false, 14.0, &start, &end));
    EXPECT_NEAR(start, 10.0, 1e-9);
    EXPECT_NEAR(end, 14.0, 1e-9);
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, false, 900.0, &start, &end));
    EXPECT_NEAR(end, TRACK, 1e-9);

    // Dragging it back past the start stops at the minimum length rather than
    // collapsing the cue to something that can never be grabbed again.
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, false, 2.0, &start, &end));
    EXPECT_NEAR(end, 10.0 + LYRIC_LANE_MIN_CUE_SECONDS, 1e-9);
    EXPECT_TRUE(end > start);

    // The leading edge, symmetrically.
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, true, 7.5, &start, &end));
    EXPECT_NEAR(start, 7.5, 1e-9);
    EXPECT_NEAR(end, 11.0, 1e-9);
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, true, -40.0, &start, &end));
    EXPECT_NEAR(start, 0.0, 1e-9);
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, true, 50.0, &start, &end));
    EXPECT_NEAR(start, 11.0 - LYRIC_LANE_MIN_CUE_SECONDS, 1e-9);

    // Whatever it produces, lyrics_retime must accept it.
    REQUIRE_TRUE(lyric_lane_clamp_resize(&document, 1, true, 50.0, &start, &end));
    EXPECT_TRUE(lyrics_retime(&document, 1, start, end) == LYRICS_OK);

    EXPECT_FALSE(lyric_lane_clamp_resize(&document, 99, true, 1.0, &start, &end));
    EXPECT_FALSE(lyric_lane_clamp_resize(&document, 1, true, NAN, &start, &end));
}
