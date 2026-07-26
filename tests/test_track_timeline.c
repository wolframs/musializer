#include "track_timeline.h"
#include "test_support.h"

#include <math.h>

TEST(track_timeline_waveform_preserves_envelope_and_normalizes_peak)
{
    const float samples[] = {
        -0.25f, 0.10f,
         0.50f, 0.20f,
        -0.75f, 0.40f,
         0.25f, 0.10f,
    };
    Track_Timeline_Bin bins[2] = {0};
    REQUIRE_TRUE(track_timeline_build_waveform(samples, 4, 2, bins, 2) == 2);
    EXPECT_NEAR(bins[0].minimum, -1.0f/3.0f, 0.0001f);
    EXPECT_NEAR(bins[0].maximum, 2.0f/3.0f, 0.0001f);
    EXPECT_NEAR(bins[1].minimum, -1.0f, 0.0001f);
    EXPECT_NEAR(bins[1].maximum, 1.0f/1.875f, 0.0001f);
}

TEST(track_timeline_waveform_handles_silence_nonfinite_and_invalid_input)
{
    const float samples[] = {0.0f, NAN, INFINITY, -INFINITY};
    Track_Timeline_Bin bins[4] = {{99.0f, 99.0f}};
    REQUIRE_TRUE(track_timeline_build_waveform(samples, 4, 1, bins, 4) == 4);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(bins[i].minimum, 0.0f, 0.0f);
        EXPECT_NEAR(bins[i].maximum, 0.0f, 0.0f);
    }
    EXPECT_EQ_SIZE(track_timeline_build_waveform(NULL, 4, 1, bins, 4), 0);
    EXPECT_EQ_SIZE(track_timeline_build_waveform(samples, 4, 0, bins, 4), 0);
    EXPECT_EQ_SIZE(track_timeline_build_waveform(samples, 4, 1, NULL, 4), 0);
}

TEST(track_timeline_relative_seek_is_exact_and_clamped)
{
    EXPECT_NEAR(track_timeline_seek_relative(12.5, 0.1, 60.0), 12.6, 0.0000001);
    EXPECT_NEAR(track_timeline_seek_relative(12.5, -1.0, 60.0), 11.5, 0.0);
    EXPECT_NEAR(track_timeline_seek_relative(2.0, -10.0, 60.0), 0.0, 0.0);
    EXPECT_NEAR(track_timeline_seek_relative(58.0, 10.0, 60.0), 60.0, 0.0);
    EXPECT_NEAR(track_timeline_seek_relative(NAN, 1.0, 60.0), 0.0, 0.0);
}

TEST(track_timeline_seek_capability_matches_decoder_contract)
{
    EXPECT_TRUE(track_timeline_path_is_seekable("song.wav"));
    EXPECT_TRUE(track_timeline_path_is_seekable("album/live.FLAC"));
    EXPECT_FALSE(track_timeline_path_is_seekable("tracker.XM"));
    EXPECT_FALSE(track_timeline_path_is_seekable("tracker.mod"));
    EXPECT_FALSE(track_timeline_path_is_seekable(NULL));
}
