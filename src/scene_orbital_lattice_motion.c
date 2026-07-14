#include "scene_orbital_lattice_motion.h"

#include <math.h>
#include <string.h>

#define ORBITAL_PI 3.14159265358979323846
#define ORBITAL_TAU (2.0*ORBITAL_PI)

typedef struct Orbital_Lattice_Targets {
    float energy;
    float bass;
    float mids;
    float treble;
    float flux;
    float semantic_valence;
    float semantic_tension;
    float node_bands[ORBITAL_LATTICE_NODES_PER_RING];
} Orbital_Lattice_Targets;

static float orbital_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static float orbital_clamp_signed(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (value <= -1.0f) return -1.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static double orbital_wrap(double value, double period)
{
    if (!isfinite(value) || !isfinite(period) || period <= 0.0) return 0.0;
    value = fmod(value, period);
    return value < 0.0 ? value + period : value;
}

static uint32_t orbital_hash(uint64_t seed, uint32_t salt)
{
    uint64_t value = seed ^ ((uint64_t)salt + UINT64_C(0x9e3779b97f4a7c15));
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (uint32_t)value;
}

static double orbital_hash_unit(uint64_t seed, uint32_t salt)
{
    return (double)(orbital_hash(seed, salt) & UINT32_C(0xffff))/65535.0;
}

static float orbital_band_mean(const float *bands, size_t count,
                               size_t begin, size_t end)
{
    if (bands == NULL || count == 0) return 0.0f;
    if (begin >= count) begin = count - 1;
    if (end > count) end = count;
    if (end <= begin) end = begin + 1;

    double total = 0.0;
    for (size_t i = begin; i < end; ++i) {
        total += orbital_clamp01(bands[i]);
    }
    return (float)(total/(double)(end - begin));
}

static Orbital_Lattice_Targets orbital_targets(
    const Orbital_Lattice_Motion_Input *input)
{
    Orbital_Lattice_Targets targets = {0};
    if (input == NULL) return targets;

    targets.energy = orbital_clamp01(input->rms*1.65f);
    targets.flux = orbital_clamp01(input->spectral_flux*4.0f);

    const size_t count = input->bands_count;
    if (input->bands != NULL && count > 0) {
        size_t bass_end = count/5;
        if (bass_end < 1) bass_end = 1;
        size_t treble_begin = count*3/5;
        if (treble_begin < bass_end) treble_begin = bass_end;
        if (treble_begin >= count) treble_begin = count - 1;
        targets.bass = orbital_band_mean(input->bands, count, 0, bass_end);
        targets.mids = orbital_band_mean(input->bands, count,
                                         bass_end, treble_begin);
        targets.treble = orbital_band_mean(input->bands, count,
                                           treble_begin, count);

        for (size_t node = 0;
             node < ORBITAL_LATTICE_NODES_PER_RING; ++node) {
            size_t begin = node*count/ORBITAL_LATTICE_NODES_PER_RING;
            size_t end = (node + 1)*count/ORBITAL_LATTICE_NODES_PER_RING;
            if (end <= begin) end = begin + 1;
            targets.node_bands[node] = orbital_band_mean(
                input->bands, count, begin, end);
        }
    }

    if (input->semantic_available) {
        float confidence = orbital_clamp01(input->semantic_confidence);
        targets.semantic_valence = orbital_clamp_signed(input->semantic_valence)*confidence;
        targets.semantic_tension = orbital_clamp01(input->semantic_tension)*confidence;
    }
    return targets;
}

static float orbital_damp(float current, float target,
                          float attack_rate, float release_rate, float delta)
{
    float rate = target > current ? attack_rate : release_rate;
    float blend = 1.0f - expf(-rate*delta);
    return current + (target - current)*blend;
}

static float orbital_smoothstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1) return value >= edge1 ? 1.0f : 0.0f;
    float amount = orbital_clamp01((value - edge0)/(edge1 - edge0));
    return amount*amount*(3.0f - 2.0f*amount);
}

static void orbital_lattice_motion_rebase(
    Orbital_Lattice_Motion *motion,
    const Orbital_Lattice_Motion_Input *input,
    const Orbital_Lattice_Targets *targets)
{
    double time = input != NULL && isfinite(input->time_seconds) &&
                  input->time_seconds > 0.0 ? input->time_seconds : 0.0;
    double motion_rate = isfinite(input->motion_rate) && input->motion_rate > 0.0f ?
                         input->motion_rate : 1.0;
    motion->camera_phase = orbital_wrap(
        orbital_hash_unit(motion->seed, 1)*ORBITAL_TAU + time*0.045*motion_rate,
        ORBITAL_TAU);
    motion->travel_phase = orbital_wrap(
        orbital_hash_unit(motion->seed, 2)*ORBITAL_LATTICE_PATH_LENGTH +
        time*0.38*motion_rate,
        ORBITAL_LATTICE_PATH_LENGTH);
    motion->twist_phase = orbital_wrap(
        orbital_hash_unit(motion->seed, 3)*ORBITAL_TAU + time*0.10*motion_rate,
        ORBITAL_TAU);
    motion->hue_degrees = orbital_wrap(
        205.0 + orbital_hash_unit(motion->seed, 4)*110.0 + time*1.5*motion_rate,
        360.0);

    motion->energy = targets->energy;
    motion->bass = targets->bass;
    motion->mids = targets->mids;
    motion->treble = targets->treble;
    motion->flux = targets->flux;
    motion->semantic_valence = targets->semantic_valence;
    motion->semantic_tension = targets->semantic_tension;
    memcpy(motion->node_bands, targets->node_bands,
           sizeof(motion->node_bands));
    motion->onset_hold_seconds = input != NULL && input->onset ? 0.09f : 0.0f;
    motion->onset_pulse = input != NULL && input->onset ? 1.0f : 0.0f;
    motion->last_time_seconds = time;
    motion->initialized = true;
}

void orbital_lattice_motion_init(Orbital_Lattice_Motion *motion, uint64_t seed)
{
    if (motion == NULL) return;
    memset(motion, 0, sizeof(*motion));
    motion->seed = seed;
}

void orbital_lattice_motion_update(
    Orbital_Lattice_Motion *motion,
    const Orbital_Lattice_Motion_Input *input)
{
    if (motion == NULL || input == NULL) return;
    Orbital_Lattice_Targets targets = orbital_targets(input);

    double time = isfinite(input->time_seconds) && input->time_seconds > 0.0 ?
                  input->time_seconds : 0.0;
    double elapsed = time - motion->last_time_seconds;
    float delta = isfinite(input->delta_seconds) && input->delta_seconds > 0.0f ?
                  input->delta_seconds : 0.0f;
    bool discontinuity = !motion->initialized || elapsed < -0.001 ||
                         elapsed > 0.25 || delta > 0.20f ||
                         (fabs(elapsed) > 0.02 && delta == 0.0f);
    if (discontinuity) {
        orbital_lattice_motion_rebase(motion, input, &targets);
        return;
    }
    if (delta > 0.10f) delta = 0.10f;
    motion->last_time_seconds = time;
    if (delta <= 0.0f) return;

    motion->energy = orbital_damp(motion->energy, targets.energy,
                                  5.5f, 2.4f, delta);
    motion->bass = orbital_damp(motion->bass, targets.bass,
                                4.2f, 2.0f, delta);
    motion->mids = orbital_damp(motion->mids, targets.mids,
                                5.0f, 2.6f, delta);
    motion->treble = orbital_damp(motion->treble, targets.treble,
                                  7.0f, 3.6f, delta);
    motion->flux = orbital_damp(motion->flux, targets.flux,
                                6.5f, 2.8f, delta);
    motion->semantic_valence = orbital_damp(
        motion->semantic_valence, targets.semantic_valence, 1.4f, 1.1f, delta);
    motion->semantic_tension = orbital_damp(
        motion->semantic_tension, targets.semantic_tension, 1.5f, 1.1f, delta);
    for (size_t node = 0;
         node < ORBITAL_LATTICE_NODES_PER_RING; ++node) {
        motion->node_bands[node] = orbital_damp(
            motion->node_bands[node], targets.node_bands[node],
            8.0f, 4.0f, delta);
    }

    if (input->onset) motion->onset_hold_seconds = 0.09f;
    float onset_target = motion->onset_hold_seconds > 0.0f ? 1.0f : 0.0f;
    motion->onset_pulse = orbital_damp(
        motion->onset_pulse, onset_target, 18.0f, 3.0f, delta);
    motion->onset_hold_seconds = fmaxf(0.0f,
        motion->onset_hold_seconds - delta);

    // Motion phase is a deterministic function of seed and transport time.
    // Audio changes the damped geometry, color, and scale, but never the rate
    // at which absolute phase accumulates; seeking therefore lands on the same
    // orbit as uninterrupted preview/export instead of integrating a different
    // history-dependent camera path.
    const double camera_speed = 0.045;
    const double travel_speed = 0.38;
    const double twist_speed = 0.10;
    const double hue_speed = 1.5;
    const double motion_rate = isfinite(input->motion_rate) && input->motion_rate > 0.0f ?
                               input->motion_rate : 1.0;
    motion->camera_phase = orbital_wrap(
        orbital_hash_unit(motion->seed, 1)*ORBITAL_TAU + time*camera_speed*motion_rate,
        ORBITAL_TAU);
    motion->travel_phase = orbital_wrap(
        orbital_hash_unit(motion->seed, 2)*ORBITAL_LATTICE_PATH_LENGTH +
        time*travel_speed*motion_rate, ORBITAL_LATTICE_PATH_LENGTH);
    motion->twist_phase = orbital_wrap(
        orbital_hash_unit(motion->seed, 3)*ORBITAL_TAU + time*twist_speed*motion_rate,
        ORBITAL_TAU);
    motion->hue_degrees = orbital_wrap(
        205.0 + orbital_hash_unit(motion->seed, 4)*110.0 + time*hue_speed*motion_rate,
        360.0);
}

bool orbital_lattice_motion_ring(
    const Orbital_Lattice_Motion *motion,
    size_t ring_index,
    Orbital_Lattice_Ring_Motion *ring)
{
    if (motion == NULL || ring == NULL || !motion->initialized ||
        ring_index >= ORBITAL_LATTICE_RING_COUNT) return false;

    float distance = (float)orbital_wrap(
        (double)ring_index*ORBITAL_LATTICE_RING_SPACING + motion->travel_phase,
        ORBITAL_LATTICE_PATH_LENGTH);
    float near_fade = orbital_smoothstep(0.0f, 0.80f, distance);
    float far_fade = 1.0f - orbital_smoothstep(
        ORBITAL_LATTICE_PATH_LENGTH - 3.0f,
        ORBITAL_LATTICE_PATH_LENGTH, distance);
    *ring = (Orbital_Lattice_Ring_Motion) {
        .distance = distance,
        .depth_t = distance/ORBITAL_LATTICE_PATH_LENGTH,
        .visibility = near_fade*far_fade,
    };
    return true;
}
