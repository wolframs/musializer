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

TEST(lyrics_duration_normalization_clamps_tail_and_rejects_late_cues_atomically)
{
    Lyrics_Document source;
    Lyrics_Document destination;
    REQUIRE_TRUE(lyrics_document_init(&source, 10.0) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_document_init(&destination, 8.0) == LYRICS_OK);
    Lyric_Cue tail = cue(1, 8.5, 10.0, "tail");
    REQUIRE_TRUE(lyrics_insert(&source, &tail, NULL) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_document_normalize_duration(
        &destination, &source, 9.0) == LYRICS_OK);
    EXPECT_NEAR(destination.duration_seconds, 9.0, 0.0);
    EXPECT_NEAR(destination.cues[0].end_seconds, 9.0, 0.0);
    EXPECT_TRUE(lyrics_at_time(&destination, 8.75) == &destination.cues[0]);
    EXPECT_TRUE(lyrics_at_time(&destination, 9.0) == NULL);

    Lyrics_Document before = destination;
    EXPECT_TRUE(lyrics_document_normalize_duration(
        &destination, &source, 8.0) == LYRICS_ERROR_DURATION);
    EXPECT_TRUE(memcmp(&destination, &before, sizeof(destination)) == 0);
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

// --- Pasting into a draft ---------------------------------------------------
//
// The draft field is append-only, so a paste lands at the end. What matters is
// that it lands whole or not at all: truncating to fit would cut a multi-byte
// sequence and produce a draft that can never be applied.

TEST(lyrics_text_append_adds_to_the_end_of_a_draft)
{
    char text[LYRICS_TEXT_CAPACITY] = "We were carving ";
    bool flattened = true;
    REQUIRE_TRUE(lyrics_text_append(text, sizeof(text), "light out of the quiet",
                                    &flattened) == LYRICS_OK);
    EXPECT_TRUE(strcmp(text, "We were carving light out of the quiet") == 0);
    EXPECT_FALSE(flattened);

    // Appending to an empty draft is the ordinary first paste.
    char empty[LYRICS_TEXT_CAPACITY] = "";
    REQUIRE_TRUE(lyrics_text_append(empty, sizeof(empty), "First line", NULL) ==
                 LYRICS_OK);
    EXPECT_TRUE(strcmp(empty, "First line") == 0);
}

TEST(lyrics_text_append_flattens_line_breaks_and_says_so)
{
    // Copying two lines from a lyrics sheet is the common case. A cue is one
    // line by contract, so the breaks collapse rather than the paste failing.
    char text[LYRICS_TEXT_CAPACITY] = "";
    bool flattened = false;
    REQUIRE_TRUE(lyrics_text_append(text, sizeof(text), "one\r\ntwo\tthree\n",
                                    &flattened) == LYRICS_OK);
    EXPECT_TRUE(strcmp(text, "one  two three ") == 0);
    EXPECT_TRUE(flattened);
}

TEST(lyrics_text_append_refuses_an_over_long_paste_whole)
{
    char text[LYRICS_TEXT_CAPACITY];
    memset(text, 'a', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    char before[LYRICS_TEXT_CAPACITY];
    memcpy(before, text, sizeof(before));
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "more", NULL) ==
                LYRICS_ERROR_TEXT_TOO_LONG);
    // Byte-for-byte untouched: no partial write, no truncation to fit.
    EXPECT_TRUE(memcmp(text, before, sizeof(before)) == 0);

    // A paste larger than the whole buffer is refused the same way.
    char room[LYRICS_TEXT_CAPACITY] = "short";
    char huge[LYRICS_TEXT_CAPACITY*2];
    memset(huge, 'b', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    EXPECT_TRUE(lyrics_text_append(room, sizeof(room), huge, NULL) ==
                LYRICS_ERROR_TEXT_TOO_LONG);
    EXPECT_TRUE(strcmp(room, "short") == 0);
}

TEST(lyrics_text_append_never_splits_a_multi_byte_sequence)
{
    // Fill the draft so only a few bytes remain, then paste a 3-byte codepoint
    // that does not fit. Truncating to the remaining room would leave a
    // continuation byte dangling and the draft could never be applied.
    char text[LYRICS_TEXT_CAPACITY];
    size_t filled = sizeof(text) - 3;
    memset(text, 'a', filled);
    text[filled] = '\0';

    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "\xE6\xBC\xA2", NULL) ==
                LYRICS_ERROR_TEXT_TOO_LONG);
    EXPECT_EQ_SIZE(strlen(text), filled);
    // Still valid on its own terms after the refusal.
    Lyric_Cue cue = {.start_seconds = 0.0, .end_seconds = 1.0};
    memcpy(cue.text, text, filled + 1);
    Lyrics_Document document;
    REQUIRE_TRUE(lyrics_document_init(&document, 10.0) == LYRICS_OK);
    EXPECT_TRUE(lyrics_insert(&document, &cue, NULL) == LYRICS_OK);
}

TEST(lyrics_text_append_rejects_bytes_the_user_could_not_see)
{
    char text[LYRICS_TEXT_CAPACITY] = "keep";

    // A control character is refused outright rather than stripped, so the cue
    // never silently differs from what was copied.
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "bad\x01here", NULL) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(strcmp(text, "keep") == 0);
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "del\x7F", NULL) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(strcmp(text, "keep") == 0);

    // Malformed UTF-8 fails the same validation a stored cue faces.
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "\xE6\xBC", NULL) ==
                LYRICS_ERROR_INVALID_UTF8);
    EXPECT_TRUE(strcmp(text, "keep") == 0);
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "\xFF", NULL) ==
                LYRICS_ERROR_INVALID_UTF8);
    EXPECT_TRUE(strcmp(text, "keep") == 0);

    // An empty clipboard is not an edit.
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), "", NULL) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(strcmp(text, "keep") == 0);

    EXPECT_TRUE(lyrics_text_append(NULL, sizeof(text), "x", NULL) == LYRICS_ERROR_NULL);
    EXPECT_TRUE(lyrics_text_append(text, sizeof(text), NULL, NULL) == LYRICS_ERROR_NULL);
    EXPECT_TRUE(lyrics_text_append(text, 0, "x", NULL) == LYRICS_ERROR_NULL);
}

TEST(lyrics_text_append_joins_halves_of_a_sequence_only_when_valid)
{
    // A draft ending mid-sequence is not reachable through the UI, but the
    // combined text is validated rather than assumed, so splicing the second
    // half produces a whole codepoint instead of two broken ones.
    char text[LYRICS_TEXT_CAPACITY] = {'h', 'i', (char)0xE6, (char)0xBC, '\0'};
    REQUIRE_TRUE(lyrics_text_append(text, sizeof(text), "\xA2", NULL) == LYRICS_OK);
    EXPECT_TRUE(strcmp(text, "hi\xE6\xBC\xA2") == 0);

    // The same splice with a wrong continuation byte is refused whole.
    char broken[LYRICS_TEXT_CAPACITY] = {'h', 'i', (char)0xE6, (char)0xBC, '\0'};
    EXPECT_TRUE(lyrics_text_append(broken, sizeof(broken), "z", NULL) ==
                LYRICS_ERROR_INVALID_UTF8);
    EXPECT_EQ_SIZE(strlen(broken), 4);
}

// A four-cue document, evenly spaced, with room at both ends. Every bulk-move
// test below starts from this shape so the numbers stay checkable by eye.
static void build_shift_fixture(Lyrics_Document *document)
{
    REQUIRE_TRUE(lyrics_document_init(document, 40.0) == LYRICS_OK);
    Lyric_Cue cues[4] = {
        cue(0, 5.0, 6.0, "one"),
        cue(0, 10.0, 11.0, "two"),
        cue(0, 15.0, 16.0, "three"),
        cue(0, 20.0, 21.0, "four"),
    };
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE_TRUE(lyrics_insert(document, &cues[i], NULL) == LYRICS_OK);
    }
}

TEST(lyrics_retime_moves_one_cue_and_keeps_its_text)
{
    Lyrics_Document document;
    build_shift_fixture(&document);
    REQUIRE_TRUE(lyrics_retime(&document, 2, 9.0, 12.5) == LYRICS_OK);
    const Lyric_Cue *moved = lyrics_find(&document, 2);
    REQUIRE_TRUE(moved != NULL);
    EXPECT_NEAR(moved->start_seconds, 9.0, 1e-9);
    EXPECT_NEAR(moved->end_seconds, 12.5, 1e-9);
    EXPECT_TRUE(strcmp(moved->text, "two") == 0);
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_OK);

    // A resize handle dragged past its opposite edge, or off the end of the
    // track, must leave the cue exactly as it was.
    EXPECT_TRUE(lyrics_retime(&document, 2, 12.5, 9.0) == LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(lyrics_retime(&document, 2, 39.0, 41.0) == LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(lyrics_retime(&document, 2, -1.0, 5.0) == LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(lyrics_retime(&document, 999, 1.0, 2.0) == LYRICS_ERROR_NOT_FOUND);
    moved = lyrics_find(&document, 2);
    EXPECT_NEAR(moved->start_seconds, 9.0, 1e-9);
    EXPECT_NEAR(moved->end_seconds, 12.5, 1e-9);
}

TEST(lyrics_shift_many_moves_a_selection_together)
{
    Lyrics_Document document;
    build_shift_fixture(&document);
    const uint64_t selection[2] = {2, 4};
    REQUIRE_TRUE(lyrics_shift_many(&document, selection, 2, 2.5) == LYRICS_OK);
    EXPECT_NEAR(lyrics_find(&document, 1)->start_seconds, 5.0, 1e-9);
    EXPECT_NEAR(lyrics_find(&document, 2)->start_seconds, 12.5, 1e-9);
    EXPECT_NEAR(lyrics_find(&document, 2)->end_seconds, 13.5, 1e-9);
    EXPECT_NEAR(lyrics_find(&document, 3)->start_seconds, 15.0, 1e-9);
    EXPECT_NEAR(lyrics_find(&document, 4)->start_seconds, 22.5, 1e-9);
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_OK);
    EXPECT_TRUE(strcmp(lyrics_find(&document, 2)->text, "two") == 0);
}

TEST(lyrics_shift_many_reorders_when_the_selection_crosses_a_fixed_cue)
{
    Lyrics_Document document;
    build_shift_fixture(&document);
    // Drag cue 1 (5-6 s) past cues 2 and 3 to land between 3 and 4. The stored
    // array must come back canonically ordered, not merely legal in place --
    // a document that is out of order fails validation on save.
    const uint64_t selection[1] = {1};
    REQUIRE_TRUE(lyrics_shift_many(&document, selection, 1, 12.0) == LYRICS_OK);
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_OK);
    EXPECT_EQ_U64(document.cues[0].id, 2);
    EXPECT_EQ_U64(document.cues[1].id, 3);
    EXPECT_EQ_U64(document.cues[2].id, 1);
    EXPECT_EQ_U64(document.cues[3].id, 4);
    EXPECT_NEAR(document.cues[2].start_seconds, 17.0, 1e-9);
}

TEST(lyrics_shift_many_moves_everything_or_nothing)
{
    Lyrics_Document document;
    build_shift_fixture(&document);
    // Cue 4 ends at 21 s in a 40 s track, so it has 19 s of headroom; cue 1
    // starts at 5 s, so the selection as a whole can only go back 5 s. Applying
    // the move cue by cue would have shifted cue 4 and then failed on cue 1,
    // leaving the document half-moved with no way back.
    const uint64_t selection[2] = {1, 4};
    EXPECT_TRUE(lyrics_shift_many(&document, selection, 2, -6.0) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_NEAR(lyrics_find(&document, 1)->start_seconds, 5.0, 1e-9);
    EXPECT_NEAR(lyrics_find(&document, 4)->start_seconds, 20.0, 1e-9);

    EXPECT_TRUE(lyrics_shift_many(&document, selection, 2, 20.0) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_NEAR(lyrics_find(&document, 4)->end_seconds, 21.0, 1e-9);

    // Exactly to the boundary is allowed: -5 s puts cue 1 at zero.
    REQUIRE_TRUE(lyrics_shift_many(&document, selection, 2, -5.0) == LYRICS_OK);
    EXPECT_NEAR(lyrics_find(&document, 1)->start_seconds, 0.0, 1e-9);
    EXPECT_NEAR(lyrics_find(&document, 4)->start_seconds, 15.0, 1e-9);
    EXPECT_TRUE(lyrics_document_validate(&document).result == LYRICS_OK);
}

TEST(lyrics_shift_many_collapses_repeats_and_rejects_a_stale_id)
{
    Lyrics_Document document;
    build_shift_fixture(&document);
    // The lane's selection is a list, and a double ctrl+click could put the
    // same cue in it twice. Shifting it twice would be silently wrong.
    const uint64_t repeated[3] = {2, 2, 2};
    REQUIRE_TRUE(lyrics_shift_many(&document, repeated, 3, 1.0) == LYRICS_OK);
    EXPECT_NEAR(lyrics_find(&document, 2)->start_seconds, 11.0, 1e-9);

    uint64_t revision = document.revision;
    const uint64_t stale[2] = {2, 77};
    EXPECT_TRUE(lyrics_shift_many(&document, stale, 2, 1.0) == LYRICS_ERROR_NOT_FOUND);
    EXPECT_NEAR(lyrics_find(&document, 2)->start_seconds, 11.0, 1e-9);
    EXPECT_EQ_U64(document.revision, revision);

    EXPECT_TRUE(lyrics_shift_many(&document, NULL, 0, 1.0) == LYRICS_ERROR_NOT_FOUND);
    EXPECT_TRUE(lyrics_shift_many(&document, repeated, 3, NAN) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_TRUE(lyrics_shift_many(&document, repeated, 3, INFINITY) ==
                LYRICS_ERROR_INVALID_CUE);
    EXPECT_EQ_U64(document.revision, revision);
}

TEST(lyrics_shift_headroom_matches_what_shift_many_accepts)
{
    Lyrics_Document document;
    build_shift_fixture(&document);
    const uint64_t selection[2] = {1, 4};
    double backward = -1.0, forward = -1.0;
    REQUIRE_TRUE(lyrics_shift_headroom(&document, selection, 2,
                                       &backward, &forward) == LYRICS_OK);
    EXPECT_NEAR(backward, 5.0, 1e-9);
    EXPECT_NEAR(forward, 19.0, 1e-9);

    // The contract that makes a live drag clamp correctly: the reported
    // headroom is exactly the largest delta the commit will take. Verified in
    // both directions on a fresh copy so the two probes do not interact.
    Lyrics_Document probe = document;
    EXPECT_TRUE(lyrics_shift_many(&probe, selection, 2, -backward) == LYRICS_OK);
    probe = document;
    EXPECT_TRUE(lyrics_shift_many(&probe, selection, 2, forward) == LYRICS_OK);
    probe = document;
    EXPECT_TRUE(lyrics_shift_many(&probe, selection, 2, -backward - 0.001) ==
                LYRICS_ERROR_INVALID_CUE);
    probe = document;
    EXPECT_TRUE(lyrics_shift_many(&probe, selection, 2, forward + 0.001) ==
                LYRICS_ERROR_INVALID_CUE);

    // A selection already flush against the start reports no room to give.
    REQUIRE_TRUE(lyrics_shift_many(&document, selection, 2, -5.0) == LYRICS_OK);
    REQUIRE_TRUE(lyrics_shift_headroom(&document, selection, 2,
                                       &backward, &forward) == LYRICS_OK);
    EXPECT_NEAR(backward, 0.0, 1e-9);
}
