#ifndef MUSIALIZER_FONT_CATALOGUE_H_
#define MUSIALIZER_FONT_CATALOGUE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// What tools/google_fonts.py wrote, read as bounded TSV rather than JSON: the
// family list, and the one-row manifest describing a completed download. The
// renderer has no JSON parser and does not want one for tables this shape; the
// analysis bridge established the same trade already.
//
// Nothing here touches raylib, so the parser and the search are reachable from
// the headless test binary, which is the point: this reads a file written by
// another process and must not be able to be talked into anything.

#define FONT_CATALOGUE_SCHEMA_HEADER "musializer.font-catalogue/v1"
// Google Fonts offers about 1800 Latin families today. The cap is generous
// enough to absorb growth and small enough that a catalogue is a quarter of a
// megabyte rather than an unbounded read.
#define FONT_CATALOGUE_MAX_FAMILIES 4096u
// The longest family in the catalogue is 32 characters. This is deliberately
// far above it so a new arrival is truncated-and-rejected, never a buffer
// overrun.
#define FONT_CATALOGUE_FAMILY_CAPACITY 64u
#define FONT_CATALOGUE_MAX_BYTES (4u*1024u*1024u)

typedef enum Font_Category {
    FONT_CATEGORY_UNKNOWN = 0,
    FONT_CATEGORY_SANS_SERIF,
    FONT_CATEGORY_SERIF,
    FONT_CATEGORY_DISPLAY,
    FONT_CATEGORY_HANDWRITING,
    FONT_CATEGORY_MONOSPACE,
    FONT_CATEGORY_COUNT
} Font_Category;

// Only the scripts the caption glyph set actually covers. A family may well
// carry Devanagari; this build's atlas would not rasterize it, so recording it
// would be a promise the renderer cannot keep.
#define FONT_SCRIPT_LATIN      (1u << 0)
#define FONT_SCRIPT_LATIN_EXT  (1u << 1)
#define FONT_SCRIPT_GREEK      (1u << 2)
#define FONT_SCRIPT_CYRILLIC   (1u << 3)
#define FONT_SCRIPT_VIETNAMESE (1u << 4)

typedef struct Font_Catalogue_Entry {
    char family[FONT_CATALOGUE_FAMILY_CAPACITY];
    Font_Category category;
    uint32_t scripts;
} Font_Catalogue_Entry;

typedef struct Font_Catalogue {
    size_t count;
    Font_Catalogue_Entry entries[FONT_CATALOGUE_MAX_FAMILIES];
} Font_Catalogue;

typedef enum Font_Catalogue_Result {
    FONT_CATALOGUE_OK = 0,
    FONT_CATALOGUE_ERROR_ARGUMENT,
    FONT_CATALOGUE_ERROR_TOO_LARGE,
    FONT_CATALOGUE_ERROR_HEADER,
    FONT_CATALOGUE_ERROR_ROW,
    FONT_CATALOGUE_ERROR_FAMILY,
    FONT_CATALOGUE_ERROR_CAPACITY,
    FONT_CATALOGUE_ERROR_EMPTY,
} Font_Catalogue_Result;

const char *font_catalogue_result_string(Font_Catalogue_Result result);
const char *font_category_name(Font_Category category);
// Human-readable script coverage, e.g. "Latin, Greek". Always NUL-terminated.
void font_scripts_describe(uint32_t scripts, char *output, size_t capacity);

void font_catalogue_clear(Font_Catalogue *catalogue);

// Replaces the destination only on success: a catalogue that fails to parse
// leaves whatever was already loaded intact, so a bad refresh does not empty
// a working picker.
Font_Catalogue_Result font_catalogue_parse(Font_Catalogue *destination,
                                           const char *text, size_t size);

// Whether one entry matches a search query, so a caller drawing only the
// visible window of a long list can skip rows without collecting every index
// that matched first.
bool font_catalogue_entry_matches(const Font_Catalogue_Entry *entry,
                                  const char *query);

// Case-insensitive substring match on the family name, preserving the
// catalogue's own order (most popular first). Writes at most `capacity`
// indices and returns how many matched, which may exceed what was written.
size_t font_catalogue_filter(const Font_Catalogue *catalogue, const char *query,
                             uint32_t required_scripts, size_t *indices,
                             size_t capacity, size_t *written);

// Exact family lookup, which is what turns a saved family name back into a
// catalogue row. Returns false when the family is not offered.
bool font_catalogue_find(const Font_Catalogue *catalogue, const char *family,
                         size_t *index);

#define FONT_IMPORT_MANIFEST_HEADER "musializer.font-import/v1"
#define FONT_IMPORT_MANIFEST_INDEX_NAME "import.tsv"
#define FONT_IMPORT_PATH_CAPACITY 1024u
#define FONT_IMPORT_SHA256_CAPACITY 65u
#define FONT_IMPORT_LICENCE_NAME_CAPACITY 64u

// One completed download, as the helper described it. Every path and digest
// here is a claim: the caller re-hashes both files before either is used.
typedef struct Font_Import_Manifest {
    char family[FONT_CATALOGUE_FAMILY_CAPACITY];
    char font_path[FONT_IMPORT_PATH_CAPACITY];
    char font_sha256[FONT_IMPORT_SHA256_CAPACITY];
    char licence_path[FONT_IMPORT_PATH_CAPACITY];
    char licence_sha256[FONT_IMPORT_SHA256_CAPACITY];
    char licence_name[FONT_IMPORT_LICENCE_NAME_CAPACITY];
} Font_Import_Manifest;

// Reads the single manifest row. Rejects a missing header, the wrong column
// count, an over-long field, a malformed digest, and an empty family or path,
// leaving the destination untouched on every failure.
Font_Catalogue_Result font_import_manifest_parse(Font_Import_Manifest *destination,
                                                 const char *text, size_t size);

#endif // MUSIALIZER_FONT_CATALOGUE_H_
