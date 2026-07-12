#include "semantic_lane.h"

#include <math.h>
#include <string.h>

bool semantic_lane_sample(Event_Timeline_View events,
                          double time_seconds,
                          Semantic_Frame *frame)
{
    if (frame == NULL || !isfinite(time_seconds) || time_seconds < 0.0 ||
        (events.count > 0 && events.events == NULL) ||
        events.count > EVENT_TIMELINE_CAPACITY) return false;
    Semantic_Frame result = {0};
    for (size_t i = 0; i < events.count; ++i) {
        const Event_Record *event = &events.events[i];
        if (event->timestamp_seconds > time_seconds) break;
        if (!event_record_is_valid(event) || event->type != EVENT_TYPE_SEMANTIC ||
            event->value_count != 4) continue;
        float energy = event->values[0];
        float tension = event->values[1];
        float valence = event->values[2];
        float confidence = event->values[3];
        if (energy < 0.0f || energy > 1.0f ||
            tension < 0.0f || tension > 1.0f ||
            valence < -1.0f || valence > 1.0f ||
            confidence < 0.0f || confidence > 1.0f) continue;
        result = (Semantic_Frame) {
            .available = true,
            .source_id = event->id,
            .energy = energy,
            .tension = tension,
            .valence = valence,
            .confidence = confidence,
        };
    }
    *frame = result;
    return result.available;
}
