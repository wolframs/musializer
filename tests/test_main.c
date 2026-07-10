#include "audio_fixtures.h"
#include "test_support.h"

#include <math.h>

TEST(audio_fixtures_cover_baseline_signals)
{
    Audio_Fixture fixture = {0};

    REQUIRE_TRUE(audio_fixture_silence(&fixture, 8000, 2, 80));
    EXPECT_EQ_SIZE(fixture.frame_count, 80);
    EXPECT_NEAR(fixture.samples[79 * 2 + 1], 0.0f, 0.0f);
    audio_fixture_destroy(&fixture);

    REQUIRE_TRUE(audio_fixture_sine(&fixture, 8000, 1, 80, 1000.0f, 0.5f));
    EXPECT_NEAR(fixture.samples[2], 0.5f, 0.0001f);
    audio_fixture_destroy(&fixture);

    REQUIRE_TRUE(audio_fixture_sweep(&fixture, 8000, 1, 80, 100.0f, 1000.0f, 0.5f));
    EXPECT_TRUE(isfinite(fixture.samples[79]));
    audio_fixture_destroy(&fixture);

    REQUIRE_TRUE(audio_fixture_impulse(&fixture, 8000, 2, 80, 17, 0.75f));
    EXPECT_NEAR(fixture.samples[17 * 2], 0.75f, 0.0f);
    EXPECT_NEAR(fixture.samples[17 * 2 + 1], 0.75f, 0.0f);
    audio_fixture_destroy(&fixture);

    REQUIRE_TRUE(audio_fixture_stereo_imbalance(&fixture, 8000, 80, 500.0f, 0.8f, 0.2f));
    EXPECT_NEAR(fixture.samples[2], 0.8f * sinf(2.0f * 3.14159265358979323846f * 500.0f / 8000.0f), 0.0001f);
    EXPECT_NEAR(fixture.samples[3], 0.2f * sinf(2.0f * 3.14159265358979323846f * 500.0f / 8000.0f), 0.0001f);
    audio_fixture_destroy(&fixture);

    REQUIRE_TRUE(audio_fixture_beat(&fixture, 8000, 2, 8000, 120.0f, 0.9f));
    EXPECT_TRUE(fixture.samples[0] > 0.0f);
    EXPECT_TRUE(fixture.samples[4000 * 2] > 0.0f);
    audio_fixture_destroy(&fixture);
}

int main(void)
{
    return test_run_all();
}
