#include "beat_tracker.h"
#include "test_support.h"

#include <math.h>

TEST(beat_tracker_rejects_invalid_input)
{
    Beat_Tracker tracker;
    float phase = 0.0f;
    beat_tracker_reset(&tracker);
    EXPECT_FALSE(beat_tracker_update(NULL, 0.0, false, 0.0f, &phase));
    EXPECT_FALSE(beat_tracker_update(&tracker, -1.0, false, 0.0f, &phase));
    EXPECT_FALSE(beat_tracker_update(&tracker, NAN, false, 0.0f, &phase));
    EXPECT_FALSE(beat_tracker_update(&tracker, 0.0, false, NAN, &phase));
    EXPECT_FALSE(beat_tracker_update(&tracker, 0.0, false, -0.1f, &phase));
    EXPECT_FALSE(beat_tracker_update(&tracker, 0.0, false, 0.0f, NULL));
}

TEST(beat_tracker_has_a_stable_neutral_clock_before_learning)
{
    Beat_Tracker tracker;
    float phase = 0.0f;
    beat_tracker_reset(&tracker);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 3.0, false, 0.0f, &phase));
    EXPECT_NEAR(phase, 0.0f, 1e-6f);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 3.125, false, 0.0f, &phase));
    EXPECT_NEAR(phase, 0.25f, 1e-6f);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 3.499, false, 0.0f, &phase));
    EXPECT_TRUE(phase > 0.99f && phase < 1.0f);
}

TEST(beat_tracker_learns_credible_onset_intervals)
{
    Beat_Tracker tracker;
    float phase = 0.0f;
    beat_tracker_reset(&tracker);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 0.0, true, 0.2f, &phase));
    REQUIRE_TRUE(beat_tracker_update(&tracker, 0.6, true, 0.2f, &phase));
    REQUIRE_TRUE(beat_tracker_update(&tracker, 1.2, true, 0.2f, &phase));
    EXPECT_EQ_SIZE(tracker.learned_intervals, 2);
    EXPECT_TRUE(tracker.interval_seconds > 0.55);
    EXPECT_TRUE(tracker.interval_seconds < 0.60);
    EXPECT_NEAR(phase, 0.0f, 1e-6f);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 1.35, false, 0.0f, &phase));
    EXPECT_TRUE(phase > 0.25f && phase < 0.28f);
}

TEST(beat_tracker_ignores_noise_and_resets_after_discontinuity)
{
    Beat_Tracker tracker;
    float phase = 0.0f;
    beat_tracker_reset(&tracker);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 0.0, true, 0.2f, &phase));
    REQUIRE_TRUE(beat_tracker_update(&tracker, 0.1, true, 0.2f, &phase));
    EXPECT_EQ_SIZE(tracker.learned_intervals, 0);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 2.0, false, 0.0f, &phase));
    EXPECT_NEAR(phase, 0.0f, 1e-6f);
    EXPECT_FALSE(tracker.has_onset);
    REQUIRE_TRUE(beat_tracker_update(&tracker, 0.5, false, 0.0f, &phase));
    EXPECT_NEAR(phase, 0.0f, 1e-6f);
}
