#include "audio_analyzer.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

#define TEST_SAMPLE_RATE 44100u

static void make_sine(float *samples, size_t count, float frequency, float amplitude)
{
    const float tau = 6.28318530717958647692f;
    for (size_t i = 0; i < count; ++i) {
        samples[i] = amplitude*sinf(tau*frequency*(float)i/(float)TEST_SAMPLE_RATE);
    }
}

static size_t strongest_band(AudioSpectrumView spectrum)
{
    size_t strongest = 0;
    for (size_t i = 1; i < spectrum.band_count; ++i) {
        if (spectrum.logarithmic[i] > spectrum.logarithmic[strongest]) strongest = i;
    }
    return strongest;
}

TEST(audio_analyzer_rejects_invalid_configuration_and_input)
{
    AudioAnalyzer analyzer;
    EXPECT_FALSE(audio_analyzer_init(NULL, (AudioAnalyzerConfig){44100, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    EXPECT_FALSE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){0, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    EXPECT_FALSE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){44100, 0, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    EXPECT_FALSE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){44100, 2, AUDIO_ANALYZER_CHANNEL_SELECT, 2}));
    EXPECT_FALSE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){44100, 2, (AudioAnalyzerChannelMode)99, 0}));

    REQUIRE_TRUE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){44100, 2, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    EXPECT_EQ_SIZE(audio_analyzer_push_interleaved(&analyzer, NULL, 1), 0);
    EXPECT_EQ_SIZE(audio_analyzer_push_mono(&analyzer, NULL, 1), 0);
    EXPECT_FALSE(audio_analyzer_analyze(&analyzer, -0.1f));
    EXPECT_FALSE(audio_analyzer_analyze(&analyzer, NAN));
    EXPECT_NEAR(audio_analyzer_bin_frequency(&analyzer, AUDIO_ANALYZER_FFT_SIZE), 0.0f, 0.0f);
}

TEST(audio_analyzer_silence_and_short_input_are_stable)
{
    AudioAnalyzer analyzer;
    REQUIRE_TRUE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    float one_sample = 0.0f;
    EXPECT_EQ_SIZE(audio_analyzer_push_mono(&analyzer, &one_sample, 1), 1);
    REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 1.0f/60.0f));

    AudioSpectrumView spectrum = audio_analyzer_spectrum(&analyzer);
    EXPECT_TRUE(spectrum.band_count > 0);
    EXPECT_TRUE(spectrum.band_count <= AUDIO_ANALYZER_MAX_BANDS);
    for (size_t i = 0; i < spectrum.band_count; ++i) {
        EXPECT_NEAR(spectrum.logarithmic[i], 0.0f, 0.0f);
        EXPECT_NEAR(spectrum.smooth[i], 0.0f, 0.0f);
        EXPECT_NEAR(spectrum.smear[i], 0.0f, 0.0f);
    }
    EXPECT_TRUE(audio_analyzer_settled(&analyzer, 1e-3f));
}

TEST(audio_analyzer_localizes_a_sine_in_the_expected_log_band)
{
    AudioAnalyzer analyzer;
    float samples[AUDIO_ANALYZER_FFT_SIZE];
    make_sine(samples, AUDIO_ANALYZER_FFT_SIZE, 440.0f, 0.8f);

    REQUIRE_TRUE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    EXPECT_EQ_SIZE(audio_analyzer_push_mono(&analyzer, samples, AUDIO_ANALYZER_FFT_SIZE), AUDIO_ANALYZER_FFT_SIZE);
    REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 1.0f/60.0f));

    AudioSpectrumView spectrum = audio_analyzer_spectrum(&analyzer);
    size_t band = strongest_band(spectrum);
    float lower = audio_analyzer_bin_frequency(&analyzer, spectrum.band_first_bin[band]);
    float upper = audio_analyzer_bin_frequency(&analyzer, spectrum.band_end_bin[band]);
    EXPECT_TRUE(lower <= 440.0f);
    EXPECT_TRUE(upper >= 440.0f);
    EXPECT_TRUE(spectrum.logarithmic[band] > 0.99f);
}

TEST(audio_analyzer_channel_mix_and_selection_are_explicit)
{
    AudioAnalyzer mixed;
    AudioAnalyzer selected;
    float stereo[AUDIO_ANALYZER_FFT_SIZE*2];
    const float tau = 6.28318530717958647692f;
    for (size_t i = 0; i < AUDIO_ANALYZER_FFT_SIZE; ++i) {
        float value = sinf(tau*880.0f*(float)i/(float)TEST_SAMPLE_RATE);
        stereo[i*2] = value;
        stereo[i*2 + 1] = -value;
    }

    REQUIRE_TRUE(audio_analyzer_init(&mixed, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 2, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    REQUIRE_TRUE(audio_analyzer_init(&selected, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 2, AUDIO_ANALYZER_CHANNEL_SELECT, 0}));
    EXPECT_EQ_SIZE(audio_analyzer_push_interleaved(&mixed, stereo, AUDIO_ANALYZER_FFT_SIZE), AUDIO_ANALYZER_FFT_SIZE);
    EXPECT_EQ_SIZE(audio_analyzer_push_interleaved(&selected, stereo, AUDIO_ANALYZER_FFT_SIZE), AUDIO_ANALYZER_FFT_SIZE);
    REQUIRE_TRUE(audio_analyzer_analyze(&mixed, 1.0f/60.0f));
    REQUIRE_TRUE(audio_analyzer_analyze(&selected, 1.0f/60.0f));

    AudioSpectrumView mixed_spectrum = audio_analyzer_spectrum(&mixed);
    AudioSpectrumView selected_spectrum = audio_analyzer_spectrum(&selected);
    EXPECT_NEAR(mixed_spectrum.logarithmic[strongest_band(mixed_spectrum)], 0.0f, 0.0f);
    EXPECT_TRUE(selected_spectrum.logarithmic[strongest_band(selected_spectrum)] > 0.99f);
}

TEST(audio_analyzer_impulse_smoothing_matches_legacy_recurrence)
{
    AudioAnalyzer analyzer;
    float impulse[AUDIO_ANALYZER_FFT_SIZE] = {0};
    impulse[AUDIO_ANALYZER_FFT_SIZE/2] = 1.0f;
    REQUIRE_TRUE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    audio_analyzer_push_mono(&analyzer, impulse, AUDIO_ANALYZER_FFT_SIZE);
    REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 0.01f));

    AudioSpectrumView first = audio_analyzer_spectrum(&analyzer);
    size_t band = strongest_band(first);
    float logarithmic = first.logarithmic[band];
    float smooth = first.smooth[band];
    float smear = first.smear[band];
    REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 0.01f));
    AudioSpectrumView second = audio_analyzer_spectrum(&analyzer);
    EXPECT_NEAR(second.smooth[band], smooth + (logarithmic - smooth)*0.08f, 1e-6f);
    EXPECT_NEAR(second.smear[band], smear + (second.smooth[band] - smear)*0.03f, 1e-6f);
}

TEST(audio_analyzer_preview_stall_does_not_overshoot_normalized_state)
{
    AudioAnalyzer analyzer;
    float sine[AUDIO_ANALYZER_FFT_SIZE];
    make_sine(sine, AUDIO_ANALYZER_FFT_SIZE, 440.0f, 0.9f);
    REQUIRE_TRUE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    audio_analyzer_push_mono(&analyzer, sine, AUDIO_ANALYZER_FFT_SIZE);
    REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 0.5f));
    AudioSpectrumView spectrum = audio_analyzer_spectrum(&analyzer);
    for (size_t i = 0; i < spectrum.band_count; ++i) {
        EXPECT_TRUE(isfinite(spectrum.smooth[i]));
        EXPECT_TRUE(isfinite(spectrum.smear[i]));
        EXPECT_TRUE(spectrum.smooth[i] >= 0.0f && spectrum.smooth[i] <= 1.0f);
        EXPECT_TRUE(spectrum.smear[i] >= 0.0f && spectrum.smear[i] <= 1.0f);
    }
}

TEST(audio_analyzer_circular_window_keeps_only_the_latest_samples)
{
    AudioAnalyzer direct;
    AudioAnalyzer wrapped;
    float silence[AUDIO_ANALYZER_FFT_SIZE] = {0};
    float sine[AUDIO_ANALYZER_FFT_SIZE];
    make_sine(sine, AUDIO_ANALYZER_FFT_SIZE, 1000.0f, 0.7f);
    AudioAnalyzerConfig config = {TEST_SAMPLE_RATE, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0};
    REQUIRE_TRUE(audio_analyzer_init(&direct, config));
    REQUIRE_TRUE(audio_analyzer_init(&wrapped, config));
    audio_analyzer_push_mono(&direct, sine, AUDIO_ANALYZER_FFT_SIZE);
    audio_analyzer_push_mono(&wrapped, silence, AUDIO_ANALYZER_FFT_SIZE);
    audio_analyzer_push_mono(&wrapped, sine, AUDIO_ANALYZER_FFT_SIZE);
    REQUIRE_TRUE(audio_analyzer_analyze(&direct, 1.0f/30.0f));
    REQUIRE_TRUE(audio_analyzer_analyze(&wrapped, 1.0f/30.0f));
    AudioSpectrumView a = audio_analyzer_spectrum(&direct);
    AudioSpectrumView b = audio_analyzer_spectrum(&wrapped);
    EXPECT_EQ_SIZE(a.band_count, b.band_count);
    EXPECT_TRUE(memcmp(a.logarithmic, b.logarithmic, a.band_count*sizeof(float)) == 0);
    EXPECT_TRUE(memcmp(a.smooth, b.smooth, a.band_count*sizeof(float)) == 0);
    EXPECT_TRUE(memcmp(a.smear, b.smear, a.band_count*sizeof(float)) == 0);
}

TEST(audio_analyzer_offline_silence_tail_reaches_settlement)
{
    AudioAnalyzer analyzer;
    float sine[AUDIO_ANALYZER_FFT_SIZE];
    float silence[TEST_SAMPLE_RATE/30] = {0};
    make_sine(sine, AUDIO_ANALYZER_FFT_SIZE, 220.0f, 0.9f);
    REQUIRE_TRUE(audio_analyzer_init(&analyzer, (AudioAnalyzerConfig){TEST_SAMPLE_RATE, 1, AUDIO_ANALYZER_CHANNEL_MIX, 0}));
    audio_analyzer_push_mono(&analyzer, sine, AUDIO_ANALYZER_FFT_SIZE);
    REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 1.0f/30.0f));
    EXPECT_FALSE(audio_analyzer_settled(&analyzer, 1e-3f));

    size_t tail_frames = 0;
    while (!audio_analyzer_settled(&analyzer, 1e-3f) && tail_frames < 300) {
        audio_analyzer_push_mono(&analyzer, silence, TEST_SAMPLE_RATE/30);
        REQUIRE_TRUE(audio_analyzer_analyze(&analyzer, 1.0f/30.0f));
        tail_frames += 1;
    }
    EXPECT_TRUE(audio_analyzer_settled(&analyzer, 1e-3f));
    EXPECT_TRUE(tail_frames > 0);
    EXPECT_TRUE(tail_frames < 300);
}
