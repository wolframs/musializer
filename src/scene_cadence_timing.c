#include "scene_cadence_timing.h"

#include <math.h>

static float cadence_timing_clamp01(float value)
{
    if (!(value > 0.0f)) return 0.0f;   // also catches NaN
    if (value > 1.0f) return 1.0f;
    return value;
}

float cadence_timing_smoothstep(float value)
{
    value = cadence_timing_clamp01(value);
    return value*value*(3.0f - 2.0f*value);
}

float cadence_timing_line_hold(float cue_position)
{
    return cadence_timing_smoothstep((1.0f - cue_position)*CADENCE_LINE_DISSOLVE_SPAN);
}

float cadence_timing_word_hold(float cue_position, float window_end,
                               float line_hold)
{
    // Still being sung, or not yet reached: the line's exit does not apply.
    if (cue_position < window_end) return 1.0f;
    return line_hold;
}

bool cadence_timing_assign_windows(const unsigned *glyph_counts, size_t count,
                                   float *starts, float *ends)
{
    if (glyph_counts == NULL || starts == NULL || ends == NULL || count == 0) {
        return false;
    }
    unsigned long total = 0;
    for (size_t i = 0; i < count; ++i) total += (unsigned long)glyph_counts[i] + 1UL;
    if (total == 0) total = 1;
    unsigned long before = 0;
    for (size_t i = 0; i < count; ++i) {
        unsigned long weight = (unsigned long)glyph_counts[i] + 1UL;
        starts[i] = (float)before/(float)total;
        ends[i] = (float)(before + weight)/(float)total;
        before += weight;
    }
    // The final window closes exactly at the end of the cue by construction.
    ends[count - 1] = 1.0f;
    return true;
}
