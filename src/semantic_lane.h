#ifndef MUSIALIZER_SEMANTIC_LANE_H_
#define MUSIALIZER_SEMANTIC_LANE_H_

#include <stdbool.h>
#include <stdint.h>

#include "event_timeline.h"

typedef struct Semantic_Frame {
    bool available;
    uint64_t source_id;
    float energy;
    float tension;
    float valence;
    float confidence;
} Semantic_Frame;

// Holds the latest model-derived semantic cue until the next cue begins. The
// measured audio frame remains separate; callers can decide how much creative
// influence to give this explicitly interpretive lane.
bool semantic_lane_sample(Event_Timeline_View events,
                          double time_seconds,
                          Semantic_Frame *frame);

#endif // MUSIALIZER_SEMANTIC_LANE_H_
