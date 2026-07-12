#include "analysis_bridge.h"
#include "test_support.h"

#include <stdlib.h>
#include <string.h>

#define HASH_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static const char full_bridge[] =
    "MUSIALIZER_BRIDGE\t1\n"
    "AUDIO\t" HASH_A "\t10000\n"
    "LYRIC\t11\t0\t1000\t875\tuncertain\taGVsbG8=\n"
    "LYRIC\t12\t2000\t3000\t-1\tnone\td29ybGQ=\n"
    "SECTION\t21\t0\t5000\tspectrum\t250\tW10=\n"
    "SECTION\t22\t5000\t10000\tatlas\t900\tW10=\n"
    "SEMANTIC\t31\t0\t10000\t700\t300\t-200\t800\tYnJpZ2h0\n";

static Analysis_Bridge *new_bridge(void)
{
    Analysis_Bridge *bridge = malloc(sizeof(*bridge));
    if (bridge == NULL) {
        TEST_FAIL("could not allocate Analysis_Bridge test fixture");
        return NULL;
    }
    analysis_bridge_init(bridge);
    return bridge;
}

TEST(analysis_bridge_parses_all_typed_lanes)
{
    Analysis_Bridge *bridge = new_bridge();
    if (bridge == NULL) return;
    REQUIRE_TRUE(analysis_bridge_parse(bridge, full_bridge, sizeof(full_bridge) - 1,
                                      HASH_A, 10000) == ANALYSIS_BRIDGE_OK);
    EXPECT_TRUE(bridge->lyrics_present);
    EXPECT_TRUE(bridge->sections_present);
    EXPECT_TRUE(bridge->semantic_cues_present);
    EXPECT_FALSE(bridge->semantic_notes_present);
    EXPECT_EQ_SIZE(bridge->lyrics.count, 2);
    EXPECT_TRUE(strcmp(bridge->lyrics.cues[0].text, "hello") == 0);
    EXPECT_EQ_U64(bridge->lyric_metadata[0].id, 11);
    EXPECT_TRUE(bridge->lyric_metadata[0].uncertain);
    EXPECT_EQ_SIZE(bridge->section_count, 2);
    EXPECT_TRUE(bridge->sections[1].recommended_scene == ANALYSIS_SCENE_ATLAS);
    EXPECT_TRUE(strcmp(bridge->sections[0].reasons_json, "[]") == 0);
    EXPECT_EQ_SIZE(bridge->semantic_cue_count, 1);
    EXPECT_TRUE(strcmp(bridge->semantic_cues[0].summary, "bright") == 0);
    EXPECT_TRUE(bridge->semantic_cues[0].valence_milli == -200);
    free(bridge);
}

TEST(analysis_bridge_preserves_absent_lanes_and_semantic_notes)
{
    static const char notes_bridge[] =
        "MUSIALIZER_BRIDGE\t1\n"
        "AUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tpulse\t500\tW10=\n"
        "SEMANTIC_NOTE\t41\tZmVlbHMgYWxpdmU=\n";
    Analysis_Bridge *bridge = new_bridge();
    if (bridge == NULL) return;
    REQUIRE_TRUE(analysis_bridge_parse(bridge, notes_bridge, sizeof(notes_bridge) - 1,
                                      NULL, 0) == ANALYSIS_BRIDGE_OK);
    EXPECT_FALSE(bridge->lyrics_present);
    EXPECT_TRUE(bridge->sections_present);
    EXPECT_FALSE(bridge->semantic_cues_present);
    EXPECT_TRUE(bridge->semantic_notes_present);
    EXPECT_EQ_SIZE(bridge->semantic_note_count, 1);
    EXPECT_TRUE(strcmp(bridge->semantic_notes[0].text, "feels alive") == 0);
    free(bridge);
}

TEST(analysis_bridge_rejects_identity_header_and_base64_errors_atomically)
{
    Analysis_Bridge *bridge = new_bridge();
    if (bridge == NULL) return;
    REQUIRE_TRUE(analysis_bridge_parse(bridge, full_bridge, sizeof(full_bridge) - 1,
                                      HASH_A, 10000) == ANALYSIS_BRIDGE_OK);
    EXPECT_TRUE(analysis_bridge_parse(bridge, full_bridge, sizeof(full_bridge) - 1,
                                     "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                                     10000) == ANALYSIS_BRIDGE_ERROR_AUDIO_MISMATCH);
    EXPECT_EQ_SIZE(bridge->section_count, 2);
    EXPECT_TRUE(strcmp(bridge->audio_sha256, HASH_A) == 0);

    static const char bad_header[] = "MUSIALIZER_BRIDGE\t2\nAUDIO\t" HASH_A "\t10000\n";
    EXPECT_TRUE(analysis_bridge_parse(bridge, bad_header, sizeof(bad_header) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_HEADER);
    static const char bad_base64[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\t!!!!\n";
    EXPECT_TRUE(analysis_bridge_parse(bridge, bad_base64, sizeof(bad_base64) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_BASE64);
    EXPECT_EQ_SIZE(bridge->section_count, 2);
    free(bridge);
}

TEST(analysis_bridge_rejects_duplicate_ids_unknown_scenes_and_record_order)
{
    static const char duplicate[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "LYRIC\t21\t0\t1000\t-1\tnone\taGk=\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\tW10=\n";
    static const char unknown_scene[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tbogus\t500\tW10=\n";
    static const char wrong_order[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\tW10=\n"
        "LYRIC\t11\t0\t1000\t-1\tnone\taGk=\n";
    Analysis_Bridge *bridge = new_bridge();
    if (bridge == NULL) return;
    EXPECT_TRUE(analysis_bridge_parse(bridge, duplicate, sizeof(duplicate) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_DUPLICATE_ID);
    EXPECT_TRUE(analysis_bridge_parse(bridge, unknown_scene, sizeof(unknown_scene) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_SCENE);
    EXPECT_TRUE(analysis_bridge_parse(bridge, wrong_order, sizeof(wrong_order) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_ORDER);
    free(bridge);
}

TEST(analysis_bridge_requires_section_and_semantic_full_coverage)
{
    static const char section_gap[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t4000\tspectrum\t500\tW10=\n"
        "SECTION\t22\t5000\t10000\tpulse\t500\tW10=\n";
    static const char semantic_gap[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\tW10=\n"
        "SEMANTIC\t31\t0\t4000\t1\t2\t3\t4\t\n"
        "SEMANTIC\t32\t5000\t10000\t1\t2\t3\t4\t\n";
    Analysis_Bridge *bridge = new_bridge();
    if (bridge == NULL) return;
    EXPECT_TRUE(analysis_bridge_parse(bridge, section_gap, sizeof(section_gap) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_COVERAGE);
    EXPECT_TRUE(analysis_bridge_parse(bridge, semantic_gap, sizeof(semantic_gap) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_COVERAGE);
    free(bridge);
}

TEST(analysis_bridge_rejects_oversized_decoded_fields)
{
    const char prefix[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\t";
    size_t encoded = 4*((ANALYSIS_BRIDGE_REASONS_CAPACITY + 2)/3);
    size_t length = sizeof(prefix) - 1 + encoded + 1;
    char *input = malloc(length);
    REQUIRE_TRUE(input != NULL);
    memcpy(input, prefix, sizeof(prefix) - 1);
    memset(input + sizeof(prefix) - 1, 'A', encoded);
    input[length - 1] = '\n';
    Analysis_Bridge *bridge = new_bridge();
    if (bridge != NULL) {
        EXPECT_TRUE(analysis_bridge_parse(bridge, input, length, NULL, 0) ==
                    ANALYSIS_BRIDGE_ERROR_DECODED_SIZE);
    }
    free(bridge);
    free(input);
}

TEST(analysis_bridge_rejects_invalid_utf8_and_noncanonical_integers)
{
    static const char bad_utf8[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t10000\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\tW10=\n"
        "SEMANTIC_NOTE\t41\t/w==\n";
    static const char leading_zero[] =
        "MUSIALIZER_BRIDGE\t1\nAUDIO\t" HASH_A "\t010000\n"
        "SECTION\t21\t0\t10000\tspectrum\t500\tW10=\n";
    Analysis_Bridge *bridge = new_bridge();
    if (bridge == NULL) return;
    EXPECT_TRUE(analysis_bridge_parse(bridge, bad_utf8, sizeof(bad_utf8) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_UTF8);
    EXPECT_TRUE(analysis_bridge_parse(bridge, leading_zero, sizeof(leading_zero) - 1,
                                     NULL, 0) == ANALYSIS_BRIDGE_ERROR_AUDIO);
    free(bridge);
}
