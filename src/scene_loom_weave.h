#ifndef MUSIALIZER_SCENE_LOOM_WEAVE_H_
#define MUSIALIZER_SCENE_LOOM_WEAVE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The weave keeps two kinds of measured-audio memory for the Loom scene:
// short envelopes that make the working edge feel alive, and a per-slot
// record of the spectrum frozen as the fell passes, so the finished cloth
// is a stable tapestry of the whole song even with zero semantic events.
enum {
    LOOM_WEAVE_SLOTS = 144,
    LOOM_WEAVE_BINS = 24,
};

typedef struct Loom_Weave_Input {
    double time_seconds;
    double duration_seconds;
    float delta_seconds;
    const float *bands;   // analyzer smooth bands, lowest frequency first
    size_t bands_count;
    float rms;
    float spectral_flux;
    bool onset;
} Loom_Weave_Input;

typedef struct Loom_Weave_Column {
    bool woven;
    float energy;
    float tension;
    float profile[LOOM_WEAVE_BINS];
} Loom_Weave_Column;

typedef struct Loom_Weave {
    bool initialized;
    bool onset_active;
    double last_time_seconds;
    float energy;          // smoothed loudness, 0..1
    float tension;         // smoothed spectral flux, 0..1
    float onset_pulse;     // 1.0 on an onset edge, exponential decay
    uint32_t onset_serial; // rising-edge count; anchors per-burst placement
    float profile[LOOM_WEAVE_BINS]; // live coarse spectrum, low bin first
    Loom_Weave_Column columns[LOOM_WEAVE_SLOTS];
} Loom_Weave;

void loom_weave_init(Loom_Weave *weave);
void loom_weave_update(Loom_Weave *weave, const Loom_Weave_Input *input);

// Slot whose song-time span contains time_seconds, clamped to a valid index.
size_t loom_weave_slot(double time_seconds, double duration_seconds);

// Linear sample of a coarse profile at band_t in [0,1]; 0 is the lowest band.
float loom_weave_profile_sample(const float profile[LOOM_WEAVE_BINS],
                                float band_t);

#endif // MUSIALIZER_SCENE_LOOM_WEAVE_H_
