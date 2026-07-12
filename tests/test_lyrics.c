#include "lyrics.h"
#include "test_support.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static Lyric_Cue cue(uint64_t id, double start, double end, const char *text)
{
    Lyric_Cue result = {.id = id, .start_seconds = start, .end_seconds = end};
    if (text != NULL) snprintf(result.text, sizeof(result.text), "%s", text);
    return result;
}

TEST(lyrics_insert_is_sorted_and_ids_are_stable)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 20.0) == LYRICS_OK);
    Lyric_Cue later = cue(0, 4.0, 5.0, "later");
    Lyric_Cue early = cue(40, 1.0, 2.0, "early");
    Lyric_Cue same = cue(7, 1.0, 1.5, "same start");
    uint64_t generated = 0;
    REQUIRE_TRUE(lyrics_insert(&document, &later, &generated) == LYRICS_OK);
    EXPECT_EQ_U64(generated, 1);
    REQUIRE_TRUE(lyrics_insert(&document, &early, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&document, &same, NULL) == LYRICS_OK);
    EXPECT_EQ_U64(document.cues[0].id, 7);
    EXPECT_EQ_U64(document.cues[1].id, 40);
    EXPECT_EQ_U64(document.cues[2].id, 1);
    EXPECT_EQ_U64(document.next_id, 41);
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_OK);
    REQUIRE_TRUE(lyrics_delete(&document, 40) == LYRICS_OK);
    Lyric_Cue newest = cue(0, 2.0, 3.0, "new");
    REQUIRE_TRUE(lyrics_insert(&document, &newest, &generated) == LYRICS_OK);
    EXPECT_EQ_U64(generated, 41);
}

TEST(lyrics_rejects_bad_timing_text_and_duplicate_ids_without_mutation)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    Lyric_Cue valid = cue(9, 1.0, 2.0, "Grüße 世界");
    REQUIRE_TRUE(lyrics_insert(&document, &valid, NULL) == LYRICS_OK);
    uint64_t revision = document.revision;
    Lyric_Cue duplicate = cue(9, 3.0, 4.0, "duplicate");
    EXPECT_TRUE(lyrics_insert(&document, &duplicate, NULL) == LYRICS_ERROR_DUPLICATE_ID);
    Lyric_Cue outside = cue(10, 9.0, 11.0, "outside");
    EXPECT_TRUE(lyrics_insert(&document, &outside, NULL) == LYRICS_ERROR_INVALID_CUE);
    Lyric_Cue bad_utf8 = cue(10, 3.0, 4.0, "ok");
    bad_utf8.text[0] = (char)0xC0;
    bad_utf8.text[1] = (char)0xAF;
    bad_utf8.text[2] = '\0';
    EXPECT_TRUE(lyrics_insert(&document, &bad_utf8, NULL) == LYRICS_ERROR_INVALID_UTF8);
    EXPECT_EQ_SIZE(document.count, 1);
    EXPECT_EQ_U64(document.revision, revision);
}

TEST(lyrics_update_and_nudge_reorder_atomically)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    Lyric_Cue a = cue(1, 1.0, 2.0, "a");
    Lyric_Cue b = cue(2, 3.0, 4.0, "b");
    REQUIRE_TRUE(lyrics_insert(&document, &a, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&document, &b, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_update(&document, 2, 0.25, 0.75, "first now") == LYRICS_OK);
    EXPECT_EQ_U64(document.cues[0].id, 2);
    REQUIRE_TRUE(lyrics_nudge(&document, 2, 4.0) == LYRICS_OK);
    EXPECT_EQ_U64(document.cues[1].id, 2);
    double previous_start = lyrics_find(&document, 2)->start_seconds;
    uint64_t revision = document.revision;
    EXPECT_TRUE(lyrics_nudge(&document, 2, -99.0) == LYRICS_ERROR_INVALID_CUE);
    EXPECT_NEAR(lyrics_find(&document, 2)->start_seconds, previous_start, 0.0);
    EXPECT_EQ_U64(document.revision, revision);
}

TEST(lyrics_split_and_merge_preserve_left_id)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    Lyric_Cue original = cue(12, 1.0, 5.0, "hello world");
    REQUIRE_TRUE(lyrics_insert(&document, &original, NULL) == LYRICS_OK);
    uint64_t right_id = 0;
    REQUIRE_TRUE(lyrics_split(&document, 12, 3.0, "hello", "world", &right_id) == LYRICS_OK);
    EXPECT_EQ_U64(right_id, 13);
    EXPECT_EQ_SIZE(document.count, 2);
    EXPECT_NEAR(lyrics_find(&document, 12)->end_seconds, 3.0, 0.0);
    EXPECT_NEAR(lyrics_find(&document, 13)->start_seconds, 3.0, 0.0);
    REQUIRE_TRUE(lyrics_merge(&document, 12, 13, " ") == LYRICS_OK);
    EXPECT_EQ_SIZE(document.count, 1);
    EXPECT_EQ_U64(document.cues[0].id, 12);
    EXPECT_TRUE(strcmp(document.cues[0].text, "hello world") == 0);
    EXPECT_NEAR(document.cues[0].end_seconds, 5.0, 0.0);
}

TEST(lyrics_overlaps_are_valid_but_merge_requires_canonical_neighbors)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    Lyric_Cue a = cue(1, 1.0, 4.0, "lead");
    Lyric_Cue b = cue(2, 2.0, 3.0, "harmony");
    Lyric_Cue c = cue(3, 5.0, 6.0, "next");
    REQUIRE_TRUE(lyrics_insert(&document, &a, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&document, &b, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&document, &c, NULL) == LYRICS_OK);
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_OK);
    EXPECT_TRUE(lyrics_merge(&document, 1, 3, " ") == LYRICS_ERROR_NOT_ADJACENT);
}

TEST(lyrics_validation_detects_persistence_corruption)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    Lyric_Cue a = cue(1, 1.0, 2.0, "one");
    Lyric_Cue b = cue(2, 3.0, 4.0, "two");
    REQUIRE_TRUE(lyrics_insert(&document, &a, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&document, &b, NULL) == LYRICS_OK);
    Lyric_Cue temporary = document.cues[0];
    document.cues[0] = document.cues[1];
    document.cues[1] = temporary;
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_ERROR_ORDER);
    document.count = LYRICS_CUE_CAPACITY + 1;
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_ERROR_CAPACITY);
}

TEST(lyrics_bridge_round_trips_canonical_base64_and_milliseconds)
{
    Lyrics_Document source;
    Lyrics_Document restored;
    REQUIRE_TRUE(lyrics_document_init(&source, 12.345) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_document_init(&restored, 1.0) == LYRICS_OK);
    Lyric_Cue first = cue(8, 0.125, 1.750, "Grüße 世界");
    Lyric_Cue second = cue(19, 2.0, 3.001, "line two");
    REQUIRE_TRUE(lyrics_insert(&source, &second, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_insert(&source, &first, NULL) == LYRICS_OK);
    size_t required = 0;
    EXPECT_TRUE(lyrics_bridge_export(&source, NULL, 0, &required) ==
                LYRICS_ERROR_BUFFER_TOO_SMALL);
    char output[2048];
    REQUIRE_TRUE(required <= sizeof(output));
    REQUIRE_TRUE(lyrics_bridge_export(&source, output, sizeof(output), &required) == LYRICS_OK);
    EXPECT_TRUE(strstr(output, "MUSIALIZER-LYRICS-BRIDGE\t1\t12345\n") == output);
    REQUIRE_TRUE(lyrics_bridge_import(&restored, output, required - 1) == LYRICS_OK);
    EXPECT_EQ_SIZE(restored.count, 2);
    EXPECT_EQ_U64(restored.cues[0].id, 8);
    EXPECT_TRUE(strcmp(restored.cues[0].text, "Grüße 世界") == 0);
    EXPECT_NEAR(restored.cues[0].start_seconds, 0.125, 0.000001);
    EXPECT_NEAR(restored.duration_seconds, 12.345, 0.000001);
}

TEST(lyrics_bridge_import_is_strict_and_atomic)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    Lyric_Cue existing = cue(1, 1.0, 2.0, "keep me");
    REQUIRE_TRUE(lyrics_insert(&document, &existing, NULL) == LYRICS_OK);
    uint64_t revision = document.revision;
    const char malformed[] =
        "MUSIALIZER-LYRICS-BRIDGE\t1\t10000\n"
        "2\t2000\t3000\t%%%bad%%\n";
    EXPECT_TRUE(lyrics_bridge_import(&document, malformed, sizeof(malformed) - 1) ==
                LYRICS_ERROR_BRIDGE_FORMAT);
    EXPECT_EQ_SIZE(document.count, 1);
    EXPECT_EQ_U64(document.revision, revision);
    EXPECT_TRUE(strcmp(document.cues[0].text, "keep me") == 0);

    const char zero_id[] =
        "MUSIALIZER-LYRICS-BRIDGE\t1\t10000\n"
        "0\t2000\t3000\tbm8=\n";
    EXPECT_TRUE(lyrics_bridge_import(&document, zero_id, sizeof(zero_id) - 1) ==
                LYRICS_ERROR_BRIDGE_FORMAT);
    EXPECT_EQ_SIZE(document.count, 1);
}

TEST(lyrics_bridge_rejects_timing_that_cannot_round_trip_in_milliseconds)
{
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 1.0) == LYRICS_OK);
    Lyric_Cue tiny = cue(1, 0.1001, 0.1004, "tiny");
    REQUIRE_TRUE(lyrics_insert(&document, &tiny, NULL) == LYRICS_OK);
    size_t required = 0;
    EXPECT_TRUE(lyrics_bridge_export(&document, NULL, 0, &required) ==
                LYRICS_ERROR_INVALID_CUE);
}
