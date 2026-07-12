#include "lyrics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Lyrics_Validation validation(Lyrics_Result result, size_t index, size_t related)
{
    return (Lyrics_Validation) {result, index, related};
}

static void bump_revision(Lyrics_Document *document)
{
    document->revision += 1;
    if (document->revision == 0) document->revision = 1;
}

static int cue_compare(const Lyric_Cue *left, const Lyric_Cue *right)
{
    if (left->start_seconds < right->start_seconds) return -1;
    if (left->start_seconds > right->start_seconds) return 1;
    if (left->end_seconds < right->end_seconds) return -1;
    if (left->end_seconds > right->end_seconds) return 1;
    if (left->id < right->id) return -1;
    if (left->id > right->id) return 1;
    return 0;
}

static Lyrics_Result validate_text(const char *text)
{
    if (text == NULL) return LYRICS_ERROR_NULL;
    const unsigned char *bytes = (const unsigned char *)text;
    size_t length = 0;
    while (length < LYRICS_TEXT_CAPACITY && bytes[length] != 0) length += 1;
    if (length == LYRICS_TEXT_CAPACITY) return LYRICS_ERROR_TEXT_TOO_LONG;
    if (length == 0) return LYRICS_ERROR_INVALID_CUE;

    for (size_t i = 0; i < length;) {
        unsigned char first = bytes[i];
        size_t continuation = 0;
        uint32_t codepoint = 0;
        if (first < 0x80) {
            if (first < 0x20 && first != '\t') return LYRICS_ERROR_INVALID_CUE;
            i += 1;
            continue;
        } else if (first >= 0xC2 && first <= 0xDF) {
            continuation = 1; codepoint = first & 0x1Fu;
        } else if (first >= 0xE0 && first <= 0xEF) {
            continuation = 2; codepoint = first & 0x0Fu;
        } else if (first >= 0xF0 && first <= 0xF4) {
            continuation = 3; codepoint = first & 0x07u;
        } else {
            return LYRICS_ERROR_INVALID_UTF8;
        }
        if (i + continuation >= length) return LYRICS_ERROR_INVALID_UTF8;
        for (size_t j = 1; j <= continuation; ++j) {
            unsigned char byte = bytes[i + j];
            if ((byte & 0xC0u) != 0x80u) return LYRICS_ERROR_INVALID_UTF8;
            codepoint = (codepoint << 6) | (uint32_t)(byte & 0x3Fu);
        }
        if ((continuation == 2 && codepoint < 0x800u) ||
            (continuation == 3 && codepoint < 0x10000u) ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
            codepoint > 0x10FFFFu) return LYRICS_ERROR_INVALID_UTF8;
        i += continuation + 1;
    }
    return LYRICS_OK;
}

static Lyrics_Result validate_cue(const Lyrics_Document *document, const Lyric_Cue *cue)
{
    if (document == NULL || cue == NULL) return LYRICS_ERROR_NULL;
    if (cue->id == 0 || !isfinite(cue->start_seconds) ||
        !isfinite(cue->end_seconds) || cue->start_seconds < 0.0 ||
        cue->end_seconds <= cue->start_seconds ||
        cue->end_seconds > document->duration_seconds) return LYRICS_ERROR_INVALID_CUE;
    return validate_text(cue->text);
}

static size_t find_index(const Lyrics_Document *document, uint64_t id)
{
    if (document == NULL || id == 0 || document->count > LYRICS_CUE_CAPACITY) {
        return SIZE_MAX;
    }
    for (size_t i = 0; i < document->count; ++i) {
        if (document->cues[i].id == id) return i;
    }
    return SIZE_MAX;
}

static size_t insertion_index(const Lyrics_Document *document, const Lyric_Cue *cue)
{
    size_t low = 0;
    size_t high = document->count;
    while (low < high) {
        size_t middle = low + (high - low)/2;
        if (cue_compare(&document->cues[middle], cue) < 0) low = middle + 1;
        else high = middle;
    }
    return low;
}

static void restore_order(Lyrics_Document *document, size_t index)
{
    while (index > 0 && cue_compare(&document->cues[index], &document->cues[index - 1]) < 0) {
        Lyric_Cue temporary = document->cues[index - 1];
        document->cues[index - 1] = document->cues[index];
        document->cues[index] = temporary;
        index -= 1;
    }
    while (index + 1 < document->count &&
           cue_compare(&document->cues[index], &document->cues[index + 1]) > 0) {
        Lyric_Cue temporary = document->cues[index + 1];
        document->cues[index + 1] = document->cues[index];
        document->cues[index] = temporary;
        index += 1;
    }
}

Lyrics_Result lyrics_document_init(Lyrics_Document *document, double duration_seconds)
{
    if (document == NULL) return LYRICS_ERROR_NULL;
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0) {
        return LYRICS_ERROR_DURATION;
    }
    memset(document, 0, sizeof(*document));
    document->schema_version = LYRICS_DOCUMENT_SCHEMA_VERSION;
    document->duration_seconds = duration_seconds;
    document->next_id = 1;
    document->revision = 1;
    return LYRICS_OK;
}

Lyrics_Validation lyrics_document_validate(const Lyrics_Document *document)
{
    if (document == NULL) return validation(LYRICS_ERROR_NULL, 0, 0);
    if (document->schema_version != LYRICS_DOCUMENT_SCHEMA_VERSION) {
        return validation(LYRICS_ERROR_SCHEMA, 0, 0);
    }
    if (!isfinite(document->duration_seconds) || document->duration_seconds <= 0.0) {
        return validation(LYRICS_ERROR_DURATION, 0, 0);
    }
    if (document->count > LYRICS_CUE_CAPACITY) {
        return validation(LYRICS_ERROR_CAPACITY, document->count, 0);
    }
    for (size_t i = 0; i < document->count; ++i) {
        Lyrics_Result cue_result = validate_cue(document, &document->cues[i]);
        if (cue_result != LYRICS_OK) return validation(cue_result, i, 0);
        if (i > 0 && cue_compare(&document->cues[i - 1], &document->cues[i]) >= 0) {
            return validation(LYRICS_ERROR_ORDER, i, i - 1);
        }
        for (size_t previous = 0; previous < i; ++previous) {
            if (document->cues[previous].id == document->cues[i].id) {
                return validation(LYRICS_ERROR_DUPLICATE_ID, i, previous);
            }
        }
        if (document->next_id != 0 && document->cues[i].id >= document->next_id) {
            return validation(LYRICS_ERROR_INVALID_CUE, i, 0);
        }
    }
    return validation(LYRICS_OK, 0, 0);
}

Lyrics_Result lyrics_insert(Lyrics_Document *document, const Lyric_Cue *cue,
                            uint64_t *inserted_id)
{
    if (inserted_id != NULL) *inserted_id = 0;
    if (document == NULL || cue == NULL) return LYRICS_ERROR_NULL;
    if (document->schema_version != LYRICS_DOCUMENT_SCHEMA_VERSION) return LYRICS_ERROR_SCHEMA;
    if (document->count >= LYRICS_CUE_CAPACITY) return LYRICS_ERROR_CAPACITY;

    Lyric_Cue candidate = *cue;
    if (candidate.id == 0) {
        if (document->next_id == 0) return LYRICS_ERROR_ID_EXHAUSTED;
        candidate.id = document->next_id;
    }
    Lyrics_Result valid = validate_cue(document, &candidate);
    if (valid != LYRICS_OK) return valid;
    if (find_index(document, candidate.id) != SIZE_MAX) return LYRICS_ERROR_DUPLICATE_ID;

    uint64_t next_id = document->next_id;
    if (candidate.id >= next_id && next_id != 0) {
        next_id = candidate.id == UINT64_MAX ? 0 : candidate.id + 1;
    }
    size_t index = insertion_index(document, &candidate);
    memmove(&document->cues[index + 1], &document->cues[index],
            (document->count - index)*sizeof(document->cues[0]));
    document->cues[index] = candidate;
    document->count += 1;
    document->next_id = next_id;
    bump_revision(document);
    if (inserted_id != NULL) *inserted_id = candidate.id;
    return LYRICS_OK;
}

Lyrics_Result lyrics_update(Lyrics_Document *document, uint64_t id,
                            double start_seconds, double end_seconds,
                            const char *text)
{
    if (document == NULL || text == NULL) return LYRICS_ERROR_NULL;
    size_t index = find_index(document, id);
    if (index == SIZE_MAX) return LYRICS_ERROR_NOT_FOUND;
    Lyric_Cue replacement = document->cues[index];
    replacement.start_seconds = start_seconds;
    replacement.end_seconds = end_seconds;
    Lyrics_Result text_result = validate_text(text);
    if (text_result != LYRICS_OK) return text_result;
    memcpy(replacement.text, text, strlen(text) + 1);
    Lyrics_Result valid = validate_cue(document, &replacement);
    if (valid != LYRICS_OK) return valid;
    document->cues[index] = replacement;
    restore_order(document, index);
    bump_revision(document);
    return LYRICS_OK;
}

Lyrics_Result lyrics_delete(Lyrics_Document *document, uint64_t id)
{
    if (document == NULL) return LYRICS_ERROR_NULL;
    size_t index = find_index(document, id);
    if (index == SIZE_MAX) return LYRICS_ERROR_NOT_FOUND;
    memmove(&document->cues[index], &document->cues[index + 1],
            (document->count - index - 1)*sizeof(document->cues[0]));
    document->count -= 1;
    memset(&document->cues[document->count], 0, sizeof(document->cues[0]));
    bump_revision(document);
    return LYRICS_OK;
}

Lyrics_Result lyrics_nudge(Lyrics_Document *document, uint64_t id,
                           double delta_seconds)
{
    if (document == NULL) return LYRICS_ERROR_NULL;
    if (!isfinite(delta_seconds)) return LYRICS_ERROR_INVALID_CUE;
    size_t index = find_index(document, id);
    if (index == SIZE_MAX) return LYRICS_ERROR_NOT_FOUND;
    return lyrics_update(document, id,
                         document->cues[index].start_seconds + delta_seconds,
                         document->cues[index].end_seconds + delta_seconds,
                         document->cues[index].text);
}

Lyrics_Result lyrics_split(Lyrics_Document *document, uint64_t id,
                           double split_seconds, const char *left_text,
                           const char *right_text, uint64_t *right_id)
{
    if (right_id != NULL) *right_id = 0;
    if (document == NULL || left_text == NULL || right_text == NULL) return LYRICS_ERROR_NULL;
    if (document->count >= LYRICS_CUE_CAPACITY) return LYRICS_ERROR_CAPACITY;
    if (document->next_id == 0) return LYRICS_ERROR_ID_EXHAUSTED;
    size_t index = find_index(document, id);
    if (index == SIZE_MAX) return LYRICS_ERROR_NOT_FOUND;
    Lyric_Cue original = document->cues[index];
    if (!isfinite(split_seconds) || split_seconds <= original.start_seconds ||
        split_seconds >= original.end_seconds) return LYRICS_ERROR_INVALID_CUE;
    Lyrics_Result left_valid = validate_text(left_text);
    if (left_valid != LYRICS_OK) return left_valid;
    Lyrics_Result right_valid = validate_text(right_text);
    if (right_valid != LYRICS_OK) return right_valid;

    Lyric_Cue left = original;
    left.end_seconds = split_seconds;
    memcpy(left.text, left_text, strlen(left_text) + 1);
    Lyric_Cue right = {.id = document->next_id,
                       .start_seconds = split_seconds,
                       .end_seconds = original.end_seconds};
    memcpy(right.text, right_text, strlen(right_text) + 1);
    document->cues[index] = left;
    restore_order(document, index);
    Lyrics_Result result = lyrics_insert(document, &right, right_id);
    if (result != LYRICS_OK) {
        size_t left_index = find_index(document, id);
        document->cues[left_index] = original;
        restore_order(document, left_index);
        return result;
    }
    // insert already advanced the revision; split is one logical edit.
    return LYRICS_OK;
}

Lyrics_Result lyrics_merge(Lyrics_Document *document, uint64_t first_id,
                           uint64_t second_id, const char *separator)
{
    if (document == NULL || separator == NULL) return LYRICS_ERROR_NULL;
    size_t first = find_index(document, first_id);
    size_t second = find_index(document, second_id);
    if (first == SIZE_MAX || second == SIZE_MAX) return LYRICS_ERROR_NOT_FOUND;
    if (second != first + 1) return LYRICS_ERROR_NOT_ADJACENT;
    Lyrics_Result separator_valid = validate_text(separator);
    if (separator[0] == '\0') separator_valid = LYRICS_OK;
    if (separator_valid != LYRICS_OK) return separator_valid;
    size_t first_length = strlen(document->cues[first].text);
    size_t separator_length = strlen(separator);
    size_t second_length = strlen(document->cues[second].text);
    if (first_length + separator_length + second_length >= LYRICS_TEXT_CAPACITY) {
        return LYRICS_ERROR_TEXT_TOO_LONG;
    }

    Lyric_Cue merged = document->cues[first];
    memcpy(merged.text + first_length, separator, separator_length);
    memcpy(merged.text + first_length + separator_length,
           document->cues[second].text, second_length + 1);
    if (document->cues[second].end_seconds > merged.end_seconds) {
        merged.end_seconds = document->cues[second].end_seconds;
    }
    document->cues[first] = merged;
    memmove(&document->cues[second], &document->cues[second + 1],
            (document->count - second - 1)*sizeof(document->cues[0]));
    document->count -= 1;
    memset(&document->cues[document->count], 0, sizeof(document->cues[0]));
    restore_order(document, first);
    bump_revision(document);
    return LYRICS_OK;
}

const Lyric_Cue *lyrics_find(const Lyrics_Document *document, uint64_t id)
{
    size_t index = find_index(document, id);
    return index == SIZE_MAX ? NULL : &document->cues[index];
}

Lyric_Cue *lyrics_find_mut(Lyrics_Document *document, uint64_t id)
{
    size_t index = find_index(document, id);
    return index == SIZE_MAX ? NULL : &document->cues[index];
}

Lyrics_Result lyrics_document_replace(Lyrics_Document *destination,
                                      const Lyrics_Document *source)
{
    if (destination == NULL || source == NULL) return LYRICS_ERROR_NULL;
    Lyrics_Validation valid = lyrics_document_validate(source);
    if (valid.result != LYRICS_OK) return valid.result;
    uint64_t revision = destination->revision + 1;
    if (revision == 0) revision = 1;
    if (destination != source) memcpy(destination, source, sizeof(*destination));
    destination->revision = revision;
    return LYRICS_OK;
}

static size_t decimal_digits(uint64_t value)
{
    size_t digits = 1;
    while (value >= 10) { value /= 10; digits += 1; }
    return digits;
}

static uint64_t seconds_to_milliseconds(double seconds)
{
    return (uint64_t)floor(seconds*1000.0 + 0.5);
}

static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *input, size_t length, char *output)
{
    size_t source = 0;
    size_t target = 0;
    while (source + 3 <= length) {
        uint32_t bits = ((uint32_t)input[source] << 16) |
                        ((uint32_t)input[source + 1] << 8) | input[source + 2];
        output[target++] = base64_alphabet[(bits >> 18) & 63u];
        output[target++] = base64_alphabet[(bits >> 12) & 63u];
        output[target++] = base64_alphabet[(bits >> 6) & 63u];
        output[target++] = base64_alphabet[bits & 63u];
        source += 3;
    }
    size_t remainder = length - source;
    if (remainder == 1) {
        uint32_t bits = (uint32_t)input[source] << 16;
        output[target++] = base64_alphabet[(bits >> 18) & 63u];
        output[target++] = base64_alphabet[(bits >> 12) & 63u];
        output[target++] = '=';
        output[target++] = '=';
    } else if (remainder == 2) {
        uint32_t bits = ((uint32_t)input[source] << 16) |
                        ((uint32_t)input[source + 1] << 8);
        output[target++] = base64_alphabet[(bits >> 18) & 63u];
        output[target++] = base64_alphabet[(bits >> 12) & 63u];
        output[target++] = base64_alphabet[(bits >> 6) & 63u];
        output[target++] = '=';
    }
}

Lyrics_Result lyrics_bridge_export(const Lyrics_Document *document,
                                    char *output, size_t output_capacity,
                                    size_t *required_size)
{
    if (document == NULL || required_size == NULL) return LYRICS_ERROR_NULL;
    Lyrics_Validation valid = lyrics_document_validate(document);
    if (valid.result != LYRICS_OK) return valid.result;
    if (document->duration_seconds > (double)UINT64_MAX/1000.0) {
        return LYRICS_ERROR_DURATION;
    }
    uint64_t duration_ms = seconds_to_milliseconds(document->duration_seconds);
    if (duration_ms == 0) return LYRICS_ERROR_DURATION;
    size_t needed = strlen("MUSIALIZER-LYRICS-BRIDGE\t1\t\n") + decimal_digits(duration_ms) + 1;
    for (size_t i = 0; i < document->count; ++i) {
        const Lyric_Cue *cue = &document->cues[i];
        uint64_t start_ms = seconds_to_milliseconds(cue->start_seconds);
        uint64_t end_ms = seconds_to_milliseconds(cue->end_seconds);
        if (end_ms <= start_ms || end_ms > duration_ms) return LYRICS_ERROR_INVALID_CUE;
        size_t text_length = strlen(cue->text);
        size_t encoded_length = 4*((text_length + 2)/3);
        needed += decimal_digits(cue->id) + decimal_digits(start_ms) +
                  decimal_digits(end_ms) + 4 + encoded_length;
    }
    *required_size = needed;
    if (output == NULL || output_capacity < needed) return LYRICS_ERROR_BUFFER_TOO_SMALL;

    size_t offset = (size_t)snprintf(output, output_capacity,
                                    "MUSIALIZER-LYRICS-BRIDGE\t1\t%llu\n",
                                    (unsigned long long)duration_ms);
    for (size_t i = 0; i < document->count; ++i) {
        const Lyric_Cue *cue = &document->cues[i];
        int prefix = snprintf(output + offset, output_capacity - offset,
                              "%llu\t%llu\t%llu\t",
                              (unsigned long long)cue->id,
                              (unsigned long long)seconds_to_milliseconds(cue->start_seconds),
                              (unsigned long long)seconds_to_milliseconds(cue->end_seconds));
        offset += (size_t)prefix;
        size_t text_length = strlen(cue->text);
        size_t encoded_length = 4*((text_length + 2)/3);
        base64_encode((const unsigned char *)cue->text, text_length, output + offset);
        offset += encoded_length;
        output[offset++] = '\n';
    }
    output[offset] = '\0';
    return LYRICS_OK;
}

static bool parse_uint64_field(const char **cursor, const char *end,
                               char delimiter, uint64_t *value)
{
    if (*cursor >= end || **cursor < '0' || **cursor > '9') return false;
    uint64_t result = 0;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        unsigned digit = (unsigned)(**cursor - '0');
        if (result > (UINT64_MAX - digit)/10) return false;
        result = result*10 + digit;
        *cursor += 1;
    }
    if (*cursor >= end || **cursor != delimiter) return false;
    *cursor += 1;
    *value = result;
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

static bool base64_decode(const char *input, size_t length, char *output, size_t capacity)
{
    if (length == 0 || length%4 != 0) return false;
    size_t target = 0;
    for (size_t i = 0; i < length; i += 4) {
        bool final = i + 4 == length;
        int a = base64_value(input[i]);
        int b = base64_value(input[i + 1]);
        int c = input[i + 2] == '=' ? -2 : base64_value(input[i + 2]);
        int d = input[i + 3] == '=' ? -2 : base64_value(input[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) || ((c == -2 || d == -2) && !final)) return false;
        uint32_t bits = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                        ((uint32_t)(c < 0 ? 0 : c) << 6) |
                        (uint32_t)(d < 0 ? 0 : d);
        if (target + 1 >= capacity) return false;
        output[target++] = (char)(bits >> 16);
        if (c >= 0) {
            if (target + 1 >= capacity) return false;
            output[target++] = (char)(bits >> 8);
        }
        if (d >= 0) {
            if (target + 1 >= capacity) return false;
            output[target++] = (char)bits;
        }
        // Reject non-canonical nonzero padding bits.
        if ((c == -2 && (b & 15) != 0) || (d == -2 && c >= 0 && (c & 3) != 0)) return false;
    }
    output[target] = '\0';
    return memchr(output, '\0', target) == NULL;
}

Lyrics_Result lyrics_bridge_import(Lyrics_Document *document,
                                    const char *input, size_t input_size)
{
    if (document == NULL || input == NULL) return LYRICS_ERROR_NULL;
    if (input_size == 0 || input_size > LYRICS_BRIDGE_MAX_BYTES ||
        memchr(input, '\0', input_size) != NULL) return LYRICS_ERROR_BRIDGE_FORMAT;
    const char *cursor = input;
    const char *end = input + input_size;
    static const char prefix[] = "MUSIALIZER-LYRICS-BRIDGE\t";
    if ((size_t)(end - cursor) < sizeof(prefix) - 1 ||
        memcmp(cursor, prefix, sizeof(prefix) - 1) != 0) return LYRICS_ERROR_BRIDGE_FORMAT;
    cursor += sizeof(prefix) - 1;
    uint64_t version = 0;
    uint64_t duration_ms = 0;
    if (!parse_uint64_field(&cursor, end, '\t', &version) ||
        !parse_uint64_field(&cursor, end, '\n', &duration_ms) ||
        version != LYRICS_BRIDGE_VERSION || duration_ms == 0) return LYRICS_ERROR_BRIDGE_FORMAT;

    Lyrics_Document *replacement = malloc(sizeof(*replacement));
    if (replacement == NULL) return LYRICS_ERROR_ALLOCATION;
    Lyrics_Result result = lyrics_document_init(replacement, (double)duration_ms/1000.0);
    while (result == LYRICS_OK && cursor < end) {
        uint64_t id = 0, start_ms = 0, end_ms = 0;
        if (!parse_uint64_field(&cursor, end, '\t', &id) ||
            !parse_uint64_field(&cursor, end, '\t', &start_ms) ||
            !parse_uint64_field(&cursor, end, '\t', &end_ms)) {
            result = LYRICS_ERROR_BRIDGE_FORMAT;
            break;
        }
        if (id == 0) { result = LYRICS_ERROR_BRIDGE_FORMAT; break; }
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (line_end == NULL) { result = LYRICS_ERROR_BRIDGE_FORMAT; break; }
        Lyric_Cue cue = {.id = id, .start_seconds = (double)start_ms/1000.0,
                         .end_seconds = (double)end_ms/1000.0};
        if (!base64_decode(cursor, (size_t)(line_end - cursor), cue.text, sizeof(cue.text))) {
            result = LYRICS_ERROR_BRIDGE_FORMAT;
            break;
        }
        result = lyrics_insert(replacement, &cue, NULL);
        cursor = line_end + 1;
    }
    if (result == LYRICS_OK) result = lyrics_document_replace(document, replacement);
    free(replacement);
    return result;
}

const char *lyrics_result_string(Lyrics_Result result)
{
    switch (result) {
    case LYRICS_OK: return "ok";
    case LYRICS_ERROR_NULL: return "null argument";
    case LYRICS_ERROR_SCHEMA: return "unsupported lyrics schema";
    case LYRICS_ERROR_DURATION: return "invalid document duration";
    case LYRICS_ERROR_CAPACITY: return "lyrics capacity exceeded";
    case LYRICS_ERROR_INVALID_CUE: return "invalid lyric cue";
    case LYRICS_ERROR_INVALID_UTF8: return "invalid UTF-8 lyric text";
    case LYRICS_ERROR_TEXT_TOO_LONG: return "lyric text exceeds fixed capacity";
    case LYRICS_ERROR_DUPLICATE_ID: return "duplicate lyric cue id";
    case LYRICS_ERROR_ID_EXHAUSTED: return "lyric cue id space exhausted";
    case LYRICS_ERROR_NOT_FOUND: return "lyric cue not found";
    case LYRICS_ERROR_ORDER: return "lyric cues are not canonically ordered";
    case LYRICS_ERROR_NOT_ADJACENT: return "lyric cues are not adjacent";
    case LYRICS_ERROR_BUFFER_TOO_SMALL: return "lyrics bridge buffer too small";
    case LYRICS_ERROR_BRIDGE_FORMAT: return "malformed lyrics bridge";
    case LYRICS_ERROR_ALLOCATION: return "lyrics bridge allocation failed";
    }
    return "unknown lyrics result";
}
