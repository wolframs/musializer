#include "analysis_candidate.h"
#include "test_support.h"

#include <string.h>

static Analysis_Bridge sample_bridge(void)
{
    Analysis_Bridge bridge;
    analysis_bridge_init(&bridge);
    strcpy(bridge.audio_sha256,
           "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    bridge.duration_ms = 10000;

    bridge.lyrics_present = true;
    (void)lyrics_document_init(&bridge.lyrics, 10.0);
    Lyric_Cue lyric = {.id = 7, .start_seconds = 1.0, .end_seconds = 2.0};
    strcpy(lyric.text, "candidate line");
    (void)lyrics_insert(&bridge.lyrics, &lyric, NULL);
    bridge.lyric_metadata_count = 1;
    bridge.lyric_metadata[0] = (Analysis_Lyric_Metadata) {
        .id = 7, .confidence_milli = 610, .uncertain = true,
    };

    bridge.sections_present = true;
    bridge.section_count = 2;
    bridge.sections[0] = (Analysis_Section) {
        .id = 10, .start_ms = 0, .end_ms = 5000,
        .recommended_scene = ANALYSIS_SCENE_ORBITAL,
        .transition_strength_milli = 600,
    };
    bridge.sections[1] = (Analysis_Section) {
        .id = 11, .start_ms = 5000, .end_ms = 10000,
        .recommended_scene = ANALYSIS_SCENE_CONSTELLATION,
        .transition_strength_milli = 800,
    };

    bridge.semantic_cues_present = true;
    bridge.semantic_cue_count = 1;
    bridge.semantic_cues[0] = (Analysis_Semantic_Cue) {
        .id = 20, .start_ms = 0, .end_ms = 10000,
        .energy_milli = 900, .tension_milli = 700,
        .valence_milli = -200, .confidence_milli = 750,
    };
    bridge.semantic_notes_present = true;
    bridge.semantic_note_count = 1;
    return bridge;
}

TEST(analysis_candidate_stages_only_authorized_lanes)
{
    Analysis_Bridge bridge = sample_bridge();
    Analysis_Candidate candidate;

    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_LYRICS, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(candidate.available_lanes, ANALYSIS_CANDIDATE_LYRICS);
    EXPECT_EQ_SIZE(candidate.lyrics.count, 1);
    EXPECT_EQ_SIZE(candidate.sections.count, 0);
    EXPECT_EQ_SIZE(candidate.semantic_events.count, 0);
    EXPECT_EQ_SIZE(candidate.uncertain_lyric_count, 1);

    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_SECTIONS, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(candidate.available_lanes, ANALYSIS_CANDIDATE_SECTIONS);
    EXPECT_EQ_SIZE(candidate.lyrics.count, 0);
    EXPECT_EQ_SIZE(candidate.sections.count, 2);
    EXPECT_EQ_SIZE(candidate.semantic_events.count, 0);

    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_SEMANTICS, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(candidate.available_lanes, ANALYSIS_CANDIDATE_SEMANTICS);
    EXPECT_EQ_SIZE(candidate.lyrics.count, 0);
    EXPECT_EQ_SIZE(candidate.sections.count, 0);
    EXPECT_EQ_SIZE(candidate.semantic_events.count, 1);
    EXPECT_EQ_SIZE(candidate.semantic_note_count, 1);
}

TEST(analysis_candidate_does_not_mutate_until_explicit_apply)
{
    Analysis_Bridge bridge = sample_bridge();
    Analysis_Candidate candidate;
    Lyrics_Document lyrics;
    Scene_Switch_Timeline sections;
    Event_Timeline semantics;
    REQUIRE_TRUE(lyrics_document_init(&lyrics, 10.0) == LYRICS_OK);
    Lyric_Cue authored = {.id = 1, .start_seconds = 0.0, .end_seconds = 1.0};
    strcpy(authored.text, "authored line");
    REQUIRE_TRUE(lyrics_insert(&lyrics, &authored, NULL) == LYRICS_OK);
    scene_switch_init(&sections);
    event_timeline_init(&semantics);

    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_ALL, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(lyrics.count, 1);
    EXPECT_TRUE(strcmp(lyrics.cues[0].text, "authored line") == 0);
    EXPECT_EQ_SIZE(sections.count, 0);
    EXPECT_EQ_SIZE(semantics.count, 0);

    uint64_t lyric_revision = lyrics.revision;
    uint64_t semantic_revision = semantics.revision;
    REQUIRE_TRUE(analysis_candidate_apply(
        &candidate, &lyrics, &sections, &semantics) == ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(lyrics.count, 1);
    EXPECT_TRUE(strcmp(lyrics.cues[0].text, "candidate line") == 0);
    EXPECT_TRUE(lyrics.revision > lyric_revision);
    EXPECT_EQ_SIZE(sections.count, 2);
    EXPECT_EQ_SIZE(semantics.count, 1);
    EXPECT_TRUE(semantics.revision > semantic_revision);
}

TEST(analysis_candidate_preserves_auto_scene_opt_in)
{
    Analysis_Bridge bridge = sample_bridge();
    Analysis_Candidate candidate;
    Lyrics_Document lyrics;
    Scene_Switch_Timeline sections;
    Event_Timeline semantics;
    REQUIRE_TRUE(lyrics_document_init(&lyrics, 10.0) == LYRICS_OK);
    scene_switch_init(&sections);
    sections.enabled = true;
    event_timeline_init(&semantics);

    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_SECTIONS, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    REQUIRE_TRUE(analysis_candidate_apply(
        &candidate, &lyrics, &sections, &semantics) == ANALYSIS_CANDIDATE_OK);
    EXPECT_TRUE(sections.enabled);
    EXPECT_EQ_SIZE(sections.active_index, SIZE_MAX);
}

TEST(analysis_candidate_section_duration_normalization_is_valid_and_atomic)
{
    Analysis_Bridge bridge = sample_bridge();
    Analysis_Candidate candidate;
    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_SECTIONS, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    candidate.sections.enabled = true;
    REQUIRE_TRUE(analysis_candidate_normalize_sections(&candidate, 9.9, 7) ==
                 ANALYSIS_CANDIDATE_OK);
    EXPECT_NEAR(candidate.sections.cues[1].end_seconds, 9.9, 0.0);
    EXPECT_TRUE(candidate.sections.enabled);

    Analysis_Candidate before = candidate;
    EXPECT_TRUE(analysis_candidate_normalize_sections(&candidate, 4.9, 7) ==
                ANALYSIS_CANDIDATE_ERROR_SECTIONS);
    EXPECT_TRUE(memcmp(&candidate, &before, sizeof(candidate)) == 0);
}

TEST(analysis_candidate_notes_without_cues_do_not_clear_semantic_events)
{
    Analysis_Bridge bridge = sample_bridge();
    bridge.semantic_cues_present = false;
    bridge.semantic_cue_count = 0;
    Analysis_Candidate candidate;
    Lyrics_Document lyrics;
    Scene_Switch_Timeline sections;
    Event_Timeline semantics;
    REQUIRE_TRUE(lyrics_document_init(&lyrics, 10.0) == LYRICS_OK);
    scene_switch_init(&sections);
    event_timeline_init(&semantics);
    Event_Record existing = {
        .timestamp_seconds = 2.0, .id = 9, .type = EVENT_TYPE_SEMANTIC,
        .value_count = 1, .values = {0.5f},
    };
    REQUIRE_TRUE(event_timeline_record(&semantics, &existing) == EVENT_TIMELINE_OK);

    REQUIRE_TRUE(analysis_candidate_prepare(
        &candidate, &bridge, ANALYSIS_CANDIDATE_SEMANTICS, 10.0, 7) ==
        ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(candidate.available_lanes, 0);
    EXPECT_EQ_SIZE(candidate.semantic_note_count, 1);
    REQUIRE_TRUE(analysis_candidate_apply(
        &candidate, &lyrics, &sections, &semantics) == ANALYSIS_CANDIDATE_OK);
    EXPECT_EQ_SIZE(semantics.count, 1);
    EXPECT_EQ_U64(semantics.events[0].id, 9);
}

TEST(analysis_candidate_rejects_invalid_authority_without_touching_output)
{
    Analysis_Bridge bridge = sample_bridge();
    Analysis_Candidate candidate;
    memset(&candidate, 0x5a, sizeof(candidate));
    Analysis_Candidate before = candidate;
    EXPECT_EQ_SIZE(analysis_candidate_prepare(&candidate, &bridge, 0, 10.0, 7),
                   ANALYSIS_CANDIDATE_ERROR_AUTHORITY);
    EXPECT_TRUE(memcmp(&candidate, &before, sizeof(candidate)) == 0);
    EXPECT_EQ_SIZE(analysis_candidate_prepare(&candidate, &bridge, 0x80, 10.0, 7),
                   ANALYSIS_CANDIDATE_ERROR_AUTHORITY);
    EXPECT_TRUE(memcmp(&candidate, &before, sizeof(candidate)) == 0);
}
