#include "scene_constellation_motion.h"
#include "test_support.h"

#include <string.h>

static Constellation_Motion_Input constellation_input(
    double time_seconds, float delta_seconds, float rms, float flux, bool onset)
{
    return (Constellation_Motion_Input) {
        .time_seconds = time_seconds,
        .delta_seconds = delta_seconds,
        .rms = rms,
        .spectral_flux = flux,
        .onset = onset,
    };
}

TEST(constellation_motion_rebases_transport_discontinuities)
{
    Constellation_Motion played;
    constellation_motion_init(&played);
    for (size_t frame = 0; frame < 180; ++frame) {
        Constellation_Motion_Input input = constellation_input(
            frame/60.0, frame == 0 ? 0.0f : 1.0f/60.0f,
            0.15f + (float)(frame%9)*0.04f,
            0.02f + (float)(frame%7)*0.01f,
            frame%45 == 0);
        constellation_motion_update(&played, &input);
    }

    Constellation_Motion fresh;
    constellation_motion_init(&fresh);
    Constellation_Motion_Input seek = constellation_input(
        0.75, 0.0f, 0.62f, 0.11f, true);
    constellation_motion_update(&played, &seek);
    constellation_motion_update(&fresh, &seek);
    EXPECT_TRUE(memcmp(&played, &fresh, sizeof(played)) == 0);
}

TEST(constellation_motion_is_deterministic_and_bounded)
{
    Constellation_Motion first;
    Constellation_Motion second;
    constellation_motion_init(&first);
    constellation_motion_init(&second);
    for (size_t frame = 0; frame < 240; ++frame) {
        Constellation_Motion_Input input = constellation_input(
            12.0 + frame/60.0, frame == 0 ? 0.0f : 1.0f/60.0f,
            frame%2 ? 1.5f : -0.5f,
            frame%3 ? 0.4f : -1.0f,
            frame%37 == 0);
        constellation_motion_update(&first, &input);
        constellation_motion_update(&second, &input);
        EXPECT_TRUE(first.energy >= 0.0f && first.energy <= 1.0f);
        EXPECT_TRUE(first.flux >= 0.0f && first.flux <= 1.0f);
        EXPECT_TRUE(first.onset_pulse >= 0.0f && first.onset_pulse <= 1.0f);
    }
    EXPECT_TRUE(memcmp(&first, &second, sizeof(first)) == 0);
}
