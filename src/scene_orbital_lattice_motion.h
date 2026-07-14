#ifndef MUSIALIZER_SCENE_ORBITAL_LATTICE_MOTION_H_
#define MUSIALIZER_SCENE_ORBITAL_LATTICE_MOTION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ORBITAL_LATTICE_RING_COUNT = 12,
    ORBITAL_LATTICE_NODES_PER_RING = 16,
};

#define ORBITAL_LATTICE_RING_SPACING 2.25f
#define ORBITAL_LATTICE_PATH_LENGTH \
    (ORBITAL_LATTICE_RING_COUNT*ORBITAL_LATTICE_RING_SPACING)

typedef struct Orbital_Lattice_Motion_Input {
    double time_seconds;
    float delta_seconds;
    const float *bands;
    size_t bands_count;
    float rms;
    float spectral_flux;
    bool onset;
    bool semantic_available;
    float semantic_valence;
    float semantic_tension;
    float semantic_confidence;
    float motion_rate;
} Orbital_Lattice_Motion_Input;

typedef struct Orbital_Lattice_Motion {
    uint64_t seed;
    bool initialized;
    double last_time_seconds;

    // Absolute, fixed-rate phases make seeked preview and uninterrupted export
    // land on the same camera path. Audio animates only bounded geometry and
    // color envelopes, never transport phase velocity.
    double camera_phase;
    double travel_phase;
    double twist_phase;
    double hue_degrees;

    // Deliberately distinct roles: bass shapes the structure, mids shape its
    // rings, treble articulates nodes, and flux controls restrained accents.
    float energy;
    float bass;
    float mids;
    float treble;
    float flux;
    float onset_pulse;
    float onset_hold_seconds;
    float semantic_valence;
    float semantic_tension;
    float node_bands[ORBITAL_LATTICE_NODES_PER_RING];
} Orbital_Lattice_Motion;

typedef struct Orbital_Lattice_Ring_Motion {
    float distance;
    float depth_t;
    float visibility;
} Orbital_Lattice_Ring_Motion;

void orbital_lattice_motion_init(Orbital_Lattice_Motion *motion, uint64_t seed);
void orbital_lattice_motion_update(
    Orbital_Lattice_Motion *motion,
    const Orbital_Lattice_Motion_Input *input);

// Samples one evenly staggered ring along the bounded travel path. Rings fade
// out at the far boundary and back in at the near boundary, so wrapping never
// resets the whole lattice in one visible jump.
bool orbital_lattice_motion_ring(
    const Orbital_Lattice_Motion *motion,
    size_t ring_index,
    Orbital_Lattice_Ring_Motion *ring);

#endif // MUSIALIZER_SCENE_ORBITAL_LATTICE_MOTION_H_
