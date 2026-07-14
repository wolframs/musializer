#include "song_atlas_map.h"
#include "audio_fixtures.h"
#include "test_support.h"

#include <float.h>
#include <stdlib.h>

static double atlas_frequency_centroid(const Song_Atlas_Map *map)
{
    double weighted = 0.0;
    double total = 0.0;
    for (size_t i = 0; i < map->count; ++i) {
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            double value = map->slices[i].bands[band];
            weighted += value*(double)band;
            total += value;
        }
    }
    return total > 0.0 ? weighted/total : 0.0;
}

TEST(song_atlas_map_silence_is_valid_bounded_and_flat)
{
    Audio_Fixture fixture = {0};
    Song_Atlas_Map *map = calloc(1, sizeof(*map));
    REQUIRE_TRUE(map != NULL);
    REQUIRE_TRUE(audio_fixture_silence(&fixture, 8000, 2, 16000));

    size_t count = song_atlas_map_build(
        fixture.samples, fixture.frame_count, fixture.channel_count,
        fixture.sample_rate, map);
    EXPECT_EQ_SIZE(count, 24);
    EXPECT_TRUE(song_atlas_map_valid(map));
    EXPECT_NEAR(map->duration_seconds, 2.0, 0.0);
    for (size_t i = 0; i < map->count; ++i) {
        EXPECT_NEAR(map->slices[i].rms, 0.0f, 0.0f);
        EXPECT_NEAR(map->slices[i].flux, 0.0f, 0.0f);
        EXPECT_FALSE(map->slices[i].onset);
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            EXPECT_NEAR(map->slices[i].bands[band], 0.0f, 0.0f);
        }
    }

    audio_fixture_destroy(&fixture);
    free(map);
}

TEST(song_atlas_map_preserves_frequency_order_across_whole_track)
{
    Audio_Fixture low = {0};
    Audio_Fixture high = {0};
    Song_Atlas_Map *low_map = calloc(1, sizeof(*low_map));
    Song_Atlas_Map *high_map = calloc(1, sizeof(*high_map));
    REQUIRE_TRUE(low_map != NULL && high_map != NULL);
    REQUIRE_TRUE(audio_fixture_sine(&low, 8000, 1, 24000, 220.0f, 0.7f));
    REQUIRE_TRUE(audio_fixture_sine(&high, 8000, 1, 24000, 1800.0f, 0.7f));

    REQUIRE_TRUE(song_atlas_map_build(
        low.samples, low.frame_count, low.channel_count, low.sample_rate,
        low_map) > 0);
    REQUIRE_TRUE(song_atlas_map_build(
        high.samples, high.frame_count, high.channel_count, high.sample_rate,
        high_map) > 0);
    EXPECT_TRUE(song_atlas_map_valid(low_map));
    EXPECT_TRUE(song_atlas_map_valid(high_map));
    EXPECT_TRUE(atlas_frequency_centroid(high_map) >
                atlas_frequency_centroid(low_map) + 5.0);

    audio_fixture_destroy(&low);
    audio_fixture_destroy(&high);
    free(low_map);
    free(high_map);
}

TEST(song_atlas_map_invalid_input_clears_destination)
{
    Song_Atlas_Map *map = calloc(1, sizeof(*map));
    REQUIRE_TRUE(map != NULL);
    map->count = SONG_ATLAS_MAX_SLICES;
    map->duration_seconds = 42.0;
    map->slices[0].bands[0] = 1.0f;

    EXPECT_EQ_SIZE(song_atlas_map_build(NULL, 32, 1, 48000, map), 0);
    EXPECT_EQ_SIZE(map->count, 0);
    EXPECT_NEAR(map->duration_seconds, 0.0, 0.0);
    EXPECT_NEAR(map->slices[0].bands[0], 0.0f, 0.0f);
    EXPECT_FALSE(song_atlas_map_valid(map));

    free(map);
}

TEST(song_atlas_map_clamps_extreme_finite_pcm_to_a_valid_map)
{
    float samples[8192];
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
        samples[i] = (i & 1U) ? FLT_MAX : -FLT_MAX;
    }
    Song_Atlas_Map *map = calloc(1, sizeof(*map));
    REQUIRE_TRUE(map != NULL);
    REQUIRE_TRUE(song_atlas_map_build(samples, 8192, 1, 48000, map) > 0);
    EXPECT_TRUE(song_atlas_map_valid(map));
    free(map);
}
