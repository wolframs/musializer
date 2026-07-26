#ifndef MUSIALIZER_LYRICS_H_
#define MUSIALIZER_LYRICS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// This model owns transcribed/written lyric content and its editorial timing.
// Semantic interpretations belong in a separate analysis lane.
#define LYRICS_DOCUMENT_SCHEMA_VERSION 1u
#define LYRICS_CUE_CAPACITY 1024u
#define LYRICS_TEXT_CAPACITY 512u
#define LYRICS_BRIDGE_VERSION 1u
#define LYRICS_BRIDGE_MAX_BYTES \
    (64u + LYRICS_CUE_CAPACITY*(64u + 4u*((LYRICS_TEXT_CAPACITY + 2u)/3u)))

typedef struct Lyric_Cue {
    uint64_t id;
    double start_seconds;
    double end_seconds;
    char text[LYRICS_TEXT_CAPACITY];
} Lyric_Cue;

typedef struct Lyrics_Document {
    uint32_t schema_version;
    uint32_t reserved;
    double duration_seconds;
    uint64_t next_id;
    uint64_t revision;
    size_t count;
    Lyric_Cue cues[LYRICS_CUE_CAPACITY];
} Lyrics_Document;

typedef enum Lyrics_Result {
    LYRICS_OK = 0,
    LYRICS_ERROR_NULL,
    LYRICS_ERROR_SCHEMA,
    LYRICS_ERROR_DURATION,
    LYRICS_ERROR_CAPACITY,
    LYRICS_ERROR_INVALID_CUE,
    LYRICS_ERROR_INVALID_UTF8,
    LYRICS_ERROR_TEXT_TOO_LONG,
    LYRICS_ERROR_DUPLICATE_ID,
    LYRICS_ERROR_ID_EXHAUSTED,
    LYRICS_ERROR_NOT_FOUND,
    LYRICS_ERROR_ORDER,
    LYRICS_ERROR_NOT_ADJACENT,
    LYRICS_ERROR_BUFFER_TOO_SMALL,
    LYRICS_ERROR_BRIDGE_FORMAT,
    LYRICS_ERROR_ALLOCATION
} Lyrics_Result;

typedef struct Lyrics_Validation {
    Lyrics_Result result;
    size_t index;
    size_t related_index;
} Lyrics_Validation;

// duration_seconds is part of the persistence contract and bounds every cue.
Lyrics_Result lyrics_document_init(Lyrics_Document *document, double duration_seconds);
Lyrics_Validation lyrics_document_validate(const Lyrics_Document *document);
const char *lyrics_result_string(Lyrics_Result result);

// Appends `addition` to the NUL-terminated `text` buffer in full or not at all,
// so a rejected paste leaves the draft byte-for-byte as it was. Truncating to
// fit is specifically not allowed: it would cut a multi-byte sequence in half
// and the result would fail the same validation every stored cue must pass.
//
// A cue is a single line by contract, so line breaks and tabs in the addition
// collapse to single spaces and *flattened reports it, letting the caller say
// what it did. Any other control character rejects the whole paste rather than
// being stripped, so what lands in the cue is always what the user can see.
Lyrics_Result lyrics_text_append(char *text, size_t capacity,
                                 const char *addition, bool *flattened);

// Passing cue.id == 0 allocates a deterministic, never-reused stable id.
// Explicit nonzero ids support import and persistence round-trips.
Lyrics_Result lyrics_insert(Lyrics_Document *document, const Lyric_Cue *cue,
                            uint64_t *inserted_id);
Lyrics_Result lyrics_update(Lyrics_Document *document, uint64_t id,
                            double start_seconds, double end_seconds,
                            const char *text);
Lyrics_Result lyrics_delete(Lyrics_Document *document, uint64_t id);
Lyrics_Result lyrics_nudge(Lyrics_Document *document, uint64_t id,
                           double delta_seconds);

// split keeps id on the left cue and allocates a new id for the right cue.
Lyrics_Result lyrics_split(Lyrics_Document *document, uint64_t id,
                           double split_seconds, const char *left_text,
                           const char *right_text, uint64_t *right_id);

// merge requires two consecutive cues in canonical order, keeps the first
// cue's id, spans both timings, joins text with separator, and removes second.
Lyrics_Result lyrics_merge(Lyrics_Document *document, uint64_t first_id,
                           uint64_t second_id, const char *separator);

const Lyric_Cue *lyrics_find(const Lyrics_Document *document, uint64_t id);
Lyric_Cue *lyrics_find_mut(Lyrics_Document *document, uint64_t id);

// Validates source before replacing destination and advances destination's
// revision once. Useful when a JSON/sidecar parser is owned by another layer.
Lyrics_Result lyrics_document_replace(Lyrics_Document *destination,
                                      const Lyrics_Document *source);

// Copies a validated document onto an authoritative decoded-audio duration.
// Cues crossing the tail are clamped; cues beginning at/after the new end are
// rejected atomically.
Lyrics_Result lyrics_document_normalize_duration(
    Lyrics_Document *destination,
    const Lyrics_Document *source,
    double duration_seconds);

// Returns the most recently-started active cue, or NULL outside all cues.
const Lyric_Cue *lyrics_at_time(const Lyrics_Document *document,
                                double time_seconds);

// Derived UI bridge (not the canonical persistence format):
// MUSIALIZER-LYRICS-BRIDGE<TAB>1<TAB>duration_ms<LF>
// id<TAB>start_ms<TAB>end_ms<TAB>base64_utf8_text<LF>
// Export is canonical and locale-independent. Passing output == NULL queries
// required_size (including the trailing NUL). Import is strict and atomic.
Lyrics_Result lyrics_bridge_export(const Lyrics_Document *document,
                                    char *output, size_t output_capacity,
                                    size_t *required_size);
Lyrics_Result lyrics_bridge_import(Lyrics_Document *document,
                                    const char *input, size_t input_size);

#endif // MUSIALIZER_LYRICS_H_
