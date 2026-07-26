#include "font_catalogue.h"

#include <stdio.h>
#include <string.h>

const char *font_catalogue_result_string(Font_Catalogue_Result result)
{
    static const char *names[] = {
        "ok",
        "invalid catalogue argument",
        "catalogue is larger than this build will read",
        "catalogue header is missing or from another version",
        "catalogue row is malformed",
        "catalogue family name is empty, too long, or contains control characters",
        "catalogue holds more families than this build can list",
        "catalogue contains no families",
    };
    return result >= 0 && (size_t)result < sizeof(names)/sizeof(names[0]) ?
           names[result] : "unknown catalogue result";
}

const char *font_category_name(Font_Category category)
{
    switch (category) {
    case FONT_CATEGORY_SANS_SERIF:  return "Sans Serif";
    case FONT_CATEGORY_SERIF:       return "Serif";
    case FONT_CATEGORY_DISPLAY:     return "Display";
    case FONT_CATEGORY_HANDWRITING: return "Handwriting";
    case FONT_CATEGORY_MONOSPACE:   return "Monospace";
    case FONT_CATEGORY_UNKNOWN:
    case FONT_CATEGORY_COUNT:
        break;
    }
    return "Other";
}

void font_scripts_describe(uint32_t scripts, char *output, size_t capacity)
{
    if (output == NULL || capacity == 0) return;
    static const struct { uint32_t bit; const char *name; } names[] = {
        { FONT_SCRIPT_LATIN,      "Latin" },
        { FONT_SCRIPT_LATIN_EXT,  "Latin Ext" },
        { FONT_SCRIPT_GREEK,      "Greek" },
        { FONT_SCRIPT_CYRILLIC,   "Cyrillic" },
        { FONT_SCRIPT_VIETNAMESE, "Vietnamese" },
    };
    output[0] = '\0';
    size_t length = 0;
    bool any = false;
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        if ((scripts & names[i].bit) == 0) continue;
        any = true;
        const char *separator = length > 0 ? ", " : "";
        int written = snprintf(output + length, capacity - length, "%s%s",
                               separator, names[i].name);
        if (written <= 0 || (size_t)written >= capacity - length) {
            // Out of room. snprintf has already written as much of this name
            // as fit, so the terminator goes back where the last complete name
            // ended: a list that stops early reads as a list, "Latin, Cyril"
            // reads as a script nobody has heard of.
            output[length] = '\0';
            return;
        }
        length += (size_t)written;
    }
    if (!any) snprintf(output, capacity, "no known script");
}

void font_catalogue_clear(Font_Catalogue *catalogue)
{
    if (catalogue == NULL) return;
    catalogue->count = 0;
}

static Font_Category category_from_name(const char *name, size_t length)
{
    static const struct { const char *name; Font_Category category; } table[] = {
        { "Sans Serif",  FONT_CATEGORY_SANS_SERIF },
        { "Serif",       FONT_CATEGORY_SERIF },
        { "Display",     FONT_CATEGORY_DISPLAY },
        { "Handwriting", FONT_CATEGORY_HANDWRITING },
        { "Monospace",   FONT_CATEGORY_MONOSPACE },
    };
    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); ++i) {
        if (strlen(table[i].name) == length &&
            memcmp(table[i].name, name, length) == 0) {
            return table[i].category;
        }
    }
    // A category this build does not know is not an error: the catalogue is
    // allowed to grow one, and a family that only sorts oddly is still usable.
    return FONT_CATEGORY_UNKNOWN;
}

static uint32_t scripts_from_list(const char *list, size_t length)
{
    static const struct { const char *name; uint32_t bit; } table[] = {
        { "latin",      FONT_SCRIPT_LATIN },
        { "latin-ext",  FONT_SCRIPT_LATIN_EXT },
        { "greek",      FONT_SCRIPT_GREEK },
        { "cyrillic",   FONT_SCRIPT_CYRILLIC },
        { "vietnamese", FONT_SCRIPT_VIETNAMESE },
    };
    uint32_t scripts = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i != length && list[i] != ',') continue;
        size_t item_length = i - start;
        for (size_t j = 0; j < sizeof(table)/sizeof(table[0]); ++j) {
            if (strlen(table[j].name) == item_length &&
                memcmp(table[j].name, list + start, item_length) == 0) {
                scripts |= table[j].bit;
                break;
            }
        }
        start = i + 1;
    }
    return scripts;
}

// A family name is a label drawn into the interface and a token handed back to
// the helper as a command-line argument. Control characters in either place
// are a problem, so they are rejected on the way in rather than escaped later.
static bool family_is_printable(const char *family, size_t length)
{
    if (length == 0 || length >= FONT_CATALOGUE_FAMILY_CAPACITY) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char character = (unsigned char)family[i];
        if (character < 0x20 || character == 0x7f) return false;
    }
    return true;
}

static bool split_row(const char *row, size_t length, const char **fields,
                      size_t *lengths, size_t expected)
{
    size_t field = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i != length && row[i] != '\t') continue;
        if (field >= expected) return false;
        fields[field] = row + start;
        lengths[field] = i - start;
        ++field;
        start = i + 1;
    }
    return field == expected;
}

Font_Catalogue_Result font_catalogue_parse(Font_Catalogue *destination,
                                           const char *text, size_t size)
{
    if (destination == NULL || text == NULL) return FONT_CATALOGUE_ERROR_ARGUMENT;
    if (size > FONT_CATALOGUE_MAX_BYTES) return FONT_CATALOGUE_ERROR_TOO_LARGE;

    const char *cursor = text;
    const char *end = text + size;

    const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
    if (newline == NULL) return FONT_CATALOGUE_ERROR_HEADER;
    size_t header_length = (size_t)(newline - cursor);
    const char *header_fields[2];
    size_t header_lengths[2];
    if (!split_row(cursor, header_length, header_fields, header_lengths, 2) ||
        header_lengths[0] != strlen(FONT_CATALOGUE_SCHEMA_HEADER) ||
        memcmp(header_fields[0], FONT_CATALOGUE_SCHEMA_HEADER,
               header_lengths[0]) != 0) {
        return FONT_CATALOGUE_ERROR_HEADER;
    }
    cursor = newline + 1;

    // Parsed into a scratch count first: a row that fails halfway through must
    // not leave the caller holding a partly replaced catalogue.
    size_t count = 0;
    while (cursor < end) {
        newline = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t row_length = newline == NULL ? (size_t)(end - cursor)
                                            : (size_t)(newline - cursor);
        // A trailing newline leaves an empty final row, which is not an error.
        if (row_length == 0) {
            if (newline == NULL) break;
            cursor = newline + 1;
            continue;
        }
        const char *fields[3];
        size_t lengths[3];
        if (!split_row(cursor, row_length, fields, lengths, 3)) {
            return FONT_CATALOGUE_ERROR_ROW;
        }
        if (!family_is_printable(fields[0], lengths[0])) {
            return FONT_CATALOGUE_ERROR_FAMILY;
        }
        if (count >= FONT_CATALOGUE_MAX_FAMILIES) {
            return FONT_CATALOGUE_ERROR_CAPACITY;
        }
        Font_Catalogue_Entry *entry = &destination->entries[count];
        memcpy(entry->family, fields[0], lengths[0]);
        entry->family[lengths[0]] = '\0';
        entry->category = category_from_name(fields[1], lengths[1]);
        entry->scripts = scripts_from_list(fields[2], lengths[2]);
        ++count;
        if (newline == NULL) break;
        cursor = newline + 1;
    }
    if (count == 0) return FONT_CATALOGUE_ERROR_EMPTY;
    destination->count = count;
    return FONT_CATALOGUE_OK;
}

static char lowercase(char character)
{
    return character >= 'A' && character <= 'Z' ?
           (char)(character - 'A' + 'a') : character;
}

// ASCII-only, which is all the family names contain: the helper validates them
// against an ASCII pattern before they ever reach the index.
static bool contains_fold(const char *haystack, const char *needle)
{
    if (needle[0] == '\0') return true;
    for (size_t i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        while (needle[j] != '\0' &&
               lowercase(haystack[i + j]) == lowercase(needle[j])) {
            ++j;
        }
        if (needle[j] == '\0') return true;
    }
    return false;
}

bool font_catalogue_entry_matches(const Font_Catalogue_Entry *entry,
                                  const char *query)
{
    if (entry == NULL) return false;
    return contains_fold(entry->family, query == NULL ? "" : query);
}

size_t font_catalogue_filter(const Font_Catalogue *catalogue, const char *query,
                             uint32_t required_scripts, size_t *indices,
                             size_t capacity, size_t *written)
{
    if (written != NULL) *written = 0;
    if (catalogue == NULL) return 0;
    size_t matched = 0;
    size_t stored = 0;
    for (size_t i = 0; i < catalogue->count; ++i) {
        const Font_Catalogue_Entry *entry = &catalogue->entries[i];
        if ((entry->scripts & required_scripts) != required_scripts) continue;
        if (!font_catalogue_entry_matches(entry, query)) continue;
        ++matched;
        if (indices != NULL && stored < capacity) indices[stored++] = i;
    }
    if (written != NULL) *written = stored;
    return matched;
}

static bool sha256_hex(const char *text, size_t length)
{
    if (length != 64) return false;
    for (size_t i = 0; i < length; ++i) {
        char character = text[i];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) return false;
    }
    return true;
}

static bool copy_field(char *destination, size_t capacity, const char *text,
                       size_t length)
{
    if (length == 0 || length >= capacity) return false;
    memcpy(destination, text, length);
    destination[length] = '\0';
    return true;
}

Font_Catalogue_Result font_import_manifest_parse(Font_Import_Manifest *destination,
                                                 const char *text, size_t size)
{
    if (destination == NULL || text == NULL) return FONT_CATALOGUE_ERROR_ARGUMENT;
    if (size > FONT_CATALOGUE_MAX_BYTES) return FONT_CATALOGUE_ERROR_TOO_LARGE;

    const char *end = text + size;
    const char *newline = memchr(text, '\n', size);
    if (newline == NULL) return FONT_CATALOGUE_ERROR_HEADER;
    size_t header_length = (size_t)(newline - text);
    const char *header_fields[2];
    size_t header_lengths[2];
    if (!split_row(text, header_length, header_fields, header_lengths, 2) ||
        header_lengths[0] != strlen(FONT_IMPORT_MANIFEST_HEADER) ||
        memcmp(header_fields[0], FONT_IMPORT_MANIFEST_HEADER,
               header_lengths[0]) != 0) {
        return FONT_CATALOGUE_ERROR_HEADER;
    }
    const char *row = newline + 1;
    if (row >= end) return FONT_CATALOGUE_ERROR_EMPTY;
    const char *row_end = memchr(row, '\n', (size_t)(end - row));
    size_t row_length = row_end == NULL ? (size_t)(end - row)
                                        : (size_t)(row_end - row);
    const char *fields[6];
    size_t lengths[6];
    if (!split_row(row, row_length, fields, lengths, 6)) {
        return FONT_CATALOGUE_ERROR_ROW;
    }
    if (!family_is_printable(fields[0], lengths[0])) {
        return FONT_CATALOGUE_ERROR_FAMILY;
    }
    // Built into a local first, so a manifest that fails on its last field
    // cannot leave the caller holding half of a previous import.
    Font_Import_Manifest parsed;
    memset(&parsed, 0, sizeof(parsed));
    if (!copy_field(parsed.family, sizeof(parsed.family), fields[0], lengths[0]) ||
        !copy_field(parsed.font_path, sizeof(parsed.font_path), fields[1], lengths[1]) ||
        !sha256_hex(fields[2], lengths[2]) ||
        !copy_field(parsed.font_sha256, sizeof(parsed.font_sha256), fields[2],
                    lengths[2]) ||
        !copy_field(parsed.licence_path, sizeof(parsed.licence_path), fields[3],
                    lengths[3]) ||
        !sha256_hex(fields[4], lengths[4]) ||
        !copy_field(parsed.licence_sha256, sizeof(parsed.licence_sha256), fields[4],
                    lengths[4]) ||
        !copy_field(parsed.licence_name, sizeof(parsed.licence_name), fields[5],
                    lengths[5])) {
        return FONT_CATALOGUE_ERROR_ROW;
    }
    *destination = parsed;
    return FONT_CATALOGUE_OK;
}

bool font_catalogue_find(const Font_Catalogue *catalogue, const char *family,
                         size_t *index)
{
    if (catalogue == NULL || family == NULL || family[0] == '\0') return false;
    for (size_t i = 0; i < catalogue->count; ++i) {
        if (strcmp(catalogue->entries[i].family, family) == 0) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}
