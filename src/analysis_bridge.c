#include "analysis_bridge.h"

#include <stdlib.h>
#include <string.h>

#define ANALYSIS_BRIDGE_MAX_IDS \
    (LYRICS_CUE_CAPACITY + ANALYSIS_BRIDGE_SECTION_CAPACITY + \
     ANALYSIS_BRIDGE_SEMANTIC_CAPACITY + ANALYSIS_BRIDGE_NOTE_CAPACITY)
#define ANALYSIS_BRIDGE_MAX_EXACT_MS UINT64_C(9007199254740991)

typedef struct Field {
    const char *data;
    size_t length;
} Field;

static const char *const scene_names[ANALYSIS_SCENE_COUNT] = {
    "spectrum", "pulse", "orbital", "ascii", "atlas", "terrarium", "constellation",
    "cadence", "loom"
};

void analysis_bridge_init(Analysis_Bridge *bridge)
{
    if (bridge == NULL) return;
    memset(bridge, 0, sizeof(*bridge));
    bridge->schema_version = ANALYSIS_BRIDGE_SCHEMA_VERSION;
}

const char *analysis_scene_name(Analysis_Scene scene)
{
    if (scene < 0 || scene >= ANALYSIS_SCENE_COUNT) return NULL;
    return scene_names[scene];
}

static bool field_equals(Field field, const char *literal)
{
    size_t length = strlen(literal);
    return field.length == length && memcmp(field.data, literal, length) == 0;
}

static size_t split_fields(const char *line, size_t length, Field *fields, size_t capacity)
{
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || line[i] == '\t') {
            if (count >= capacity) return SIZE_MAX;
            fields[count++] = (Field){line + start, i - start};
            start = i + 1;
        }
    }
    return count;
}

static bool parse_u64(Field field, uint64_t *output)
{
    if (field.length == 0) return false;
    if (field.length > 1 && field.data[0] == '0') return false;
    uint64_t value = 0;
    for (size_t i = 0; i < field.length; ++i) {
        unsigned char byte = (unsigned char)field.data[i];
        if (byte < '0' || byte > '9') return false;
        unsigned digit = byte - '0';
        if (value > (UINT64_MAX - digit)/10) return false;
        value = value*10 + digit;
    }
    *output = value;
    return true;
}

static bool parse_i32(Field field, int32_t *output)
{
    if (field.length == 0) return false;
    bool negative = field.data[0] == '-';
    size_t offset = negative ? 1 : 0;
    if (offset == field.length) return false;
    uint64_t magnitude = 0;
    if (!parse_u64((Field){field.data + offset, field.length - offset}, &magnitude)) return false;
    if (negative && magnitude == 0) return false;
    if ((!negative && magnitude > INT32_MAX) || (negative && magnitude > (uint64_t)INT32_MAX + 1)) {
        return false;
    }
    if (negative && magnitude == (uint64_t)INT32_MAX + 1) *output = INT32_MIN;
    else *output = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    return true;
}

static bool valid_sha256(Field field)
{
    if (field.length != 64) return false;
    for (size_t i = 0; i < field.length; ++i) {
        char byte = field.data[i];
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) return false;
    }
    return true;
}

static int base64_value(char byte)
{
    if (byte >= 'A' && byte <= 'Z') return byte - 'A';
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 26;
    if (byte >= '0' && byte <= '9') return byte - '0' + 52;
    if (byte == '+') return 62;
    if (byte == '/') return 63;
    return -1;
}

static Analysis_Bridge_Result decode_base64(Field field, char *output, size_t capacity)
{
    if (field.length == 0) { output[0] = '\0'; return ANALYSIS_BRIDGE_OK; }
    if (field.length%4 != 0) return ANALYSIS_BRIDGE_ERROR_BASE64;
    size_t target = 0;
    for (size_t i = 0; i < field.length; i += 4) {
        bool final = i + 4 == field.length;
        int a = base64_value(field.data[i]);
        int b = base64_value(field.data[i + 1]);
        int c = field.data[i + 2] == '=' ? -2 : base64_value(field.data[i + 2]);
        int d = field.data[i + 3] == '=' ? -2 : base64_value(field.data[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) || ((c == -2 || d == -2) && !final) ||
            (c == -2 && (b & 15) != 0) || (d == -2 && c >= 0 && (c & 3) != 0)) {
            return ANALYSIS_BRIDGE_ERROR_BASE64;
        }
        uint32_t bits = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                        ((uint32_t)(c < 0 ? 0 : c) << 6) |
                        (uint32_t)(d < 0 ? 0 : d);
        size_t produced = 1 + (c >= 0 ? 1u : 0u) + (d >= 0 ? 1u : 0u);
        if (target + produced >= capacity) return ANALYSIS_BRIDGE_ERROR_DECODED_SIZE;
        output[target++] = (char)(bits >> 16);
        if (c >= 0) output[target++] = (char)(bits >> 8);
        if (d >= 0) output[target++] = (char)bits;
    }
    if (memchr(output, '\0', target) != NULL) return ANALYSIS_BRIDGE_ERROR_UTF8;
    output[target] = '\0';
    return ANALYSIS_BRIDGE_OK;
}

static bool valid_utf8(const char *text)
{
    const unsigned char *bytes = (const unsigned char *)text;
    size_t length = strlen(text);
    for (size_t i = 0; i < length;) {
        unsigned char first = bytes[i];
        size_t continuation = 0;
        uint32_t codepoint = 0;
        if (first < 0x80) { i += 1; continue; }
        if (first >= 0xC2 && first <= 0xDF) { continuation = 1; codepoint = first & 31u; }
        else if (first >= 0xE0 && first <= 0xEF) { continuation = 2; codepoint = first & 15u; }
        else if (first >= 0xF0 && first <= 0xF4) { continuation = 3; codepoint = first & 7u; }
        else return false;
        if (i + continuation >= length) return false;
        for (size_t j = 1; j <= continuation; ++j) {
            unsigned char byte = bytes[i + j];
            if ((byte & 0xC0u) != 0x80u) return false;
            codepoint = (codepoint << 6) | (byte & 63u);
        }
        if ((continuation == 2 && codepoint < 0x800u) ||
            (continuation == 3 && codepoint < 0x10000u) ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu) || codepoint > 0x10FFFFu) return false;
        i += continuation + 1;
    }
    return true;
}

static bool id_add(uint64_t *ids, size_t *count, uint64_t id)
{
    if (id == 0 || *count >= ANALYSIS_BRIDGE_MAX_IDS) return false;
    for (size_t i = 0; i < *count; ++i) if (ids[i] == id) return false;
    ids[(*count)++] = id;
    return true;
}

static bool scene_parse(Field field, Analysis_Scene *scene)
{
    for (int i = 0; i < ANALYSIS_SCENE_COUNT; ++i) {
        if (field_equals(field, scene_names[i])) { *scene = (Analysis_Scene)i; return true; }
    }
    return false;
}

static Analysis_Bridge_Result timed_record(Field *fields, uint64_t duration_ms,
                                           uint64_t *id, uint64_t *start, uint64_t *end)
{
    if (!parse_u64(fields[1], id) || !parse_u64(fields[2], start) ||
        !parse_u64(fields[3], end) || *id == 0 || *start >= *end || *end > duration_ms) {
        return ANALYSIS_BRIDGE_ERROR_RANGE;
    }
    return ANALYSIS_BRIDGE_OK;
}

Analysis_Bridge_Result analysis_bridge_parse(Analysis_Bridge *destination,
                                             const char *input, size_t input_size,
                                             const char *expected_audio_sha256,
                                             uint64_t expected_duration_ms)
{
    if (destination == NULL || input == NULL) return ANALYSIS_BRIDGE_ERROR_NULL;
    if (input_size == 0 || input_size > ANALYSIS_BRIDGE_INPUT_MAX_BYTES ||
        input[input_size - 1] != '\n' || memchr(input, '\0', input_size) != NULL ||
        memchr(input, '\r', input_size) != NULL) return ANALYSIS_BRIDGE_ERROR_INPUT_SIZE;

    Analysis_Bridge *parsed = malloc(sizeof(*parsed));
    uint64_t *ids = malloc(ANALYSIS_BRIDGE_MAX_IDS*sizeof(*ids));
    if (parsed == NULL || ids == NULL) {
        free(parsed); free(ids);
        return ANALYSIS_BRIDGE_ERROR_ALLOCATION;
    }
    analysis_bridge_init(parsed);
    size_t id_count = 0;
    size_t offset = 0;
    size_t line_number = 0;
    int record_rank = 0;
    uint64_t previous_lyric_start = 0, previous_section_start = 0, previous_semantic_start = 0;
    bool have_lyric_time = false, have_section_time = false, have_semantic_time = false;
    Analysis_Bridge_Result result = ANALYSIS_BRIDGE_OK;

    while (offset < input_size && result == ANALYSIS_BRIDGE_OK) {
        const char *line = input + offset;
        const char *newline = memchr(line, '\n', input_size - offset);
        size_t length = (size_t)(newline - line);
        offset += length + 1;
        Field fields[10];
        size_t count = split_fields(line, length, fields, 10);
        if (count == SIZE_MAX || count == 0) { result = ANALYSIS_BRIDGE_ERROR_RECORD; break; }

        if (line_number == 0) {
            if (count != 2 || !field_equals(fields[0], "MUSIALIZER_BRIDGE") ||
                !field_equals(fields[1], "1")) result = ANALYSIS_BRIDGE_ERROR_HEADER;
            line_number += 1;
            continue;
        }
        if (line_number == 1) {
            uint64_t duration_ms = 0;
            if (count != 3 || !field_equals(fields[0], "AUDIO") || !valid_sha256(fields[1]) ||
                !parse_u64(fields[2], &duration_ms) || duration_ms == 0 ||
                duration_ms > ANALYSIS_BRIDGE_MAX_EXACT_MS) {
                result = ANALYSIS_BRIDGE_ERROR_AUDIO;
            } else {
                memcpy(parsed->audio_sha256, fields[1].data, 64);
                parsed->audio_sha256[64] = '\0';
                parsed->duration_ms = duration_ms;
                if ((expected_audio_sha256 != NULL &&
                     strcmp(expected_audio_sha256, parsed->audio_sha256) != 0) ||
                    (expected_duration_ms != 0 && expected_duration_ms != duration_ms)) {
                    result = ANALYSIS_BRIDGE_ERROR_AUDIO_MISMATCH;
                } else if (lyrics_document_init(&parsed->lyrics, (double)duration_ms/1000.0) != LYRICS_OK) {
                    result = ANALYSIS_BRIDGE_ERROR_AUDIO;
                }
            }
            line_number += 1;
            continue;
        }

        uint64_t id = 0, start = 0, end = 0;
        if (field_equals(fields[0], "LYRIC")) {
            if (record_rank > 1 || count != 7) { result = ANALYSIS_BRIDGE_ERROR_ORDER; break; }
            record_rank = 1;
            result = timed_record(fields, parsed->duration_ms, &id, &start, &end);
            int32_t confidence = 0;
            if (result != ANALYSIS_BRIDGE_OK || !parse_i32(fields[4], &confidence) ||
                confidence < -1 || confidence > 1000 ||
                !(field_equals(fields[5], "none") || field_equals(fields[5], "uncertain"))) {
                result = ANALYSIS_BRIDGE_ERROR_RANGE; break;
            }
            if (have_lyric_time && start < previous_lyric_start) { result = ANALYSIS_BRIDGE_ERROR_ORDER; break; }
            if (!id_add(ids, &id_count, id)) { result = ANALYSIS_BRIDGE_ERROR_DUPLICATE_ID; break; }
            Lyric_Cue cue = {.id = id, .start_seconds = (double)start/1000.0,
                             .end_seconds = (double)end/1000.0};
            result = decode_base64(fields[6], cue.text, sizeof(cue.text));
            if (result != ANALYSIS_BRIDGE_OK) break;
            if (!valid_utf8(cue.text)) { result = ANALYSIS_BRIDGE_ERROR_UTF8; break; }
            Lyrics_Result lyric_result = lyrics_insert(&parsed->lyrics, &cue, NULL);
            if (lyric_result == LYRICS_ERROR_CAPACITY) result = ANALYSIS_BRIDGE_ERROR_CAPACITY;
            else if (lyric_result != LYRICS_OK) result = ANALYSIS_BRIDGE_ERROR_RECORD;
            if (result != ANALYSIS_BRIDGE_OK) break;
            parsed->lyric_metadata[parsed->lyric_metadata_count++] = (Analysis_Lyric_Metadata){
                .id = id, .confidence_milli = (int16_t)confidence,
                .uncertain = field_equals(fields[5], "uncertain")};
            parsed->lyrics_present = true;
            previous_lyric_start = start; have_lyric_time = true;
        } else if (field_equals(fields[0], "SECTION")) {
            if (record_rank > 2 || count != 7) { result = ANALYSIS_BRIDGE_ERROR_ORDER; break; }
            record_rank = 2;
            if (parsed->section_count >= ANALYSIS_BRIDGE_SECTION_CAPACITY) {
                result = ANALYSIS_BRIDGE_ERROR_CAPACITY; break;
            }
            result = timed_record(fields, parsed->duration_ms, &id, &start, &end);
            uint64_t strength = 0;
            Analysis_Scene scene;
            if (result != ANALYSIS_BRIDGE_OK) break;
            if (!scene_parse(fields[4], &scene)) { result = ANALYSIS_BRIDGE_ERROR_SCENE; break; }
            if (!parse_u64(fields[5], &strength) || strength > 1000) {
                result = ANALYSIS_BRIDGE_ERROR_RANGE; break;
            }
            if ((have_section_time && start < previous_section_start) ||
                (!have_section_time && start != 0) ||
                (have_section_time && start != parsed->sections[parsed->section_count - 1].end_ms)) {
                result = ANALYSIS_BRIDGE_ERROR_COVERAGE; break;
            }
            if (!id_add(ids, &id_count, id)) { result = ANALYSIS_BRIDGE_ERROR_DUPLICATE_ID; break; }
            Analysis_Section *section = &parsed->sections[parsed->section_count];
            *section = (Analysis_Section){.id = id, .start_ms = start, .end_ms = end,
                .recommended_scene = scene, .transition_strength_milli = (uint16_t)strength};
            result = decode_base64(fields[6], section->reasons_json, sizeof(section->reasons_json));
            if (result != ANALYSIS_BRIDGE_OK) break;
            if (!valid_utf8(section->reasons_json)) { result = ANALYSIS_BRIDGE_ERROR_UTF8; break; }
            size_t reason_length = strlen(section->reasons_json);
            if (reason_length < 2 || section->reasons_json[0] != '[' ||
                section->reasons_json[reason_length - 1] != ']') {
                result = ANALYSIS_BRIDGE_ERROR_RECORD; break;
            }
            parsed->section_count += 1; parsed->sections_present = true;
            previous_section_start = start; have_section_time = true;
        } else if (field_equals(fields[0], "SEMANTIC")) {
            if (record_rank > 3 || count != 9 || parsed->semantic_notes_present) {
                result = ANALYSIS_BRIDGE_ERROR_ORDER; break;
            }
            record_rank = 3;
            if (parsed->semantic_cue_count >= ANALYSIS_BRIDGE_SEMANTIC_CAPACITY) {
                result = ANALYSIS_BRIDGE_ERROR_CAPACITY; break;
            }
            result = timed_record(fields, parsed->duration_ms, &id, &start, &end);
            int32_t energy, tension, valence, confidence;
            if (result != ANALYSIS_BRIDGE_OK || !parse_i32(fields[4], &energy) ||
                !parse_i32(fields[5], &tension) || !parse_i32(fields[6], &valence) ||
                !parse_i32(fields[7], &confidence) || energy < 0 || energy > 1000 ||
                tension < 0 || tension > 1000 || valence < -1000 || valence > 1000 ||
                confidence < 0 || confidence > 1000) { result = ANALYSIS_BRIDGE_ERROR_RANGE; break; }
            if ((have_semantic_time && start < previous_semantic_start) ||
                (!have_semantic_time && start != 0) ||
                (have_semantic_time && start != parsed->semantic_cues[parsed->semantic_cue_count - 1].end_ms)) {
                result = ANALYSIS_BRIDGE_ERROR_COVERAGE; break;
            }
            if (!id_add(ids, &id_count, id)) { result = ANALYSIS_BRIDGE_ERROR_DUPLICATE_ID; break; }
            Analysis_Semantic_Cue *cue = &parsed->semantic_cues[parsed->semantic_cue_count];
            *cue = (Analysis_Semantic_Cue){.id = id, .start_ms = start, .end_ms = end,
                .energy_milli = (uint16_t)energy, .tension_milli = (uint16_t)tension,
                .valence_milli = (int16_t)valence, .confidence_milli = (uint16_t)confidence};
            result = decode_base64(fields[8], cue->summary, sizeof(cue->summary));
            if (result != ANALYSIS_BRIDGE_OK) break;
            if (!valid_utf8(cue->summary)) { result = ANALYSIS_BRIDGE_ERROR_UTF8; break; }
            parsed->semantic_cue_count += 1; parsed->semantic_cues_present = true;
            previous_semantic_start = start; have_semantic_time = true;
        } else if (field_equals(fields[0], "SEMANTIC_NOTE")) {
            if (record_rank > 3 || count != 3 || parsed->semantic_cues_present) {
                result = ANALYSIS_BRIDGE_ERROR_ORDER; break;
            }
            record_rank = 3;
            if (parsed->semantic_note_count >= ANALYSIS_BRIDGE_NOTE_CAPACITY) {
                result = ANALYSIS_BRIDGE_ERROR_CAPACITY; break;
            }
            if (!parse_u64(fields[1], &id) || id == 0) { result = ANALYSIS_BRIDGE_ERROR_RANGE; break; }
            if (!id_add(ids, &id_count, id)) { result = ANALYSIS_BRIDGE_ERROR_DUPLICATE_ID; break; }
            Analysis_Semantic_Note *note = &parsed->semantic_notes[parsed->semantic_note_count];
            note->id = id;
            result = decode_base64(fields[2], note->text, sizeof(note->text));
            if (result != ANALYSIS_BRIDGE_OK) break;
            if (!valid_utf8(note->text) || note->text[0] == '\0') {
                result = ANALYSIS_BRIDGE_ERROR_UTF8; break;
            }
            parsed->semantic_note_count += 1; parsed->semantic_notes_present = true;
        } else {
            result = ANALYSIS_BRIDGE_ERROR_RECORD;
        }
        line_number += 1;
    }

    if (result == ANALYSIS_BRIDGE_OK && line_number < 2) result = ANALYSIS_BRIDGE_ERROR_HEADER;
    if (result == ANALYSIS_BRIDGE_OK && (!parsed->sections_present ||
        parsed->sections[parsed->section_count - 1].end_ms != parsed->duration_ms)) {
        result = ANALYSIS_BRIDGE_ERROR_COVERAGE;
    }
    if (result == ANALYSIS_BRIDGE_OK && parsed->semantic_cues_present &&
        parsed->semantic_cues[parsed->semantic_cue_count - 1].end_ms != parsed->duration_ms) {
        result = ANALYSIS_BRIDGE_ERROR_COVERAGE;
    }
    if (result == ANALYSIS_BRIDGE_OK) memcpy(destination, parsed, sizeof(*destination));
    free(ids);
    free(parsed);
    return result;
}

const char *analysis_bridge_result_string(Analysis_Bridge_Result result)
{
    static const char *const names[] = {
        "ok", "null argument", "invalid bridge input size", "invalid bridge header",
        "invalid audio record", "bridge audio does not match", "invalid bridge record",
        "records are out of order", "duplicate stable id", "value or timing out of range",
        "unknown scene", "invalid base64", "invalid UTF-8", "decoded field too large",
        "bridge capacity exceeded", "timed lane does not cover audio", "allocation failed"
    };
    if (result < 0 || (size_t)result >= sizeof(names)/sizeof(names[0])) return "unknown bridge result";
    return names[result];
}
