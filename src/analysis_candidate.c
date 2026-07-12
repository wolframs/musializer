#include "analysis_candidate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool lanes_are_valid(uint32_t lanes)
{
    return lanes != 0 && (lanes & ~((uint32_t)ANALYSIS_CANDIDATE_ALL)) == 0;
}

static Analysis_Candidate_Result prepare_lyrics(Analysis_Candidate *candidate,
                                                 const Analysis_Bridge *bridge)
{
    if (!bridge->lyrics_present) return ANALYSIS_CANDIDATE_OK;
    Lyrics_Validation validation = lyrics_document_validate(&bridge->lyrics);
    if (validation.result != LYRICS_OK) return ANALYSIS_CANDIDATE_ERROR_LYRICS;

    candidate->lyrics = bridge->lyrics;
    candidate->available_lanes |= ANALYSIS_CANDIDATE_LYRICS;
    for (size_t i = 0; i < bridge->lyric_metadata_count; ++i) {
        if (bridge->lyric_metadata[i].uncertain) ++candidate->uncertain_lyric_count;
    }
    return ANALYSIS_CANDIDATE_OK;
}

static Analysis_Candidate_Result prepare_sections(Analysis_Candidate *candidate,
                                                   const Analysis_Bridge *bridge,
                                                   double duration_seconds,
                                                   uint32_t scene_count)
{
    if (!bridge->sections_present) return ANALYSIS_CANDIDATE_OK;
    Scene_Switch_Cue cues[SCENE_SWITCH_CAPACITY];
    if (bridge->section_count > SCENE_SWITCH_CAPACITY) {
        return ANALYSIS_CANDIDATE_ERROR_SECTIONS;
    }
    for (size_t i = 0; i < bridge->section_count; ++i) {
        const Analysis_Section *source = &bridge->sections[i];
        cues[i] = (Scene_Switch_Cue) {
            .id = source->id,
            .start_seconds = (double)source->start_ms/1000.0,
            .end_seconds = (double)source->end_ms/1000.0,
            // Analysis_Scene is a stable bridge enum whose order mirrors the
            // built-in scene registry. scene_switch_replace bounds it again.
            .scene_index = (uint32_t)source->recommended_scene,
            .strength = (float)source->transition_strength_milli/1000.0f,
        };
    }
    scene_switch_init(&candidate->sections);
    Scene_Switch_Result result = scene_switch_replace(
        &candidate->sections, cues, bridge->section_count,
        duration_seconds, scene_count);
    if (result != SCENE_SWITCH_OK) return ANALYSIS_CANDIDATE_ERROR_SECTIONS;
    candidate->available_lanes |= ANALYSIS_CANDIDATE_SECTIONS;
    return ANALYSIS_CANDIDATE_OK;
}

static Analysis_Candidate_Result prepare_semantics(Analysis_Candidate *candidate,
                                                    const Analysis_Bridge *bridge)
{
    candidate->semantic_note_count = bridge->semantic_notes_present ?
                                     bridge->semantic_note_count : 0;
    event_timeline_init(&candidate->semantic_events);
    if (!bridge->semantic_cues_present) return ANALYSIS_CANDIDATE_OK;

    for (size_t i = 0; i < bridge->semantic_cue_count; ++i) {
        const Analysis_Semantic_Cue *cue = &bridge->semantic_cues[i];
        uint64_t event_id = cue->id ^ UINT64_C(0x4D494D4F00000000);
        if (event_id == 0) event_id = 1;
        Event_Record event = {
            .timestamp_seconds = (double)cue->start_ms/1000.0,
            .id = event_id,
            .type = EVENT_TYPE_SEMANTIC,
            .value_count = 4,
            .values = {
                (float)cue->energy_milli/1000.0f,
                (float)cue->tension_milli/1000.0f,
                (float)cue->valence_milli/1000.0f,
                (float)cue->confidence_milli/1000.0f,
            },
        };
        if (event_timeline_record(&candidate->semantic_events, &event) !=
            EVENT_TIMELINE_OK) {
            return ANALYSIS_CANDIDATE_ERROR_SEMANTICS;
        }
    }
    candidate->available_lanes |= ANALYSIS_CANDIDATE_SEMANTICS;
    return ANALYSIS_CANDIDATE_OK;
}

Analysis_Candidate_Result analysis_candidate_prepare(
    Analysis_Candidate *destination,
    const Analysis_Bridge *bridge,
    uint32_t authorized_lanes,
    double duration_seconds,
    uint32_t scene_count)
{
    if (destination == NULL || bridge == NULL) return ANALYSIS_CANDIDATE_ERROR_NULL;
    if (!lanes_are_valid(authorized_lanes)) return ANALYSIS_CANDIDATE_ERROR_AUTHORITY;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0 || scene_count == 0) {
        return ANALYSIS_CANDIDATE_ERROR_DURATION;
    }
    if (bridge->schema_version != ANALYSIS_BRIDGE_SCHEMA_VERSION) {
        return ANALYSIS_CANDIDATE_ERROR_SCHEMA;
    }

    Analysis_Candidate *candidate = calloc(1, sizeof(*candidate));
    if (candidate == NULL) return ANALYSIS_CANDIDATE_ERROR_NULL;
    candidate->schema_version = ANALYSIS_CANDIDATE_SCHEMA_VERSION;
    candidate->authorized_lanes = authorized_lanes;

    Analysis_Candidate_Result result = ANALYSIS_CANDIDATE_OK;
    if ((authorized_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0) {
        result = prepare_lyrics(candidate, bridge);
        if (result != ANALYSIS_CANDIDATE_OK) goto cleanup;
    }
    if ((authorized_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0) {
        result = prepare_sections(candidate, bridge, duration_seconds, scene_count);
        if (result != ANALYSIS_CANDIDATE_OK) goto cleanup;
    }
    if ((authorized_lanes & ANALYSIS_CANDIDATE_SEMANTICS) != 0) {
        result = prepare_semantics(candidate, bridge);
        if (result != ANALYSIS_CANDIDATE_OK) goto cleanup;
    }

    memcpy(destination, candidate, sizeof(*destination));
cleanup:
    free(candidate);
    return result;
}

Analysis_Candidate_Result analysis_candidate_normalize_sections(
    Analysis_Candidate *candidate,
    double duration_seconds,
    uint32_t scene_count)
{
    if (candidate == NULL) return ANALYSIS_CANDIDATE_ERROR_NULL;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0 || scene_count == 0) {
        return ANALYSIS_CANDIDATE_ERROR_DURATION;
    }
    if (candidate->schema_version != ANALYSIS_CANDIDATE_SCHEMA_VERSION) {
        return ANALYSIS_CANDIDATE_ERROR_SCHEMA;
    }
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) == 0 ||
        candidate->sections.count == 0) return ANALYSIS_CANDIDATE_OK;

    Scene_Switch_Cue cues[SCENE_SWITCH_CAPACITY];
    if (candidate->sections.count > SCENE_SWITCH_CAPACITY) {
        return ANALYSIS_CANDIDATE_ERROR_SECTIONS;
    }
    memcpy(cues, candidate->sections.cues,
           candidate->sections.count*sizeof(cues[0]));
    cues[candidate->sections.count - 1].end_seconds = duration_seconds;
    Scene_Switch_Timeline normalized;
    scene_switch_init(&normalized);
    if (scene_switch_replace(&normalized, cues, candidate->sections.count,
                             duration_seconds, scene_count) != SCENE_SWITCH_OK) {
        return ANALYSIS_CANDIDATE_ERROR_SECTIONS;
    }
    normalized.enabled = candidate->sections.enabled;
    candidate->sections = normalized;
    return ANALYSIS_CANDIDATE_OK;
}

Analysis_Candidate_Result analysis_candidate_apply(
    const Analysis_Candidate *candidate,
    Lyrics_Document *lyrics,
    Scene_Switch_Timeline *sections,
    Event_Timeline *semantic_events)
{
    if (candidate == NULL || lyrics == NULL || sections == NULL ||
        semantic_events == NULL) return ANALYSIS_CANDIDATE_ERROR_NULL;
    if (candidate->schema_version != ANALYSIS_CANDIDATE_SCHEMA_VERSION) {
        return ANALYSIS_CANDIDATE_ERROR_SCHEMA;
    }
    if (!lanes_are_valid(candidate->authorized_lanes) ||
        (candidate->available_lanes & ~candidate->authorized_lanes) != 0) {
        return ANALYSIS_CANDIDATE_ERROR_AUTHORITY;
    }

    // Every source was validated during prepare, so these replacements cannot
    // fail for content. Apply the independently revisioned documents first and
    // only then publish the trivially-copyable section timeline.
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_LYRICS) != 0 &&
        lyrics_document_replace(lyrics, &candidate->lyrics) != LYRICS_OK) {
        return ANALYSIS_CANDIDATE_ERROR_LYRICS;
    }
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SEMANTICS) != 0 &&
        event_timeline_replace(semantic_events, &candidate->semantic_events) !=
        EVENT_TIMELINE_OK) {
        return ANALYSIS_CANDIDATE_ERROR_SEMANTICS;
    }
    if ((candidate->available_lanes & ANALYSIS_CANDIDATE_SECTIONS) != 0) {
        bool enabled = sections->enabled;
        *sections = candidate->sections;
        sections->enabled = enabled;
        scene_switch_reset(sections);
    }
    return ANALYSIS_CANDIDATE_OK;
}

const char *analysis_candidate_result_string(Analysis_Candidate_Result result)
{
    static const char *const names[] = {
        "ok", "null argument", "invalid lane authority", "invalid duration",
        "invalid lyric candidate", "invalid scene candidate",
        "invalid semantic candidate", "unsupported candidate schema",
    };
    if ((unsigned)result >= sizeof(names)/sizeof(names[0])) return "unknown candidate error";
    return names[result];
}
