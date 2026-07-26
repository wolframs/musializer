#ifndef MUSIALIZER_SCENE_CADENCE_TIMING_H_
#define MUSIALIZER_SCENE_CADENCE_TIMING_H_

#include <stdbool.h>
#include <stddef.h>

// The parts of Cadence's word timing that can be reasoned about without a
// window, extracted so they can be tested headlessly. Follows the precedent of
// scene_orbital_lattice_motion.c and scene_loom_weave.c.

// The line loosens back into particles over the final 1/9 of the cue.
#define CADENCE_LINE_DISSOLVE_SPAN 9.0f

// A word is drawn as settled type rather than a particle cloud only while its
// hold is above this; below it, scene_cadence draws the swarm.
#define CADENCE_HOLD_LEGIBLE 0.5f

float cadence_timing_smoothstep(float value);

// Dissolve factor for the line as a whole: 1 for most of the cue, falling to 0
// across its final beats.
float cadence_timing_line_hold(float cue_position);

// Dissolve factor for one word.
//
// This exists because applying the line's hold to every word made the final
// word illegible. Each word's window is a slice of [0, 1] and the last slice
// ends at exactly 1.0, so the line dissolve and the final word's own moment
// overlap by construction: the word's focus was scaled toward zero precisely
// while it was due to settle into type, and it stayed a particle cloud for the
// whole of its window. A word that has not finished its window is therefore
// exempt from the line dissolve.
float cadence_timing_word_hold(float cue_position, float window_end,
                               float line_hold);

// Splits [0, 1] across `count` words in proportion to glyph count + 1, which is
// what gives longer words longer moments. starts/ends must hold `count` floats.
bool cadence_timing_assign_windows(const unsigned *glyph_counts, size_t count,
                                   float *starts, float *ends);

#endif // MUSIALIZER_SCENE_CADENCE_TIMING_H_
