#ifndef MUSIALIZER_BEAT_TRACKER_H_
#define MUSIALIZER_BEAT_TRACKER_H_

#include <stdbool.h>
#include <stddef.h>

typedef struct Beat_Tracker {
    double previous_time;
    double anchor_time;
    double last_onset_time;
    double interval_seconds;
    size_t learned_intervals;
    bool initialized;
    bool has_onset;
} Beat_Tracker;

void beat_tracker_reset(Beat_Tracker *tracker);

/*
 * Produces a continuous phase in [0, 1). Before two credible onsets have been
 * observed the tracker uses a neutral 120 BPM clock anchored to track time.
 * A time reversal or a large discontinuity safely starts a new local clock.
 */
bool beat_tracker_update(Beat_Tracker *tracker, double time_seconds,
                         bool onset, float onset_strength, float *phase);

#endif // MUSIALIZER_BEAT_TRACKER_H_
