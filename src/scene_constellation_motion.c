#include "scene_constellation_motion.h"

#include <math.h>
#include <string.h>

static float constellation_motion_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static void constellation_motion_rebase(
    Constellation_Motion *motion,
    const Constellation_Motion_Input *input)
{
    motion->energy = constellation_motion_clamp01(input->rms*1.8f);
    motion->flux = constellation_motion_clamp01(input->spectral_flux*5.0f);
    motion->onset_pulse = input->onset ? 1.0f : 0.0f;
    motion->last_time_seconds = isfinite(input->time_seconds) &&
                                input->time_seconds > 0.0 ?
                                input->time_seconds : 0.0;
    motion->initialized = true;
}

void constellation_motion_init(Constellation_Motion *motion)
{
    if (motion == NULL) return;
    memset(motion, 0, sizeof(*motion));
}

void constellation_motion_update(Constellation_Motion *motion,
                                 const Constellation_Motion_Input *input)
{
    if (motion == NULL || input == NULL) return;
    double time = isfinite(input->time_seconds) && input->time_seconds > 0.0 ?
                  input->time_seconds : 0.0;
    double elapsed = time - motion->last_time_seconds;
    float delta = isfinite(input->delta_seconds) && input->delta_seconds > 0.0f ?
                  input->delta_seconds : 0.0f;
    bool discontinuity = !motion->initialized || elapsed < -0.001 ||
                         elapsed > 0.25 || delta > 0.20f ||
                         (fabs(elapsed) > 0.02 && delta == 0.0f);
    if (discontinuity) {
        constellation_motion_rebase(motion, input);
        return;
    }

    if (delta > 0.1f) delta = 0.1f;
    motion->last_time_seconds = time;
    if (delta <= 0.0f) return;

    float blend = 1.0f - expf(-5.0f*delta);
    float energy = constellation_motion_clamp01(input->rms*1.8f);
    float flux = constellation_motion_clamp01(input->spectral_flux*5.0f);
    motion->energy += (energy - motion->energy)*blend;
    motion->flux += (flux - motion->flux)*blend;
    motion->onset_pulse *= expf(-7.0f*delta);
    if (input->onset) motion->onset_pulse = 1.0f;
}
