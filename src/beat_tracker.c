#include "beat_tracker.h"

#include <math.h>
#include <string.h>

enum {
    BEAT_TRACKER_DEFAULT_BPM = 120,
};

static const double minimum_interval = 0.25;
static const double maximum_interval = 1.50;
static const double discontinuity_seconds = 0.75;

void beat_tracker_reset(Beat_Tracker *tracker)
{
    if (tracker == NULL) return;
    memset(tracker, 0, sizeof(*tracker));
    tracker->interval_seconds = 60.0/(double)BEAT_TRACKER_DEFAULT_BPM;
}

static double tempo_fold(double interval, double reference)
{
    while (interval < reference*0.67 && interval*2.0 <= maximum_interval) {
        interval *= 2.0;
    }
    while (interval > reference*1.50 && interval*0.5 >= minimum_interval) {
        interval *= 0.5;
    }
    return interval;
}

bool beat_tracker_update(Beat_Tracker *tracker, double time_seconds,
                         bool onset, float onset_strength, float *phase)
{
    if (tracker == NULL || phase == NULL || !isfinite(time_seconds) ||
        time_seconds < 0.0 || !isfinite(onset_strength) || onset_strength < 0.0f) {
        return false;
    }

    if (!tracker->initialized || time_seconds < tracker->previous_time ||
        time_seconds - tracker->previous_time > discontinuity_seconds) {
        double interval = tracker->interval_seconds;
        beat_tracker_reset(tracker);
        if (isfinite(interval) && interval >= minimum_interval &&
            interval <= maximum_interval) {
            tracker->interval_seconds = interval;
        }
        tracker->initialized = true;
        tracker->anchor_time = time_seconds;
        tracker->previous_time = time_seconds;
    }

    if (onset && onset_strength >= 0.04f) {
        if (!tracker->has_onset) {
            tracker->has_onset = true;
            tracker->last_onset_time = time_seconds;
            tracker->anchor_time = time_seconds;
        } else {
            double observed = time_seconds - tracker->last_onset_time;
            if (observed >= minimum_interval && observed <= maximum_interval) {
                observed = tempo_fold(observed, tracker->interval_seconds);
                double weight = tracker->learned_intervals < 4 ? 0.42 : 0.20;
                tracker->interval_seconds +=
                    (observed - tracker->interval_seconds)*weight;
                tracker->learned_intervals += 1;
                tracker->last_onset_time = time_seconds;
                tracker->anchor_time = time_seconds;
            }
        }
    }

    double position = (time_seconds - tracker->anchor_time)/tracker->interval_seconds;
    position -= floor(position);
    if (position < 0.0) position += 1.0;
    *phase = (float)position;
    tracker->previous_time = time_seconds;
    return isfinite(*phase) && *phase >= 0.0f && *phase < 1.0f;
}
