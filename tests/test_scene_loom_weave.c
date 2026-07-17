#include "scene_loom_weave.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

enum { LOOM_TEST_BANDS = 64 };

static Loom_Weave_Input loom_input(double time_seconds, float delta_seconds,
                                   float rms, float flux, bool onset,
                                   const float *bands, size_t bands_count,
                                   double duration_seconds)
{
    return (Loom_Weave_Input) {
        .time_seconds = time_seconds,
        .duration_seconds = duration_seconds,
        .delta_seconds = delta_seconds,
        .bands = bands,
        .bands_count = bands_count,
        .rms = rms,
        .spectral_flux = flux,
        .onset = onset,
    };
}

static void loom_fill_bands(float *bands, float low, float high)
{
    for (size_t i = 0; i < LOOM_TEST_BANDS; ++i) {
        bands[i] = i < LOOM_TEST_BANDS/2 ? low : high;
    }
}

TEST(loom_weave_records_columns_once_as_the_fell_passes)
{
    float loud[LOOM_TEST_BANDS];
    float quiet[LOOM_TEST_BANDS];
    loom_fill_bands(loud, 0.8f, 0.05f);
    loom_fill_bands(quiet, 0.02f, 0.01f);
    const double duration = 14.4;  // 0.1 seconds per slot

    Loom_Weave weave;
    loom_weave_init(&weave);
    for (size_t frame = 0; frame < 60; ++frame) {
        Loom_Weave_Input input = loom_input(
            frame/60.0, frame == 0 ? 0.0f : 1.0f/60.0f,
            0.4f, 0.1f, false, loud, LOOM_TEST_BANDS, duration);
        loom_weave_update(&weave, &input);
    }
    // 59/60 of a second played: slots 0..8 are complete, slot 9 is not.
    EXPECT_TRUE(weave.columns[8].woven);
    EXPECT_FALSE(weave.columns[9].woven);
    float low_sample = loom_weave_profile_sample(weave.columns[8].profile, 0.1f);
    float high_sample = loom_weave_profile_sample(weave.columns[8].profile, 0.9f);
    EXPECT_TRUE(low_sample > 0.4f);
    EXPECT_TRUE(low_sample > high_sample);

    Loom_Weave_Column snapshot = weave.columns[8];
    for (size_t frame = 60; frame < 120; ++frame) {
        Loom_Weave_Input input = loom_input(
            frame/60.0, 1.0f/60.0f,
            0.01f, 0.0f, false, quiet, LOOM_TEST_BANDS, duration);
        loom_weave_update(&weave, &input);
    }
    // Already-woven cloth is a stable record: later audio never restamps it.
    EXPECT_TRUE(memcmp(&snapshot, &weave.columns[8], sizeof(snapshot)) == 0);
    EXPECT_TRUE(weave.columns[18].woven);
    EXPECT_TRUE(loom_weave_profile_sample(weave.columns[18].profile, 0.1f) <
                low_sample);
}

TEST(loom_weave_seeks_keep_the_woven_record)
{
    float bands[LOOM_TEST_BANDS];
    loom_fill_bands(bands, 0.6f, 0.3f);
    const double duration = 14.4;

    Loom_Weave weave;
    loom_weave_init(&weave);
    for (size_t frame = 0; frame <= 120; ++frame) {
        Loom_Weave_Input input = loom_input(
            frame/60.0, frame == 0 ? 0.0f : 1.0f/60.0f,
            0.3f, 0.05f, false, bands, LOOM_TEST_BANDS, duration);
        loom_weave_update(&weave, &input);
    }
    EXPECT_TRUE(weave.columns[19].woven);
    Loom_Weave_Column snapshot = weave.columns[10];

    // Seeking backward is a transport discontinuity: envelopes rebase, but
    // the tapestry behind the old fell survives untouched.
    Loom_Weave_Input back = loom_input(0.5, 0.0f, 0.9f, 0.2f, true,
                                       bands, LOOM_TEST_BANDS, duration);
    loom_weave_update(&weave, &back);
    EXPECT_TRUE(weave.columns[19].woven);
    EXPECT_TRUE(memcmp(&snapshot, &weave.columns[10], sizeof(snapshot)) == 0);
    EXPECT_NEAR(weave.energy, fminf(1.0f, 0.9f*1.9f), 0.0001f);

    // Seeking far forward stamps the skipped slots so the cloth never shows
    // holes; they take the rebased measurement as their best evidence.
    Loom_Weave_Input forward = loom_input(10.0, 0.0f, 0.2f, 0.02f, false,
                                          bands, LOOM_TEST_BANDS, duration);
    loom_weave_update(&weave, &forward);
    for (size_t slot = 0; slot < 100; ++slot) {
        EXPECT_TRUE(weave.columns[slot].woven);
    }
    EXPECT_FALSE(weave.columns[100].woven);
}

TEST(loom_weave_onset_pulse_tracks_rising_edges)
{
    Loom_Weave weave;
    loom_weave_init(&weave);
    uint32_t serial_before = 0;
    for (size_t frame = 0; frame < 30; ++frame) {
        // The analyzer keeps the onset flag high across a sustained hit;
        // only the rising edge may start a new burst.
        bool onset = frame >= 10 && frame < 15;
        Loom_Weave_Input input = loom_input(
            frame/60.0, frame == 0 ? 0.0f : 1.0f/60.0f,
            0.2f, 0.05f, onset, NULL, 0, 60.0);
        loom_weave_update(&weave, &input);
        if (frame == 9) serial_before = weave.onset_serial;
        if (frame == 10) {
            EXPECT_EQ_U64(weave.onset_serial, serial_before + 1U);
            EXPECT_NEAR(weave.onset_pulse, 1.0f, 0.0001f);
        }
    }
    EXPECT_EQ_U64(weave.onset_serial, serial_before + 1U);
    EXPECT_TRUE(weave.onset_pulse < 1.0f && weave.onset_pulse > 0.0f);

    Loom_Weave_Input next = loom_input(0.5, 1.0f/60.0f, 0.2f, 0.05f, true,
                                       NULL, 0, 60.0);
    loom_weave_update(&weave, &next);
    EXPECT_EQ_U64(weave.onset_serial, serial_before + 2U);
}

TEST(loom_weave_is_deterministic_and_bounded_under_hostile_input)
{
    Loom_Weave first;
    Loom_Weave second;
    loom_weave_init(&first);
    loom_weave_init(&second);
    float bands[LOOM_TEST_BANDS];
    for (size_t frame = 0; frame < 240; ++frame) {
        loom_fill_bands(bands, frame%2 ? 3.0f : -1.0f,
                        frame%3 ? NAN : 0.4f);
        Loom_Weave_Input input = loom_input(
            frame/60.0, frame == 0 ? 0.0f : 1.0f/60.0f,
            frame%2 ? 1.5f : -0.5f,
            frame%3 ? 0.4f : NAN,
            frame%37 == 0, bands, LOOM_TEST_BANDS, 4.0);
        loom_weave_update(&first, &input);
        loom_weave_update(&second, &input);
        EXPECT_TRUE(first.energy >= 0.0f && first.energy <= 1.0f);
        EXPECT_TRUE(first.tension >= 0.0f && first.tension <= 1.0f);
        EXPECT_TRUE(first.onset_pulse >= 0.0f && first.onset_pulse <= 1.0f);
        for (size_t bin = 0; bin < LOOM_WEAVE_BINS; ++bin) {
            EXPECT_TRUE(first.profile[bin] >= 0.0f &&
                        first.profile[bin] <= 1.0f);
        }
    }
    EXPECT_TRUE(memcmp(&first, &second, sizeof(first)) == 0);
    for (size_t slot = 0; slot < LOOM_WEAVE_SLOTS; ++slot) {
        if (!first.columns[slot].woven) continue;
        EXPECT_TRUE(first.columns[slot].energy >= 0.0f &&
                    first.columns[slot].energy <= 1.0f);
    }
}

TEST(loom_weave_slot_and_profile_sample_clamp_edges)
{
    EXPECT_EQ_SIZE(loom_weave_slot(-1.0, 10.0), 0);
    EXPECT_EQ_SIZE(loom_weave_slot(5.0, 0.0), 0);
    EXPECT_EQ_SIZE(loom_weave_slot(5.0, -3.0), 0);
    EXPECT_EQ_SIZE(loom_weave_slot(NAN, 10.0), 0);
    EXPECT_EQ_SIZE(loom_weave_slot(20.0, 10.0), LOOM_WEAVE_SLOTS - 1U);
    EXPECT_EQ_SIZE(loom_weave_slot(10.0, 10.0), LOOM_WEAVE_SLOTS - 1U);
    EXPECT_EQ_SIZE(loom_weave_slot(0.0, 10.0), 0);

    float profile[LOOM_WEAVE_BINS];
    for (size_t bin = 0; bin < LOOM_WEAVE_BINS; ++bin) {
        profile[bin] = (float)bin/(float)(LOOM_WEAVE_BINS - 1U);
    }
    EXPECT_NEAR(loom_weave_profile_sample(profile, 0.0f), 0.0f, 0.0001f);
    EXPECT_NEAR(loom_weave_profile_sample(profile, 1.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(loom_weave_profile_sample(profile, 0.5f), 0.5f, 0.03f);
    EXPECT_NEAR(loom_weave_profile_sample(profile, -2.0f), 0.0f, 0.0001f);
    EXPECT_NEAR(loom_weave_profile_sample(profile, NAN), 0.0f, 0.0001f);
    EXPECT_NEAR(loom_weave_profile_sample(NULL, 0.5f), 0.0f, 0.0001f);
}
