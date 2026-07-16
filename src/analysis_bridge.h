#ifndef MUSIALIZER_ANALYSIS_BRIDGE_H_
#define MUSIALIZER_ANALYSIS_BRIDGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lyrics.h"

#define ANALYSIS_BRIDGE_SCHEMA_VERSION 1u
#define ANALYSIS_BRIDGE_INPUT_MAX_BYTES (4u*1024u*1024u)
#define ANALYSIS_BRIDGE_SHA256_CAPACITY 65u
#define ANALYSIS_BRIDGE_SECTION_CAPACITY 256u
#define ANALYSIS_BRIDGE_SEMANTIC_CAPACITY 256u
#define ANALYSIS_BRIDGE_NOTE_CAPACITY 16u
#define ANALYSIS_BRIDGE_REASONS_CAPACITY 4096u
#define ANALYSIS_BRIDGE_SUMMARY_CAPACITY 2048u
#define ANALYSIS_BRIDGE_NOTE_TEXT_CAPACITY 16384u

typedef enum Analysis_Scene {
    ANALYSIS_SCENE_SPECTRUM = 0,
    ANALYSIS_SCENE_PULSE,
    ANALYSIS_SCENE_ORBITAL,
    ANALYSIS_SCENE_ASCII,
    ANALYSIS_SCENE_ATLAS,
    ANALYSIS_SCENE_TERRARIUM,
    ANALYSIS_SCENE_CONSTELLATION,
    ANALYSIS_SCENE_CADENCE,
    ANALYSIS_SCENE_LOOM,
    ANALYSIS_SCENE_PENTAGRAM,
    ANALYSIS_SCENE_COUNT
} Analysis_Scene;

typedef struct Analysis_Lyric_Metadata {
    uint64_t id;
    int16_t confidence_milli; // -1 means absent
    bool uncertain;
} Analysis_Lyric_Metadata;

typedef struct Analysis_Section {
    uint64_t id;
    uint64_t start_ms;
    uint64_t end_ms;
    Analysis_Scene recommended_scene;
    uint16_t transition_strength_milli;
    char reasons_json[ANALYSIS_BRIDGE_REASONS_CAPACITY];
} Analysis_Section;

typedef struct Analysis_Semantic_Cue {
    uint64_t id;
    uint64_t start_ms;
    uint64_t end_ms;
    uint16_t energy_milli;
    uint16_t tension_milli;
    int16_t valence_milli;
    uint16_t confidence_milli;
    char summary[ANALYSIS_BRIDGE_SUMMARY_CAPACITY];
} Analysis_Semantic_Cue;

typedef struct Analysis_Semantic_Note {
    uint64_t id;
    char text[ANALYSIS_BRIDGE_NOTE_TEXT_CAPACITY];
} Analysis_Semantic_Note;

typedef struct Analysis_Bridge {
    uint32_t schema_version;
    uint32_t reserved;
    char audio_sha256[ANALYSIS_BRIDGE_SHA256_CAPACITY];
    uint64_t duration_ms;

    bool lyrics_present;
    bool sections_present;
    bool semantic_cues_present;
    bool semantic_notes_present;

    Lyrics_Document lyrics;
    Analysis_Lyric_Metadata lyric_metadata[LYRICS_CUE_CAPACITY];
    size_t lyric_metadata_count;

    Analysis_Section sections[ANALYSIS_BRIDGE_SECTION_CAPACITY];
    size_t section_count;
    Analysis_Semantic_Cue semantic_cues[ANALYSIS_BRIDGE_SEMANTIC_CAPACITY];
    size_t semantic_cue_count;
    Analysis_Semantic_Note semantic_notes[ANALYSIS_BRIDGE_NOTE_CAPACITY];
    size_t semantic_note_count;
} Analysis_Bridge;

typedef enum Analysis_Bridge_Result {
    ANALYSIS_BRIDGE_OK = 0,
    ANALYSIS_BRIDGE_ERROR_NULL,
    ANALYSIS_BRIDGE_ERROR_INPUT_SIZE,
    ANALYSIS_BRIDGE_ERROR_HEADER,
    ANALYSIS_BRIDGE_ERROR_AUDIO,
    ANALYSIS_BRIDGE_ERROR_AUDIO_MISMATCH,
    ANALYSIS_BRIDGE_ERROR_RECORD,
    ANALYSIS_BRIDGE_ERROR_ORDER,
    ANALYSIS_BRIDGE_ERROR_DUPLICATE_ID,
    ANALYSIS_BRIDGE_ERROR_RANGE,
    ANALYSIS_BRIDGE_ERROR_SCENE,
    ANALYSIS_BRIDGE_ERROR_BASE64,
    ANALYSIS_BRIDGE_ERROR_UTF8,
    ANALYSIS_BRIDGE_ERROR_DECODED_SIZE,
    ANALYSIS_BRIDGE_ERROR_CAPACITY,
    ANALYSIS_BRIDGE_ERROR_COVERAGE,
    ANALYSIS_BRIDGE_ERROR_ALLOCATION
} Analysis_Bridge_Result;

void analysis_bridge_init(Analysis_Bridge *bridge);

// expected_audio_sha256 may be NULL and expected_duration_ms may be zero when
// the caller only needs syntactic validation. Otherwise they are exact guards
// against attaching cached analysis to the wrong audio. Parsing is atomic.
Analysis_Bridge_Result analysis_bridge_parse(Analysis_Bridge *destination,
                                             const char *input, size_t input_size,
                                             const char *expected_audio_sha256,
                                             uint64_t expected_duration_ms);
const char *analysis_bridge_result_string(Analysis_Bridge_Result result);
const char *analysis_scene_name(Analysis_Scene scene);

#endif // MUSIALIZER_ANALYSIS_BRIDGE_H_
