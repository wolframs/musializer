#include "scene_orbital_lattice_motion.h"
#include "test_support.h"

#include <math.h>
#include <string.h>

#define TEST_TAU 6.28318530717958647692

static Orbital_Lattice_Motion_Input motion_input(
    double time, float delta, const float *bands, size_t count)
{
    return (Orbital_Lattice_Motion_Input) {
        .time_seconds = time,
        .delta_seconds = delta,
        .bands = bands,
        .bands_count = count,
    };
}

static double wrapped_distance(double first, double second, double period)
{
    double difference = fabs(second - first);
    return difference > period*0.5 ? period - difference : difference;
}

TEST(orbital_motion_damps_distinct_frequency_roles)
{
    float silence[48] = {0};
    float shaped[48];
    for (size_t i = 0; i < 48; ++i) {
        shaped[i] = i < 9 ? 1.0f : i < 28 ? 0.55f : 0.2f;
    }
    Orbital_Lattice_Motion motion;
    orbital_lattice_motion_init(&motion, 42);
    Orbital_Lattice_Motion_Input input = motion_input(0.0, 0.0f, silence, 48);
    orbital_lattice_motion_update(&motion, &input);

    input = motion_input(1.0/60.0, 1.0f/60.0f, shaped, 48);
    input.rms = 0.6f;
    input.spectral_flux = 0.2f;
    input.onset = true;
    orbital_lattice_motion_update(&motion, &input);

    EXPECT_TRUE(motion.bass > motion.mids);
    EXPECT_TRUE(motion.mids > motion.treble);
    EXPECT_TRUE(motion.bass > 0.0f && motion.bass < 1.0f);
    EXPECT_TRUE(motion.energy > 0.0f && motion.energy < 0.99f);
    EXPECT_TRUE(motion.flux > 0.0f && motion.flux < 0.8f);
    EXPECT_TRUE(motion.onset_pulse > 0.0f && motion.onset_pulse < 1.0f);
    EXPECT_TRUE(motion.node_bands[0] > motion.node_bands[8]);
    EXPECT_TRUE(motion.node_bands[8] > motion.node_bands[15]);
}

TEST(orbital_motion_keeps_audio_independent_phase_without_late_song_jumps)
{
    float bands[64] = {0};
    Orbital_Lattice_Motion motion;
    orbital_lattice_motion_init(&motion, 77);
    Orbital_Lattice_Motion_Input input = motion_input(180.0, 0.0f, bands, 64);
    orbital_lattice_motion_update(&motion, &input);

    double previous_camera = motion.camera_phase;
    double previous_travel = motion.travel_phase;
    double previous_twist = motion.twist_phase;
    double previous_hue = motion.hue_degrees;
    for (size_t frame = 1; frame <= 240; ++frame) {
        for (size_t band = 0; band < 64; ++band) {
            bands[band] = (frame & 1U) ? 1.0f : 0.0f;
        }
        input = motion_input(180.0 + frame/60.0, 1.0f/60.0f, bands, 64);
        input.rms = (frame & 1U) ? 1.0f : 0.0f;
        input.spectral_flux = (frame & 1U) ? 1.0f : 0.0f;
        orbital_lattice_motion_update(&motion, &input);

        EXPECT_TRUE(wrapped_distance(previous_camera, motion.camera_phase,
                                     TEST_TAU) < 0.002);
        EXPECT_TRUE(wrapped_distance(previous_travel, motion.travel_phase,
                                     ORBITAL_LATTICE_PATH_LENGTH) < 0.017);
        EXPECT_TRUE(wrapped_distance(previous_twist, motion.twist_phase,
                                     TEST_TAU) < 0.007);
        EXPECT_TRUE(wrapped_distance(previous_hue, motion.hue_degrees,
                                     360.0) < 0.09);
        previous_camera = motion.camera_phase;
        previous_travel = motion.travel_phase;
        previous_twist = motion.twist_phase;
        previous_hue = motion.hue_degrees;
    }
}

TEST(orbital_motion_is_deterministic_and_rebases_seeks)
{
    float bands[32];
    for (size_t i = 0; i < 32; ++i) bands[i] = (float)i/31.0f;
    Orbital_Lattice_Motion first;
    Orbital_Lattice_Motion second;
    orbital_lattice_motion_init(&first, UINT64_C(0x123456789abcdef0));
    orbital_lattice_motion_init(&second, UINT64_C(0x123456789abcdef0));
    for (size_t frame = 0; frame < 90; ++frame) {
        Orbital_Lattice_Motion_Input input = motion_input(
            frame/30.0, frame == 0 ? 0.0f : 1.0f/30.0f, bands, 32);
        input.rms = 0.4f;
        input.spectral_flux = 0.08f;
        input.onset = frame == 30;
        orbital_lattice_motion_update(&first, &input);
        orbital_lattice_motion_update(&second, &input);
    }
    EXPECT_TRUE(memcmp(&first, &second, sizeof(first)) == 0);

    Orbital_Lattice_Motion fresh;
    orbital_lattice_motion_init(&fresh, UINT64_C(0x123456789abcdef0));
    Orbital_Lattice_Motion_Input seek = motion_input(41.25, 0.0f, bands, 32);
    seek.rms = 0.7f;
    seek.spectral_flux = 0.15f;
    orbital_lattice_motion_update(&first, &seek);
    orbital_lattice_motion_update(&fresh, &seek);
    EXPECT_TRUE(memcmp(&first, &fresh, sizeof(first)) == 0);

    Orbital_Lattice_Motion uninterrupted;
    orbital_lattice_motion_init(&uninterrupted, UINT64_C(0x123456789abcdef0));
    for (size_t frame = 0; frame <= 2475; ++frame) {
        double time = (double)frame/60.0;
        Orbital_Lattice_Motion_Input input = motion_input(
            time, frame == 0 ? 0.0f : 1.0f/60.0f, bands, 32);
        input.rms = 0.2f + 0.5f*(float)(frame%17)/16.0f;
        input.spectral_flux = 0.15f*(float)(frame%11)/10.0f;
        orbital_lattice_motion_update(&uninterrupted, &input);
    }
    EXPECT_TRUE(wrapped_distance(first.camera_phase, uninterrupted.camera_phase,
                                 TEST_TAU) < 0.000001);
    EXPECT_TRUE(wrapped_distance(first.travel_phase, uninterrupted.travel_phase,
                                 ORBITAL_LATTICE_PATH_LENGTH) < 0.000001);
    EXPECT_TRUE(wrapped_distance(first.twist_phase, uninterrupted.twist_phase,
                                 TEST_TAU) < 0.000001);
    EXPECT_TRUE(wrapped_distance(first.hue_degrees, uninterrupted.hue_degrees,
                                 360.0) < 0.000001);
}

TEST(orbital_motion_is_stable_across_common_frame_rates)
{
    float silence[64] = {0};
    float bands[64];
    for (size_t i = 0; i < 64; ++i) bands[i] = 0.25f + (float)(i%7)*0.08f;
    Orbital_Lattice_Motion thirty;
    Orbital_Lattice_Motion sixty;
    orbital_lattice_motion_init(&thirty, 9001);
    orbital_lattice_motion_init(&sixty, 9001);

    Orbital_Lattice_Motion_Input start = motion_input(0.0, 0.0f, silence, 64);
    orbital_lattice_motion_update(&thirty, &start);
    orbital_lattice_motion_update(&sixty, &start);
    for (size_t frame = 1; frame <= 120; ++frame) {
        Orbital_Lattice_Motion_Input input = motion_input(
            frame/60.0, 1.0f/60.0f, bands, 64);
        input.rms = 0.42f;
        input.spectral_flux = 0.09f;
        orbital_lattice_motion_update(&sixty, &input);
    }
    for (size_t frame = 1; frame <= 60; ++frame) {
        Orbital_Lattice_Motion_Input input = motion_input(
            frame/30.0, 1.0f/30.0f, bands, 64);
        input.rms = 0.42f;
        input.spectral_flux = 0.09f;
        orbital_lattice_motion_update(&thirty, &input);
    }

    EXPECT_NEAR(thirty.energy, sixty.energy, 0.0001);
    EXPECT_NEAR(thirty.bass, sixty.bass, 0.0001);
    EXPECT_TRUE(wrapped_distance(thirty.camera_phase, sixty.camera_phase,
                                 TEST_TAU) < 0.0001);
    EXPECT_TRUE(wrapped_distance(thirty.travel_phase, sixty.travel_phase,
                                 ORBITAL_LATTICE_PATH_LENGTH) < 0.0002);
    EXPECT_TRUE(wrapped_distance(thirty.twist_phase, sixty.twist_phase,
                                 TEST_TAU) < 0.0002);
}

TEST(orbital_ring_convoy_wraps_one_faded_ring_instead_of_every_ring)
{
    Orbital_Lattice_Motion motion;
    orbital_lattice_motion_init(&motion, 5);
    Orbital_Lattice_Motion_Input input = motion_input(0.0, 0.0f, NULL, 0);
    orbital_lattice_motion_update(&motion, &input);
    motion.travel_phase = ORBITAL_LATTICE_PATH_LENGTH - 0.05;

    size_t faded = 0;
    float distances[ORBITAL_LATTICE_RING_COUNT];
    for (size_t i = 0; i < ORBITAL_LATTICE_RING_COUNT; ++i) {
        Orbital_Lattice_Ring_Motion ring;
        REQUIRE_TRUE(orbital_lattice_motion_ring(&motion, i, &ring));
        distances[i] = ring.distance;
        if (ring.visibility < 0.1f) faded += 1;
        EXPECT_TRUE(ring.distance >= 0.0f &&
                    ring.distance < ORBITAL_LATTICE_PATH_LENGTH);
        EXPECT_TRUE(ring.depth_t >= 0.0f && ring.depth_t < 1.0f);
        EXPECT_TRUE(ring.visibility >= 0.0f && ring.visibility <= 1.0f);
    }
    EXPECT_TRUE(faded >= 1 && faded <= 3);
    for (size_t i = 1; i < ORBITAL_LATTICE_RING_COUNT; ++i) {
        float spacing = distances[i] - distances[i - 1];
        if (spacing < 0.0f) spacing += ORBITAL_LATTICE_PATH_LENGTH;
        EXPECT_NEAR(spacing,
                    ORBITAL_LATTICE_RING_SPACING, 0.0001);
    }
    EXPECT_FALSE(orbital_lattice_motion_ring(
        &motion, ORBITAL_LATTICE_RING_COUNT, NULL));
}
