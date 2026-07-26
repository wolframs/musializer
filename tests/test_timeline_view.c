#include "timeline_view.h"
#include "test_support.h"

#include <math.h>

// The demo fixture is about 40 s; a real song is nearer 240 s. Both shapes are
// exercised because the interesting clamps are relative to duration.
#define TRACK 240.0
#define STRIP_LEFT 12.0
#define STRIP_WIDTH 1200.0

static Timeline_View whole(void)
{
    Timeline_View view;
    timeline_view_reset(&view, TRACK);
    return view;
}

// The invariant every mutator promises. Asserting it after each operation is
// what makes the clamping testable at all: the individual numbers below are
// examples, this is the contract.
static void expect_legal(const Timeline_View *view, double duration)
{
    EXPECT_TRUE(isfinite(view->start_seconds) && view->start_seconds >= 0.0);
    EXPECT_TRUE(isfinite(view->span_seconds) && view->span_seconds > 0.0);
    EXPECT_TRUE(view->span_seconds <= duration + 1e-9);
    EXPECT_TRUE(view->start_seconds + view->span_seconds <= duration + 1e-9);
}

TEST(timeline_view_starts_showing_the_whole_track)
{
    Timeline_View view = whole();
    EXPECT_NEAR(view.start_seconds, 0.0, 1e-9);
    EXPECT_NEAR(view.span_seconds, TRACK, 1e-9);
    EXPECT_TRUE(timeline_view_is_whole(&view, TRACK));
    // The unzoomed mapping must be exactly the arithmetic it replaced, or
    // every capture in tools/ui_states.txt shifts by a pixel.
    EXPECT_NEAR(timeline_view_x_at(&view, 0.0, STRIP_LEFT, STRIP_WIDTH),
                STRIP_LEFT, 1e-9);
    EXPECT_NEAR(timeline_view_x_at(&view, TRACK, STRIP_LEFT, STRIP_WIDTH),
                STRIP_LEFT + STRIP_WIDTH, 1e-9);
    EXPECT_NEAR(timeline_view_x_at(&view, 60.0, STRIP_LEFT, STRIP_WIDTH),
                STRIP_LEFT + STRIP_WIDTH*0.25, 1e-9);
}

TEST(timeline_view_zoom_keeps_the_anchor_under_the_pointer)
{
    Timeline_View view = whole();
    double anchor = 90.0;
    double before = timeline_view_x_at(&view, anchor, STRIP_LEFT, STRIP_WIDTH);
    timeline_view_zoom(&view, TRACK, 4.0, anchor);
    expect_legal(&view, TRACK);
    EXPECT_NEAR(view.span_seconds, 60.0, 1e-9);
    // The whole point of anchored zoom: the moment the cursor was over does not
    // move. A centred zoom would put it back at the middle of the strip instead.
    EXPECT_NEAR(timeline_view_x_at(&view, anchor, STRIP_LEFT, STRIP_WIDTH),
                before, 1e-6);
    // ... and it is genuinely not the centre, which is what makes the check
    // above non-vacuous.
    EXPECT_TRUE(fabs(before - (STRIP_LEFT + STRIP_WIDTH*0.5)) > 100.0);
}

TEST(timeline_view_zoom_at_the_ends_sacrifices_the_anchor_not_the_window)
{
    Timeline_View view = whole();
    // Anchoring on the very first moment would want start = 0 - 0*span, which is
    // legal; anchoring near the tail wants a window running past the end.
    timeline_view_zoom(&view, TRACK, 8.0, TRACK);
    expect_legal(&view, TRACK);
    EXPECT_NEAR(view.span_seconds, 30.0, 1e-9);
    EXPECT_NEAR(view.start_seconds, TRACK - 30.0, 1e-9);

    Timeline_View head = whole();
    timeline_view_zoom(&head, TRACK, 8.0, 0.0);
    expect_legal(&head, TRACK);
    EXPECT_NEAR(head.start_seconds, 0.0, 1e-9);
}

TEST(timeline_view_will_not_zoom_past_the_span_floor)
{
    Timeline_View view = whole();
    for (int i = 0; i < 40; ++i) {
        timeline_view_zoom(&view, TRACK, 2.0, 120.0);
        expect_legal(&view, TRACK);
    }
    EXPECT_NEAR(view.span_seconds, TIMELINE_VIEW_MIN_SPAN_SECONDS, 1e-9);
    // At the floor a pixel is still coarser than nothing: the drag resolution
    // the UI advertises has to be a real number, not zero.
    double resolution = timeline_view_seconds_per_pixel(&view, STRIP_WIDTH);
    EXPECT_TRUE(resolution > 0.0 && resolution < 0.001);
}

TEST(timeline_view_zooming_out_always_returns_to_the_whole_track)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 64.0, 200.0);
    EXPECT_FALSE(timeline_view_is_whole(&view, TRACK));
    for (int i = 0; i < 40; ++i) {
        timeline_view_zoom(&view, TRACK, 0.5, 200.0);
        expect_legal(&view, TRACK);
    }
    EXPECT_TRUE(timeline_view_is_whole(&view, TRACK));
    EXPECT_NEAR(view.start_seconds, 0.0, 1e-9);
    EXPECT_NEAR(view.span_seconds, TRACK, 1e-9);
}

TEST(timeline_view_pan_stops_at_both_ends)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 4.0, 120.0);
    double span = view.span_seconds;

    timeline_view_pan(&view, TRACK, -1000.0);
    expect_legal(&view, TRACK);
    EXPECT_NEAR(view.start_seconds, 0.0, 1e-9);
    EXPECT_NEAR(view.span_seconds, span, 1e-9);

    timeline_view_pan(&view, TRACK, 1000.0);
    expect_legal(&view, TRACK);
    EXPECT_NEAR(view.start_seconds, TRACK - span, 1e-9);
    // Panning must never change how much you can see. A clamp written as
    // "shrink the span to fit" would pass the legality check and silently zoom.
    EXPECT_NEAR(view.span_seconds, span, 1e-9);
}

TEST(timeline_view_reveal_only_moves_when_the_moment_is_outside)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 4.0, 120.0);
    Timeline_View before = view;

    // Dead centre: already visible with margin to spare, so nothing happens.
    timeline_view_reveal(&view, TRACK, view.start_seconds + view.span_seconds*0.5);
    EXPECT_NEAR(view.start_seconds, before.start_seconds, 1e-9);

    // Past the right edge: scrolls just enough to bring it in with a margin.
    double target = before.start_seconds + before.span_seconds + 5.0;
    timeline_view_reveal(&view, TRACK, target);
    expect_legal(&view, TRACK);
    EXPECT_TRUE(target >= view.start_seconds && target <= view.start_seconds + view.span_seconds);
    EXPECT_NEAR(view.start_seconds,
                target - view.span_seconds + view.span_seconds*TIMELINE_VIEW_REVEAL_MARGIN,
                1e-6);

    // Behind the left edge, symmetric.
    timeline_view_reveal(&view, TRACK, 3.0);
    expect_legal(&view, TRACK);
    EXPECT_TRUE(3.0 >= view.start_seconds && 3.0 <= view.start_seconds + view.span_seconds);
}

TEST(timeline_view_round_trips_pixels_and_seconds)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 12.0, 77.0);
    for (double x = STRIP_LEFT; x <= STRIP_LEFT + STRIP_WIDTH; x += 37.0) {
        double seconds = timeline_view_seconds_at(&view, x, STRIP_LEFT, STRIP_WIDTH, TRACK);
        double back = timeline_view_x_at(&view, seconds, STRIP_LEFT, STRIP_WIDTH);
        EXPECT_NEAR(back, x, 1e-6);
    }
}

TEST(timeline_view_seconds_at_stays_inside_the_track)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 6.0, 20.0);
    // Dragging a cue boundary off the left of the strip must yield 0, not a
    // negative start that lyrics_update would reject with no visible reason.
    EXPECT_NEAR(timeline_view_seconds_at(&view, STRIP_LEFT - 4000.0, STRIP_LEFT,
                                         STRIP_WIDTH, TRACK), 0.0, 1e-9);
    EXPECT_NEAR(timeline_view_seconds_at(&view, STRIP_LEFT + 9000.0, STRIP_LEFT,
                                         STRIP_WIDTH, TRACK), TRACK, 1e-9);
}

TEST(timeline_view_x_at_reports_offscreen_edges_honestly)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 8.0, 120.0);
    // A cue that begins before the window must map to a negative offset so the
    // caller can clip it. Clamping here would draw every earlier cue as though
    // it started exactly at the left edge, which is how a scrolled lane starts
    // lying about cue boundaries.
    double x = timeline_view_x_at(&view, 0.0, STRIP_LEFT, STRIP_WIDTH);
    EXPECT_TRUE(x < STRIP_LEFT - 1.0);
}

TEST(timeline_view_survives_a_track_shorter_than_the_span_floor)
{
    Timeline_View view;
    timeline_view_reset(&view, 0.1);
    expect_legal(&view, 0.1);
    timeline_view_zoom(&view, 0.1, 100.0, 0.05);
    expect_legal(&view, 0.1);
    // The floor must yield to the material, otherwise the window extends past
    // the end of a very short track.
    EXPECT_NEAR(view.span_seconds, 0.1, 1e-9);
    EXPECT_TRUE(timeline_view_is_whole(&view, 0.1));
}

TEST(timeline_view_rejects_degenerate_input_without_producing_nan)
{
    Timeline_View view = whole();
    timeline_view_zoom(&view, TRACK, 4.0, 120.0);
    Timeline_View before = view;

    timeline_view_zoom(&view, TRACK, NAN, 120.0);
    timeline_view_zoom(&view, TRACK, 0.0, 120.0);
    timeline_view_zoom(&view, TRACK, -2.0, 120.0);
    timeline_view_pan(&view, TRACK, NAN);
    timeline_view_reveal(&view, TRACK, INFINITY);
    EXPECT_NEAR(view.start_seconds, before.start_seconds, 1e-9);
    EXPECT_NEAR(view.span_seconds, before.span_seconds, 1e-9);

    // A NaN anchor is not the caller's fault the same way: it means "zoom on
    // whatever is in the middle", which is the useful behaviour for a keyboard
    // zoom with no pointer.
    timeline_view_zoom(&view, TRACK, 2.0, NAN);
    expect_legal(&view, TRACK);
    EXPECT_NEAR(view.span_seconds, before.span_seconds*0.5, 1e-9);

    // A corrupt view recovers to the whole track instead of poisoning pixels.
    Timeline_View broken = {.start_seconds = NAN, .span_seconds = NAN};
    timeline_view_clamp(&broken, TRACK);
    expect_legal(&broken, TRACK);
    EXPECT_TRUE(timeline_view_is_whole(&broken, TRACK));
}

TEST(timeline_view_tick_step_keeps_labels_inside_every_window)
{
    // The defect this replaces: the step was chosen from the track length, so a
    // 240 s song kept 30 s spacing at every zoom and a 4 s window contained no
    // labelled gridline at all.
    static const double spans[] = {0.25, 0.5, 1.0, 4.0, 12.0, 40.0, 90.0,
                                   240.0, 600.0, 3600.0};
    for (size_t i = 0; i < sizeof(spans)/sizeof(spans[0]); ++i) {
        double step = timeline_view_tick_step(spans[i]);
        EXPECT_TRUE(step > 0.0 && isfinite(step));
        // At least two divisions, so there is always a labelled reference in
        // view, and no more than about forty, so they do not collide.
        EXPECT_TRUE(spans[i]/step >= 2.0);
        EXPECT_TRUE(spans[i]/step <= 40.0);
    }
    // Monotone: zooming in never widens the spacing.
    double previous = timeline_view_tick_step(0.25);
    for (double span = 0.25; span < 4000.0; span *= 1.3) {
        double step = timeline_view_tick_step(span);
        EXPECT_TRUE(step >= previous);
        previous = step;
    }
    EXPECT_NEAR(timeline_view_tick_step(-1.0), 0.05, 1e-9);
    EXPECT_NEAR(timeline_view_tick_step(NAN), 0.05, 1e-9);
}

TEST(timeline_view_treats_a_missing_track_as_no_timeline)
{
    Timeline_View view;
    timeline_view_reset(&view, 0.0);
    EXPECT_NEAR(view.span_seconds, 0.0, 1e-9);
    EXPECT_TRUE(timeline_view_is_whole(&view, 0.0));
    EXPECT_NEAR(timeline_view_seconds_per_pixel(&view, STRIP_WIDTH), 0.0, 1e-9);
    // Callers draw before they check; these must not divide by zero.
    EXPECT_NEAR(timeline_view_x_at(&view, 5.0, STRIP_LEFT, STRIP_WIDTH), STRIP_LEFT, 1e-9);
    EXPECT_NEAR(timeline_view_seconds_at(&view, 900.0, STRIP_LEFT, STRIP_WIDTH, 0.0), 0.0, 1e-9);
}
