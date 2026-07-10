#ifndef MUSIALIZER_AUDIO_FIXTURES_H
#define MUSIALIZER_AUDIO_FIXTURES_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float *samples;
    size_t frame_count;
    unsigned int sample_rate;
    unsigned int channel_count;
} Audio_Fixture;

bool audio_fixture_silence(Audio_Fixture *fixture, unsigned int sample_rate,
                           unsigned int channel_count, size_t frame_count);
bool audio_fixture_sine(Audio_Fixture *fixture, unsigned int sample_rate,
                        unsigned int channel_count, size_t frame_count,
                        float frequency_hz, float amplitude);
bool audio_fixture_sweep(Audio_Fixture *fixture, unsigned int sample_rate,
                         unsigned int channel_count, size_t frame_count,
                         float start_frequency_hz, float end_frequency_hz,
                         float amplitude);
bool audio_fixture_impulse(Audio_Fixture *fixture, unsigned int sample_rate,
                           unsigned int channel_count, size_t frame_count,
                           size_t impulse_frame, float amplitude);
bool audio_fixture_stereo_imbalance(Audio_Fixture *fixture, unsigned int sample_rate,
                                    size_t frame_count, float frequency_hz,
                                    float left_amplitude, float right_amplitude);
bool audio_fixture_beat(Audio_Fixture *fixture, unsigned int sample_rate,
                        unsigned int channel_count, size_t frame_count,
                        float beats_per_minute, float amplitude);
void audio_fixture_destroy(Audio_Fixture *fixture);

#endif
