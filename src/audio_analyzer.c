#include "audio_analyzer.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static AudioComplex complex_add(AudioComplex a, AudioComplex b)
{
    return (AudioComplex){a.real + b.real, a.imaginary + b.imaginary};
}

static AudioComplex complex_sub(AudioComplex a, AudioComplex b)
{
    return (AudioComplex){a.real - b.real, a.imaginary - b.imaginary};
}

static AudioComplex complex_mul(AudioComplex a, AudioComplex b)
{
    return (AudioComplex){
        a.real*b.real - a.imaginary*b.imaginary,
        a.real*b.imaginary + a.imaginary*b.real,
    };
}

/* Ported from the legacy analyzer (originally cp-algorithms). */
static void fft(const float *input, AudioComplex *output, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        output[i] = (AudioComplex){input[i], 0.0f};
    }

    for (size_t i = 1, j = 0; i < count; ++i) {
        size_t bit = count >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            AudioComplex temporary = output[i];
            output[i] = output[j];
            output[j] = temporary;
        }
    }

    for (size_t length = 2; length <= count; length <<= 1) {
        float angle = (float)(2.0*M_PI)/(float)length;
        AudioComplex step = {cosf(angle), sinf(angle)};
        for (size_t i = 0; i < count; i += length) {
            AudioComplex weight = {1.0f, 0.0f};
            for (size_t j = 0; j < length/2; ++j) {
                AudioComplex even = output[i + j];
                AudioComplex odd = complex_mul(output[i + j + length/2], weight);
                output[i + j] = complex_add(even, odd);
                output[i + j + length/2] = complex_sub(even, odd);
                weight = complex_mul(weight, step);
            }
        }
    }
}

static float legacy_amplitude(AudioComplex value)
{
    return logf(value.real*value.real + value.imaginary*value.imaginary);
}

static bool analyzer_state_valid(const AudioAnalyzer *analyzer)
{
    if (analyzer == NULL || analyzer->config.sample_rate == 0 ||
        analyzer->config.channel_count == 0 ||
        analyzer->write_cursor >= AUDIO_ANALYZER_FFT_SIZE ||
        analyzer->sample_count > AUDIO_ANALYZER_FFT_SIZE ||
        analyzer->band_count > AUDIO_ANALYZER_MAX_BANDS) {
        return false;
    }
    if (analyzer->config.channel_mode == AUDIO_ANALYZER_CHANNEL_MIX) return true;
    return analyzer->config.channel_mode == AUDIO_ANALYZER_CHANNEL_SELECT &&
        analyzer->config.selected_channel < analyzer->config.channel_count;
}

bool audio_analyzer_init(AudioAnalyzer *analyzer, AudioAnalyzerConfig config)
{
    if (analyzer == NULL || config.sample_rate == 0 || config.channel_count == 0) {
        return false;
    }
    if (config.channel_mode != AUDIO_ANALYZER_CHANNEL_MIX &&
        config.channel_mode != AUDIO_ANALYZER_CHANNEL_SELECT) {
        return false;
    }
    if (config.channel_mode == AUDIO_ANALYZER_CHANNEL_SELECT &&
        config.selected_channel >= config.channel_count) {
        return false;
    }

    memset(analyzer, 0, sizeof(*analyzer));
    analyzer->config = config;
    return true;
}

void audio_analyzer_reset(AudioAnalyzer *analyzer)
{
    if (analyzer == NULL) return;
    AudioAnalyzerConfig config = analyzer->config;
    memset(analyzer, 0, sizeof(*analyzer));
    analyzer->config = config;
}

static void push_one(AudioAnalyzer *analyzer, float sample)
{
    analyzer->input[analyzer->write_cursor] = sample;
    analyzer->write_cursor = (analyzer->write_cursor + 1)%AUDIO_ANALYZER_FFT_SIZE;
    if (analyzer->sample_count < AUDIO_ANALYZER_FFT_SIZE) {
        analyzer->sample_count += 1;
    }
}

size_t audio_analyzer_push_mono(AudioAnalyzer *analyzer,
                                const float *samples,
                                size_t sample_count)
{
    if (!analyzer_state_valid(analyzer) || (samples == NULL && sample_count != 0)) return 0;
    for (size_t i = 0; i < sample_count; ++i) push_one(analyzer, samples[i]);
    return sample_count;
}

size_t audio_analyzer_push_interleaved(AudioAnalyzer *analyzer,
                                       const float *samples,
                                       size_t frame_count)
{
    if (!analyzer_state_valid(analyzer) ||
        (samples == NULL && frame_count != 0)) {
        return 0;
    }

    size_t channels = analyzer->config.channel_count;
    if (frame_count > SIZE_MAX/channels) return 0;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        const float *source = samples + frame*channels;
        float mono = 0.0f;
        if (analyzer->config.channel_mode == AUDIO_ANALYZER_CHANNEL_SELECT) {
            mono = source[analyzer->config.selected_channel];
        } else {
            for (size_t channel = 0; channel < channels; ++channel) mono += source[channel];
            mono /= (float)channels;
        }
        push_one(analyzer, mono);
    }
    return frame_count;
}

static void prepare_window(AudioAnalyzer *analyzer)
{
    const size_t zero_count = AUDIO_ANALYZER_FFT_SIZE - analyzer->sample_count;
    for (size_t i = 0; i < zero_count; ++i) analyzer->windowed[i] = 0.0f;

    size_t oldest = analyzer->sample_count == AUDIO_ANALYZER_FFT_SIZE
        ? analyzer->write_cursor : 0;
    for (size_t i = 0; i < analyzer->sample_count; ++i) {
        size_t source = (oldest + i)%AUDIO_ANALYZER_FFT_SIZE;
        size_t destination = zero_count + i;
        float t = (float)destination/(float)(AUDIO_ANALYZER_FFT_SIZE - 1);
        float hann = 0.5f - 0.5f*cosf((float)(2.0*M_PI)*t);
        analyzer->windowed[destination] = analyzer->input[source]*hann;
    }
}

bool audio_analyzer_analyze(AudioAnalyzer *analyzer, float dt_seconds)
{
    if (!analyzer_state_valid(analyzer) ||
        !isfinite(dt_seconds) || dt_seconds < 0.0f) {
        return false;
    }
    // The legacy recurrence is stable only while 8*dt <= 1. Preview stalls
    // are discontinuities, not permission to overshoot normalized bands.
    if (dt_seconds > 0.1f) dt_seconds = 0.1f;

    prepare_window(analyzer);
    fft(analyzer->windowed, analyzer->fft, AUDIO_ANALYZER_FFT_SIZE);

    const float step = 1.06f;
    size_t band_count = 0;
    float maximum = 1.0f;
    for (float frequency_bin = 1.0f;
         (size_t)frequency_bin < AUDIO_ANALYZER_FFT_SIZE/2;
         frequency_bin = ceilf(frequency_bin*step)) {
        if (band_count >= AUDIO_ANALYZER_MAX_BANDS) return false;
        float next = ceilf(frequency_bin*step);
        size_t first = (size_t)frequency_bin;
        size_t end = (size_t)next;
        if (end > AUDIO_ANALYZER_FFT_SIZE/2) end = AUDIO_ANALYZER_FFT_SIZE/2;
        float amplitude = 0.0f;
        for (size_t bin = first; bin < end; ++bin) {
            float candidate = legacy_amplitude(analyzer->fft[bin]);
            if (candidate > amplitude) amplitude = candidate;
        }
        if (amplitude > maximum) maximum = amplitude;
        analyzer->logarithmic[band_count] = amplitude;
        analyzer->band_first_bin[band_count] = (uint16_t)first;
        analyzer->band_end_bin[band_count] = (uint16_t)end;
        band_count += 1;
    }

    for (size_t i = 0; i < band_count; ++i) {
        analyzer->logarithmic[i] /= maximum;
        analyzer->smooth[i] += (analyzer->logarithmic[i] - analyzer->smooth[i])*8.0f*dt_seconds;
        analyzer->smear[i] += (analyzer->smooth[i] - analyzer->smear[i])*3.0f*dt_seconds;
    }
    analyzer->band_count = band_count;
    return true;
}

AudioSpectrumView audio_analyzer_spectrum(const AudioAnalyzer *analyzer)
{
    if (!analyzer_state_valid(analyzer)) return (AudioSpectrumView){0};
    return (AudioSpectrumView){
        .logarithmic = analyzer->logarithmic,
        .smooth = analyzer->smooth,
        .smear = analyzer->smear,
        .band_first_bin = analyzer->band_first_bin,
        .band_end_bin = analyzer->band_end_bin,
        .band_count = analyzer->band_count,
        .sample_rate = analyzer->config.sample_rate,
    };
}

bool audio_analyzer_settled(const AudioAnalyzer *analyzer, float epsilon)
{
    if (!analyzer_state_valid(analyzer) || !isfinite(epsilon) || epsilon < 0.0f) return false;
    for (size_t i = 0; i < analyzer->band_count; ++i) {
        if (fabsf(analyzer->smooth[i]) > epsilon || fabsf(analyzer->smear[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

float audio_analyzer_bin_frequency(const AudioAnalyzer *analyzer, size_t bin)
{
    if (!analyzer_state_valid(analyzer) ||
        bin > AUDIO_ANALYZER_FFT_SIZE/2) {
        return 0.0f;
    }
    return (float)bin*(float)analyzer->config.sample_rate/(float)AUDIO_ANALYZER_FFT_SIZE;
}
