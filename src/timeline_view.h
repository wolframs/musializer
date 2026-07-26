#ifndef MUSIALIZER_TIMELINE_VIEW_H_
#define MUSIALIZER_TIMELINE_VIEW_H_

#include <stdbool.h>

// A zoomable, pannable window onto a track. The timeline strip used to map the
// whole track across the waveform width, which makes a two-second lyric cue in
// a four-minute song about 15 px wide -- too small to grab, let alone to place
// a boundary in. Every seconds<->pixel conversion in the strip goes through
// this model so the waveform, the tick labels, the event markers, the lyric
// blocks, the playhead and the scrubber cannot disagree about where a moment is.
//
// The model is deliberately Raylib-free and holds no pixels: callers pass the
// destination rectangle's left edge and width per call. That keeps it testable
// headlessly and keeps a resized window from invalidating stored state.

// Smallest window the view will zoom to. A 0.25 s span across a 900 px strip is
// about 0.28 ms per pixel, already finer than the millisecond the lyric bridge
// format round-trips, so further zoom would buy precision the format cannot
// store while making the 2048-bin waveform visibly blocky.
#define TIMELINE_VIEW_MIN_SPAN_SECONDS 0.25

// Fraction of the visible span kept between the playhead and the edge when the
// view follows playback. Zero would re-centre on every frame at the boundary.
#define TIMELINE_VIEW_REVEAL_MARGIN 0.12

typedef struct Timeline_View {
    double start_seconds;
    double span_seconds;
} Timeline_View;

// Every mutator re-establishes the same invariant before returning:
//   start >= 0, span in [min(MIN_SPAN, duration), duration], start + span <= duration.
// A non-finite or non-positive duration collapses the view to zero, which is
// the state callers already treat as "there is no timeline to draw".
void timeline_view_reset(Timeline_View *view, double duration_seconds);
void timeline_view_clamp(Timeline_View *view, double duration_seconds);

// True when the view shows the whole track, i.e. zooming out further is inert.
bool timeline_view_is_whole(const Timeline_View *view, double duration_seconds);

// factor > 1 zooms in, factor < 1 zooms out. anchor_seconds keeps its pixel
// position wherever it can: the moment under the cursor does not slide away,
// which is what makes wheel zoom feel attached to the pointer. The anchor is
// only violated when honouring it would push the window off either end.
void timeline_view_zoom(Timeline_View *view, double duration_seconds,
                        double factor, double anchor_seconds);

void timeline_view_pan(Timeline_View *view, double duration_seconds,
                       double delta_seconds);

// Scrolls by the least amount that brings `seconds` inside the window with
// TIMELINE_VIEW_REVEAL_MARGIN of the span to spare. A moment already comfortably
// inside the window leaves the view untouched, so this is safe to call per frame.
void timeline_view_reveal(Timeline_View *view, double duration_seconds,
                          double seconds);

// Maps a moment onto the strip. The result is intentionally allowed to fall
// outside [left, left + width]: a caller drawing a cue block needs the real
// off-screen edge to clip against, not a pre-clamped one that would make every
// partially visible block look as though it started at the window edge.
double timeline_view_x_at(const Timeline_View *view, double seconds,
                          double left, double width);

// Inverse of timeline_view_x_at, clamped to [0, duration] because the result is
// always used as a transport position or a cue boundary.
double timeline_view_seconds_at(const Timeline_View *view, double x,
                                double left, double width,
                                double duration_seconds);

// The finest interval a one-pixel pointer movement can express. Callers use it
// to decide whether a drag is worth committing and to size an edge grab region.
double timeline_view_seconds_per_pixel(const Timeline_View *view, double width);

// Spacing between labelled gridlines for a given visible span, chosen from a
// fixed ladder of readable intervals. The strip used to pick this from the
// track length, so zooming into a four-minute song kept the 30 s spacing and
// left a window with no label in it at all.
double timeline_view_tick_step(double span_seconds);

#endif // MUSIALIZER_TIMELINE_VIEW_H_
