#include "audio_fixtures.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define AUDIO_FIXTURE_PI 3.14159265358979323846

static bool audio_fixture_allocate(Audio_Fixture *fixture, unsigned int sample_rate,
                                   unsigned int channel_count, size_t frame_count)
{
    if (fixture == NULL || fixture->samples != NULL || sample_rate == 0 ||
        channel_count == 0 || frame_count == 0 ||
        frame_count > SIZE_MAX / channel_count ||
        frame_count * channel_count > SIZE_MAX / sizeof(float)) {
        return false;
    }

    float *samples = calloc(frame_count * channel_count, sizeof(*samples));
    if (samples == NULL) return false;
    *fixture = (Audio_Fixture) {
        .samples = samples,
        .frame_count = frame_count,
        .sample_rate = sample_rate,
        .channel_count = channel_count,
    };
    return true;
}

static bool valid_signal(float frequency_hz, float amplitude)
{
    return isfinite(frequency_hz) && frequency_hz >= 0.0f &&
           isfinite(amplitude) && fabsf(amplitude) <= 1.0f;
}

bool audio_fixture_silence(Audio_Fixture *fixture, unsigned int sample_rate,
                           unsigned int channel_count, size_t frame_count)
{
    return audio_fixture_allocate(fixture, sample_rate, channel_count, frame_count);
}

bool audio_fixture_sine(Audio_Fixture *fixture, unsigned int sample_rate,
                        unsigned int channel_count, size_t frame_count,
                        float frequency_hz, float amplitude)
{
    if (!valid_signal(frequency_hz, amplitude) || frequency_hz > sample_rate * 0.5f ||
        !audio_fixture_allocate(fixture, sample_rate, channel_count, frame_count)) {
        return false;
    }
    for (size_t frame = 0; frame < frame_count; ++frame) {
        float sample = amplitude * sinf((float) (2.0 * AUDIO_FIXTURE_PI * frequency_hz *
                                                  (double) frame / sample_rate));
        for (unsigned int channel = 0; channel < channel_count; ++channel) {
            fixture->samples[frame * channel_count + channel] = sample;
        }
    }
    return true;
}

bool audio_fixture_sweep(Audio_Fixture *fixture, unsigned int sample_rate,
                         unsigned int channel_count, size_t frame_count,
                         float start_frequency_hz, float end_frequency_hz,
                         float amplitude)
{
    if (!valid_signal(start_frequency_hz, amplitude) ||
        !valid_signal(end_frequency_hz, amplitude) ||
        start_frequency_hz > sample_rate * 0.5f || end_frequency_hz > sample_rate * 0.5f ||
        !audio_fixture_allocate(fixture, sample_rate, channel_count, frame_count)) {
        return false;
    }

    double phase = 0.0;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        double progress = frame_count > 1 ? (double) frame / (double) (frame_count - 1) : 0.0;
        double frequency = start_frequency_hz + (end_frequency_hz - start_frequency_hz) * progress;
        float sample = amplitude * sinf((float) phase);
        for (unsigned int channel = 0; channel < channel_count; ++channel) {
            fixture->samples[frame * channel_count + channel] = sample;
        }
        phase += 2.0 * AUDIO_FIXTURE_PI * frequency / sample_rate;
        phase = fmod(phase, 2.0 * AUDIO_FIXTURE_PI);
    }
    return true;
}

bool audio_fixture_impulse(Audio_Fixture *fixture, unsigned int sample_rate,
                           unsigned int channel_count, size_t frame_count,
                           size_t impulse_frame, float amplitude)
{
    if (!valid_signal(0.0f, amplitude) || impulse_frame >= frame_count ||
        !audio_fixture_allocate(fixture, sample_rate, channel_count, frame_count)) {
        return false;
    }
    for (unsigned int channel = 0; channel < channel_count; ++channel) {
        fixture->samples[impulse_frame * channel_count + channel] = amplitude;
    }
    return true;
}

bool audio_fixture_stereo_imbalance(Audio_Fixture *fixture, unsigned int sample_rate,
                                    size_t frame_count, float frequency_hz,
                                    float left_amplitude, float right_amplitude)
{
    if (!valid_signal(frequency_hz, left_amplitude) ||
        !valid_signal(frequency_hz, right_amplitude) || frequency_hz > sample_rate * 0.5f ||
        !audio_fixture_allocate(fixture, sample_rate, 2, frame_count)) {
        return false;
    }
    for (size_t frame = 0; frame < frame_count; ++frame) {
        float wave = sinf((float) (2.0 * AUDIO_FIXTURE_PI * frequency_hz *
                                    (double) frame / sample_rate));
        fixture->samples[frame * 2] = left_amplitude * wave;
        fixture->samples[frame * 2 + 1] = right_amplitude * wave;
    }
    return true;
}

bool audio_fixture_beat(Audio_Fixture *fixture, unsigned int sample_rate,
                        unsigned int channel_count, size_t frame_count,
                        float beats_per_minute, float amplitude)
{
    if (!isfinite(beats_per_minute) || beats_per_minute <= 0.0f ||
        !valid_signal(0.0f, amplitude) ||
        !audio_fixture_allocate(fixture, sample_rate, channel_count, frame_count)) {
        return false;
    }

    size_t beat_period = (size_t) llround(60.0 * sample_rate / beats_per_minute);
    if (beat_period == 0) beat_period = 1;
    size_t pulse_frames = sample_rate / 200; // A short, click-like 5 ms envelope.
    if (pulse_frames == 0) pulse_frames = 1;

    for (size_t beat = 0; beat < frame_count; beat += beat_period) {
        for (size_t offset = 0; offset < pulse_frames && beat + offset < frame_count; ++offset) {
            float envelope = amplitude * (1.0f - (float) offset / pulse_frames);
            for (unsigned int channel = 0; channel < channel_count; ++channel) {
                fixture->samples[(beat + offset) * channel_count + channel] = envelope;
            }
        }
    }
    return true;
}

void audio_fixture_destroy(Audio_Fixture *fixture)
{
    if (fixture == NULL) return;
    free(fixture->samples);
    *fixture = (Audio_Fixture) {0};
}
