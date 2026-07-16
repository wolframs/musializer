#include "render_export.h"
#include "test_support.h"

#include <stdlib.h>
#include <string.h>

TEST(render_export_builds_unique_sibling_mp4_path)
{
    char *first = NULL;
    char *second = NULL;
    REQUIRE_TRUE(render_export_temporary_path(
        "/tmp/movie final.mp4", 42, 1, &first));
    REQUIRE_TRUE(render_export_temporary_path(
        "/tmp/movie final.mp4", 42, 2, &second));
    EXPECT_TRUE(strcmp(first, "/tmp/.musializer-42-1.part.mp4") == 0);
    EXPECT_TRUE(strcmp(second, "/tmp/.musializer-42-2.part.mp4") == 0);
    EXPECT_TRUE(strcmp(first, second) != 0);
    free(first);
    free(second);
}

TEST(render_export_keeps_windows_paths_in_the_destination_directory)
{
    char *path = NULL;
    REQUIRE_TRUE(render_export_temporary_path(
        "C:\\Videos\\movie.mp4", 7, 3, &path));
    EXPECT_TRUE(strcmp(path, "C:\\Videos\\.musializer-7-3.part.mp4") == 0);
    free(path);

    char audio[256];
    REQUIRE_TRUE(render_export_decoded_audio_path(
        "C:\\Videos\\movie.mp4", 99, 3, audio, sizeof(audio)));
    EXPECT_TRUE(strcmp(audio,
                       "C:\\Videos\\.musializer-audio-99-3.wav") == 0);
}

TEST(render_export_decoded_audio_paths_are_unique_and_bounded)
{
    char first[256] = "unchanged";
    char second[256];
    REQUIRE_TRUE(render_export_decoded_audio_path(
        "/tmp/final movie.mp4", 42, 1, first, sizeof(first)));
    REQUIRE_TRUE(render_export_decoded_audio_path(
        "/tmp/final movie.mp4", 42, 2, second, sizeof(second)));
    EXPECT_TRUE(strcmp(first, "/tmp/.musializer-audio-42-1.wav") == 0);
    EXPECT_TRUE(strcmp(first, second) != 0);
    char tiny[5] = "keep";
    EXPECT_FALSE(render_export_decoded_audio_path(
        "/tmp/movie.mp4", 42, 1, tiny, sizeof(tiny)));
    EXPECT_TRUE(strcmp(tiny, "keep") == 0);
}

TEST(render_export_rejects_invalid_paths_atomically)
{
    char sentinel[] = "unchanged";
    char *output = sentinel;
    EXPECT_FALSE(render_export_temporary_path(NULL, 1, 1, &output));
    EXPECT_TRUE(output == sentinel);
    EXPECT_FALSE(render_export_temporary_path("", 1, 1, &output));
    EXPECT_TRUE(output == sentinel);
    EXPECT_FALSE(render_export_temporary_path("movie.mp4", 1, 1, NULL));
    EXPECT_FALSE(render_export_temporary_path("movie.mp4", 0, 1, &output));
    EXPECT_FALSE(render_export_temporary_path("movie.mp4", 1, 0, &output));
}

TEST(render_export_finalize_deadlines_are_bounded_and_boundary_exact)
{
    EXPECT_EQ_U64(render_export_finalize_grace_ms(true), 5000);
    EXPECT_EQ_U64(render_export_finalize_grace_ms(false), 300000);
    EXPECT_EQ_SIZE(render_export_wait_action(false, 4999, true),
                   RENDER_EXPORT_WAIT_CONTINUE);
    EXPECT_EQ_SIZE(render_export_wait_action(false, 5000, true),
                   RENDER_EXPORT_WAIT_TIMEOUT);
    EXPECT_EQ_SIZE(render_export_wait_action(false, 299999, false),
                   RENDER_EXPORT_WAIT_CONTINUE);
    EXPECT_EQ_SIZE(render_export_wait_action(false, 300000, false),
                   RENDER_EXPORT_WAIT_TIMEOUT);
    EXPECT_EQ_SIZE(render_export_wait_action(true, UINT64_MAX, false),
                   RENDER_EXPORT_WAIT_COMPLETE);
}

TEST(render_export_defaults_and_presets_are_valid)
{
    Render_Export_Config config;
    render_export_config_init(&config);
    EXPECT_EQ_SIZE(config.width, 1920);
    EXPECT_EQ_SIZE(config.height, 1080);
    EXPECT_EQ_SIZE(config.fps, 30);
    EXPECT_EQ_SIZE(config.quality, RENDER_QUALITY_HIGH);
    EXPECT_EQ_SIZE(config.supersample_factor, 2);
    EXPECT_EQ_SIZE(render_export_config_validate(&config), RENDER_EXPORT_OK);

    for (int resolution = 0; resolution < RENDER_RESOLUTION_COUNT; ++resolution) {
        REQUIRE_TRUE(render_export_config_set_resolution(
            &config, (Render_Resolution)resolution) == RENDER_EXPORT_OK);
        EXPECT_TRUE(render_export_resolution_name((Render_Resolution)resolution) != NULL);
    }
    for (int frame_rate = 0; frame_rate < RENDER_FRAME_RATE_COUNT; ++frame_rate) {
        REQUIRE_TRUE(render_export_config_set_frame_rate(
            &config, (Render_Frame_Rate)frame_rate) == RENDER_EXPORT_OK);
        EXPECT_TRUE(render_export_frame_rate_name((Render_Frame_Rate)frame_rate) != NULL);
    }
    for (int quality = 0; quality < RENDER_QUALITY_COUNT; ++quality) {
        REQUIRE_TRUE(render_export_config_set_quality(
            &config, (Render_Quality)quality) == RENDER_EXPORT_OK);
        EXPECT_TRUE(render_export_quality_name((Render_Quality)quality) != NULL);
    }
}

TEST(render_export_transport_ends_at_audio_duration_without_decay_tail)
{
    uint64_t total = 0;
    uint64_t cursor = 0;
    REQUIRE_TRUE(render_export_total_frames(12000, 48000, 30, &total) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(total, 8);
    REQUIRE_TRUE(render_export_sample_cursor(7, 48000, 30, 12000, &cursor) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(cursor, 11200);
    REQUIRE_TRUE(render_export_sample_cursor(8, 48000, 30, 12000, &cursor) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(cursor, 12000);

    REQUIRE_TRUE(render_export_total_frames(44100, 44100, 60, &total) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(total, 60);
    REQUIRE_TRUE(render_export_total_frames(1, 48000, 24, &total) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(total, 1);
}

TEST(render_export_window_maps_to_exact_frames_and_clamps)
{
    uint64_t start = 0;
    uint64_t end = 0;

    // The full track expressed as a window is the identity transport.
    REQUIRE_TRUE(render_export_window_frames(240, 24, 0.0, 10.0, &start, &end) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(start, 0);
    EXPECT_EQ_U64(end, 240);

    // Interior windows use absolute frame indices on the full timeline.
    REQUIRE_TRUE(render_export_window_frames(4661, 24, 35.0, 20.0, &start, &end) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(start, 840);
    EXPECT_EQ_U64(end, 1320);

    // A start inside a frame floors to the frame containing it.
    REQUIRE_TRUE(render_export_window_frames(240, 24, 0.1, 1.0, &start, &end) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(start, 2);
    EXPECT_EQ_U64(end, 26);

    // A sub-frame duration still renders one whole frame.
    REQUIRE_TRUE(render_export_window_frames(240, 24, 1.0, 0.001, &start, &end) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(start, 24);
    EXPECT_EQ_U64(end, 25);

    // Durations past the end of the track clamp without overflow.
    REQUIRE_TRUE(render_export_window_frames(240, 24, 9.0, 100.0, &start, &end) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(start, 216);
    EXPECT_EQ_U64(end, 240);
    REQUIRE_TRUE(render_export_window_frames(240, 24, 0.0, 1.0e300, &start, &end) ==
                 RENDER_EXPORT_OK);
    EXPECT_EQ_U64(start, 0);
    EXPECT_EQ_U64(end, 240);
}

TEST(render_export_window_rejects_degenerate_ranges_atomically)
{
    uint64_t start = 77;
    uint64_t end = 78;

    // Starting at or after the end of the track cannot render anything.
    EXPECT_TRUE(render_export_window_frames(240, 24, 10.0, 1.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);
    EXPECT_TRUE(render_export_window_frames(240, 24, 11.0, 1.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);

    // Non-positive, non-finite, and negative ranges are rejected.
    EXPECT_TRUE(render_export_window_frames(240, 24, -0.5, 1.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);
    EXPECT_TRUE(render_export_window_frames(240, 24, 0.0, 0.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);
    EXPECT_TRUE(render_export_window_frames(240, 24, 0.0, -2.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);
    EXPECT_TRUE(render_export_window_frames(240, 24, NAN, 1.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);
    EXPECT_TRUE(render_export_window_frames(240, 24, 0.0, INFINITY, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);
    EXPECT_TRUE(render_export_window_frames(0, 24, 0.0, 1.0, &start, &end) ==
                RENDER_EXPORT_ERROR_WINDOW);

    // Invalid transports and null outputs keep their own error classes.
    EXPECT_TRUE(render_export_window_frames(240, 0, 0.0, 1.0, &start, &end) ==
                RENDER_EXPORT_ERROR_FRAME_RATE);
    EXPECT_TRUE(render_export_window_frames(240, 24, 0.0, 1.0, NULL, &end) ==
                RENDER_EXPORT_ERROR_NULL);

    // Failures never touch the outputs.
    EXPECT_EQ_U64(start, 77);
    EXPECT_EQ_U64(end, 78);
}

TEST(render_export_transport_duration_text_is_exact_bounded_and_atomic)
{
    char duration[32] = "unchanged";
    REQUIRE_TRUE(render_export_transport_duration_text(
        24, 30, duration, sizeof(duration)) == RENDER_EXPORT_OK);
    EXPECT_TRUE(strcmp(duration, "0.800000000") == 0);
    REQUIRE_TRUE(render_export_transport_duration_text(
        5, 24, duration, sizeof(duration)) == RENDER_EXPORT_OK);
    EXPECT_TRUE(strcmp(duration, "0.208333333") == 0);

    char tiny[5] = "keep";
    EXPECT_EQ_SIZE(render_export_transport_duration_text(
        24, 30, tiny, sizeof(tiny)), RENDER_EXPORT_ERROR_BUFFER);
    EXPECT_TRUE(strcmp(tiny, "keep") == 0);
    EXPECT_EQ_SIZE(render_export_transport_duration_text(
        0, 30, duration, sizeof(duration)), RENDER_EXPORT_ERROR_FRAME_RATE);
}

TEST(render_export_target_scale_preserves_logical_composition)
{
    Render_Export_Config config;
    render_export_config_init(&config);
    float scale = 99.0f;
    REQUIRE_TRUE(render_export_target_scale(
        &config, 3840, 2160, &scale) == RENDER_EXPORT_OK);
    EXPECT_NEAR(scale, 2.0f, 0.0f);
    REQUIRE_TRUE(render_export_target_scale(
        &config, 1920, 1080, &scale) == RENDER_EXPORT_OK);
    EXPECT_NEAR(scale, 1.0f, 0.0f);

    scale = 99.0f;
    EXPECT_EQ_SIZE(render_export_target_scale(
        &config, 3840, 1080, &scale), RENDER_EXPORT_ERROR_RESOLUTION);
    EXPECT_NEAR(scale, 99.0f, 0.0f);
}

TEST(render_export_suggests_descriptive_sibling_path)
{
    Render_Export_Config config;
    char path[512];
    render_export_config_init(&config);
    REQUIRE_TRUE(render_export_suggest_path(
        "/music/Autoregressive Kitty.mp3", "constellation", &config,
        path, sizeof(path)) == RENDER_EXPORT_OK);
    EXPECT_TRUE(strcmp(path,
        "/music/Autoregressive Kitty-musializer-constellation-1080p30.mp4") == 0);

    char sentinel[] = "unchanged";
    char tiny[10];
    memcpy(tiny, sentinel, sizeof(tiny));
    EXPECT_EQ_SIZE(render_export_suggest_path(
        "song.wav", "bad/name", &config, tiny, sizeof(tiny)), RENDER_EXPORT_ERROR_PATH);
    EXPECT_TRUE(memcmp(tiny, sentinel, sizeof(tiny)) == 0);
}
