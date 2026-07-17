#include "scene_loom_weave.h"

#include <math.h>
#include <string.h>

static float loom_weave_clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static float loom_weave_band_target(const Loom_Weave_Input *input, size_t bin)
{
    if (input->bands == NULL || input->bands_count == 0) return 0.0f;
    size_t begin = bin*input->bands_count/LOOM_WEAVE_BINS;
    size_t end = (bin + 1U)*input->bands_count/LOOM_WEAVE_BINS;
    if (end <= begin) end = begin + 1U;
    if (end > input->bands_count) end = input->bands_count;
    float total = 0.0f;
    for (size_t i = begin; i < end; ++i) {
        total += loom_weave_clamp01(input->bands[i]);
    }
    return loom_weave_clamp01(total/(float)(end - begin)*1.5f);
}

static void loom_weave_register_onset(Loom_Weave *weave, bool onset)
{
    if (onset && !weave->onset_active) {
        weave->onset_pulse = 1.0f;
        weave->onset_serial += 1U;
    }
    weave->onset_active = onset;
}

static void loom_weave_rebase(Loom_Weave *weave, const Loom_Weave_Input *input,
                              double time)
{
    weave->energy = loom_weave_clamp01(input->rms*1.9f);
    weave->tension = loom_weave_clamp01(input->spectral_flux*4.5f);
    weave->onset_pulse = 0.0f;
    loom_weave_register_onset(weave, input->onset);
    for (size_t bin = 0; bin < LOOM_WEAVE_BINS; ++bin) {
        weave->profile[bin] = loom_weave_band_target(input, bin);
    }
    weave->last_time_seconds = time;
    weave->initialized = true;
}

// Slots whose whole span lies behind the playhead are stamped once with the
// current smoothed measurement and then left alone: the woven cloth is a
// stable record, so replaying or seeking backward never rewrites it.
static void loom_weave_stamp_columns(Loom_Weave *weave, double time,
                                     double duration)
{
    if (!isfinite(duration) || duration <= 0.0 || time <= 0.0) return;
    double passed = time/duration*(double)LOOM_WEAVE_SLOTS;
    size_t complete = passed >= (double)LOOM_WEAVE_SLOTS ? LOOM_WEAVE_SLOTS :
                      (size_t)passed;
    for (size_t slot = 0; slot < complete; ++slot) {
        Loom_Weave_Column *column = &weave->columns[slot];
        if (column->woven) continue;
        column->woven = true;
        column->energy = weave->energy;
        column->tension = weave->tension;
        memcpy(column->profile, weave->profile, sizeof(column->profile));
    }
}

void loom_weave_init(Loom_Weave *weave)
{
    if (weave == NULL) return;
    memset(weave, 0, sizeof(*weave));
}

void loom_weave_update(Loom_Weave *weave, const Loom_Weave_Input *input)
{
    if (weave == NULL || input == NULL) return;
    double time = isfinite(input->time_seconds) && input->time_seconds > 0.0 ?
                  input->time_seconds : 0.0;
    double elapsed = time - weave->last_time_seconds;
    float delta = isfinite(input->delta_seconds) &&
                  input->delta_seconds > 0.0f ? input->delta_seconds : 0.0f;
    bool discontinuity = !weave->initialized || elapsed < -0.001 ||
                         elapsed > 0.25 || delta > 0.20f ||
                         (fabs(elapsed) > 0.02 && delta == 0.0f);
    if (discontinuity) {
        loom_weave_rebase(weave, input, time);
        loom_weave_stamp_columns(weave, time, input->duration_seconds);
        return;
    }

    if (delta > 0.1f) delta = 0.1f;
    weave->last_time_seconds = time;
    if (delta > 0.0f) {
        // Envelopes rise quickly and relax slowly so hits land and decays
        // read; targets match the rebase scaling exactly.
        float energy = loom_weave_clamp01(input->rms*1.9f);
        float tension = loom_weave_clamp01(input->spectral_flux*4.5f);
        float energy_blend = 1.0f - expf((energy > weave->energy ?
                                          -10.0f : -3.0f)*delta);
        float tension_blend = 1.0f - expf((tension > weave->tension ?
                                           -14.0f : -4.0f)*delta);
        weave->energy += (energy - weave->energy)*energy_blend;
        weave->tension += (tension - weave->tension)*tension_blend;
        weave->onset_pulse *= expf(-4.0f*delta);
        for (size_t bin = 0; bin < LOOM_WEAVE_BINS; ++bin) {
            float target = loom_weave_band_target(input, bin);
            float blend = 1.0f - expf((target > weave->profile[bin] ?
                                       -12.0f : -4.5f)*delta);
            weave->profile[bin] += (target - weave->profile[bin])*blend;
        }
    }
    loom_weave_register_onset(weave, input->onset);
    loom_weave_stamp_columns(weave, time, input->duration_seconds);
}

size_t loom_weave_slot(double time_seconds, double duration_seconds)
{
    if (!isfinite(time_seconds) || !isfinite(duration_seconds) ||
        duration_seconds <= 0.0 || time_seconds <= 0.0) return 0;
    double position = time_seconds/duration_seconds*(double)LOOM_WEAVE_SLOTS;
    if (position >= (double)(LOOM_WEAVE_SLOTS - 1U)) {
        return LOOM_WEAVE_SLOTS - 1U;
    }
    return (size_t)position;
}

float loom_weave_profile_sample(const float profile[LOOM_WEAVE_BINS],
                                float band_t)
{
    if (profile == NULL || !isfinite(band_t)) return 0.0f;
    if (band_t <= 0.0f) return loom_weave_clamp01(profile[0]);
    if (band_t >= 1.0f) {
        return loom_weave_clamp01(profile[LOOM_WEAVE_BINS - 1U]);
    }
    float position = band_t*(float)(LOOM_WEAVE_BINS - 1U);
    size_t low = (size_t)position;
    size_t high = low + 1U < LOOM_WEAVE_BINS ? low + 1U : LOOM_WEAVE_BINS - 1U;
    float fraction = position - (float)low;
    return loom_weave_clamp01(profile[low] +
                              (profile[high] - profile[low])*fraction);
}
