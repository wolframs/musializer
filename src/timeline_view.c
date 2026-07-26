#include "timeline_view.h"

#include <math.h>
#include <stddef.h>

static bool usable_duration(double duration_seconds)
{
    return isfinite(duration_seconds) && duration_seconds > 0.0;
}

static double minimum_span(double duration_seconds)
{
    // A track shorter than the floor is shown whole; the floor must never
    // exceed the material or the window would extend past the end.
    return duration_seconds < TIMELINE_VIEW_MIN_SPAN_SECONDS ?
        duration_seconds : TIMELINE_VIEW_MIN_SPAN_SECONDS;
}

void timeline_view_reset(Timeline_View *view, double duration_seconds)
{
    if (view == NULL) return;
    view->start_seconds = 0.0;
    view->span_seconds = usable_duration(duration_seconds) ? duration_seconds : 0.0;
}

void timeline_view_clamp(Timeline_View *view, double duration_seconds)
{
    if (view == NULL) return;
    if (!usable_duration(duration_seconds)) {
        view->start_seconds = 0.0;
        view->span_seconds = 0.0;
        return;
    }
    // Non-finite state can only arrive through hot reload of an older layout or
    // a caller that did its own arithmetic; recover to the whole track rather
    // than propagating NaN into every pixel conversion below.
    if (!isfinite(view->span_seconds) || view->span_seconds <= 0.0 ||
        !isfinite(view->start_seconds)) {
        timeline_view_reset(view, duration_seconds);
        return;
    }
    double floor_span = minimum_span(duration_seconds);
    if (view->span_seconds < floor_span) view->span_seconds = floor_span;
    if (view->span_seconds > duration_seconds) view->span_seconds = duration_seconds;
    if (view->start_seconds < 0.0) view->start_seconds = 0.0;
    double last_start = duration_seconds - view->span_seconds;
    if (last_start < 0.0) last_start = 0.0;
    if (view->start_seconds > last_start) view->start_seconds = last_start;
}

bool timeline_view_is_whole(const Timeline_View *view, double duration_seconds)
{
    if (view == NULL || !usable_duration(duration_seconds)) return true;
    if (!isfinite(view->span_seconds)) return true;
    return view->span_seconds >= duration_seconds;
}

void timeline_view_zoom(Timeline_View *view, double duration_seconds,
                        double factor, double anchor_seconds)
{
    if (view == NULL) return;
    timeline_view_clamp(view, duration_seconds);
    if (!usable_duration(duration_seconds)) return;
    if (!isfinite(factor) || factor <= 0.0) return;

    double old_span = view->span_seconds;
    double span = old_span/factor;
    double floor_span = minimum_span(duration_seconds);
    if (span < floor_span) span = floor_span;
    if (span > duration_seconds) span = duration_seconds;

    // Hold the anchor's fractional position in the window fixed. An anchor
    // outside the current window (a keyboard zoom against a playhead that has
    // scrolled away) still works: the fraction falls outside [0,1] and the
    // clamp below pulls the result back to a legal window.
    if (!isfinite(anchor_seconds)) anchor_seconds = view->start_seconds + old_span*0.5;
    double fraction = old_span > 0.0 ?
        (anchor_seconds - view->start_seconds)/old_span : 0.5;
    if (!isfinite(fraction)) fraction = 0.5;
    view->start_seconds = anchor_seconds - fraction*span;
    view->span_seconds = span;
    timeline_view_clamp(view, duration_seconds);
}

void timeline_view_pan(Timeline_View *view, double duration_seconds,
                       double delta_seconds)
{
    if (view == NULL) return;
    timeline_view_clamp(view, duration_seconds);
    if (!usable_duration(duration_seconds)) return;
    if (!isfinite(delta_seconds)) return;
    view->start_seconds += delta_seconds;
    timeline_view_clamp(view, duration_seconds);
}

void timeline_view_reveal(Timeline_View *view, double duration_seconds,
                          double seconds)
{
    if (view == NULL) return;
    timeline_view_clamp(view, duration_seconds);
    if (!usable_duration(duration_seconds)) return;
    if (!isfinite(seconds)) return;
    double margin = view->span_seconds*TIMELINE_VIEW_REVEAL_MARGIN;
    double low = view->start_seconds + margin;
    double high = view->start_seconds + view->span_seconds - margin;
    if (seconds < low) {
        view->start_seconds = seconds - margin;
    } else if (seconds > high) {
        view->start_seconds = seconds - view->span_seconds + margin;
    }
    timeline_view_clamp(view, duration_seconds);
}

double timeline_view_x_at(const Timeline_View *view, double seconds,
                          double left, double width)
{
    if (view == NULL || !isfinite(seconds) || !isfinite(left) ||
        !isfinite(width) || width <= 0.0) return left;
    if (!isfinite(view->span_seconds) || view->span_seconds <= 0.0) return left;
    return left + (seconds - view->start_seconds)/view->span_seconds*width;
}

double timeline_view_seconds_at(const Timeline_View *view, double x,
                                double left, double width,
                                double duration_seconds)
{
    if (view == NULL || !usable_duration(duration_seconds)) return 0.0;
    if (!isfinite(x) || !isfinite(left) || !isfinite(width) || width <= 0.0) {
        return view->start_seconds;
    }
    if (!isfinite(view->span_seconds) || view->span_seconds <= 0.0) {
        return view->start_seconds;
    }
    double seconds = view->start_seconds + (x - left)/width*view->span_seconds;
    if (!isfinite(seconds) || seconds < 0.0) return 0.0;
    if (seconds > duration_seconds) return duration_seconds;
    return seconds;
}

double timeline_view_seconds_per_pixel(const Timeline_View *view, double width)
{
    if (view == NULL || !isfinite(width) || width <= 0.0) return 0.0;
    if (!isfinite(view->span_seconds) || view->span_seconds <= 0.0) return 0.0;
    return view->span_seconds/width;
}

double timeline_view_tick_step(double span_seconds)
{
    // Intervals a listener reads without arithmetic: fractions of a second,
    // then seconds, then the bar-ish 15/30/60 groupings, then minutes.
    static const double ladder[] = {
        0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0,
        15.0, 30.0, 60.0, 120.0, 300.0, 600.0,
    };
    const size_t count = sizeof(ladder)/sizeof(ladder[0]);
    if (!isfinite(span_seconds) || span_seconds <= 0.0) return ladder[0];
    // Around eight divisions: enough to locate a moment, few enough that the
    // labels do not collide at the narrowest supported strip.
    double target = span_seconds/8.0;
    for (size_t i = 0; i < count; ++i) {
        if (ladder[i] >= target) return ladder[i];
    }
    return ladder[count - 1];
}
