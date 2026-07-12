#ifndef MUSIALIZER_ANALYSIS_CANDIDATE_H_
#define MUSIALIZER_ANALYSIS_CANDIDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "analysis_bridge.h"
#include "event_timeline.h"
#include "lyrics.h"
#include "scene_switch.h"

#define ANALYSIS_CANDIDATE_SCHEMA_VERSION 1u

typedef enum Analysis_Candidate_Lane {
    ANALYSIS_CANDIDATE_LYRICS = 1u << 0,
    ANALYSIS_CANDIDATE_SECTIONS = 1u << 1,
    ANALYSIS_CANDIDATE_SEMANTICS = 1u << 2,
    ANALYSIS_CANDIDATE_ALL = ANALYSIS_CANDIDATE_LYRICS |
                             ANALYSIS_CANDIDATE_SECTIONS |
                             ANALYSIS_CANDIDATE_SEMANTICS,
} Analysis_Candidate_Lane;

typedef struct Analysis_Candidate {
    uint32_t schema_version;
    uint32_t authorized_lanes;
    uint32_t available_lanes;
    uint32_t reserved;

    Lyrics_Document lyrics;
    Scene_Switch_Timeline sections;
    Event_Timeline semantic_events;

    size_t uncertain_lyric_count;
    size_t semantic_note_count;
} Analysis_Candidate;

typedef enum Analysis_Candidate_Result {
    ANALYSIS_CANDIDATE_OK = 0,
    ANALYSIS_CANDIDATE_ERROR_NULL,
    ANALYSIS_CANDIDATE_ERROR_AUTHORITY,
    ANALYSIS_CANDIDATE_ERROR_DURATION,
    ANALYSIS_CANDIDATE_ERROR_LYRICS,
    ANALYSIS_CANDIDATE_ERROR_SECTIONS,
    ANALYSIS_CANDIDATE_ERROR_SEMANTICS,
    ANALYSIS_CANDIDATE_ERROR_SCHEMA,
} Analysis_Candidate_Result;

// Converts a parsed bridge into validated, mode-bounded candidate state.
// Nothing in the destination editor is mutated until analysis_candidate_apply.
Analysis_Candidate_Result analysis_candidate_prepare(
    Analysis_Candidate *destination,
    const Analysis_Bridge *bridge,
    uint32_t authorized_lanes,
    double duration_seconds,
    uint32_t scene_count);

// Revalidates a sections lane against the decoder's authoritative duration.
// The candidate is unchanged when normalization would make the final cue
// invalid (for example, when decoder padding ends before its start).
Analysis_Candidate_Result analysis_candidate_normalize_sections(
    Analysis_Candidate *candidate,
    double duration_seconds,
    uint32_t scene_count);

// Applies only lanes both authorized and present in the candidate. Suggestions
// for sections preserve the user's existing auto-scene opt-in state.
Analysis_Candidate_Result analysis_candidate_apply(
    const Analysis_Candidate *candidate,
    Lyrics_Document *lyrics,
    Scene_Switch_Timeline *sections,
    Event_Timeline *semantic_events);

const char *analysis_candidate_result_string(Analysis_Candidate_Result result);

#endif // MUSIALIZER_ANALYSIS_CANDIDATE_H_
