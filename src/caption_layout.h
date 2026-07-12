#ifndef MUSIALIZER_CAPTION_LAYOUT_H_
#define MUSIALIZER_CAPTION_LAYOUT_H_

#include <stdbool.h>
#include <stddef.h>

// Lyric cues are deliberately bounded before they reach the renderer. Keep the
// layout model independent of Raylib so preview and export use the same rules.
#define CAPTION_LAYOUT_SOURCE_CAPACITY 512u
#define CAPTION_LAYOUT_MAX_LINES 3u
#define CAPTION_LAYOUT_LINE_CAPACITY (CAPTION_LAYOUT_SOURCE_CAPACITY + 4u)

// This is a hard ceiling on the glyph request passed to Raylib's LoadFontEx.
// The curated set is intentionally much smaller than "load all of Unicode".
#define CAPTION_FONT_CODEPOINT_LIMIT 2048u

typedef float (*Caption_Measure_Text)(const char *utf8, void *user_data);

typedef struct Caption_Layout_Line {
    char text[CAPTION_LAYOUT_LINE_CAPACITY];
    size_t byte_length;
    float width;
    // Offset from the left edge of a max_width caption box.
    float centered_offset;
} Caption_Layout_Line;

typedef struct Caption_Layout {
    Caption_Layout_Line lines[CAPTION_LAYOUT_MAX_LINES];
    size_t line_count;
    bool ellipsized;
} Caption_Layout;

typedef enum Caption_Layout_Result {
    CAPTION_LAYOUT_OK = 0,
    CAPTION_LAYOUT_ERROR_NULL,
    CAPTION_LAYOUT_ERROR_SOURCE_TOO_LONG,
    CAPTION_LAYOUT_ERROR_INVALID_UTF8,
    CAPTION_LAYOUT_ERROR_INVALID_TEXT,
    CAPTION_LAYOUT_ERROR_EMPTY,
    CAPTION_LAYOUT_ERROR_WIDTH,
    CAPTION_LAYOUT_ERROR_MEASUREMENT,
    CAPTION_LAYOUT_ERROR_TOO_NARROW
} Caption_Layout_Result;

// Trims and collapses Unicode whitespace, greedily wraps at word boundaries,
// and only splits a word at a UTF-8 codepoint boundary when it cannot fit on an
// empty line. If content exceeds three lines, the third line always ends in a
// visible U+2026 ellipsis and ellipsized is true. Output is replaced atomically.
Caption_Layout_Result caption_layout_utf8(const char *text,
                                          float max_width,
                                          Caption_Measure_Text measure,
                                          void *user_data,
                                          Caption_Layout *output);

typedef enum Caption_Font_Result {
    CAPTION_FONT_OK = 0,
    CAPTION_FONT_ERROR_NULL,
    CAPTION_FONT_ERROR_BUFFER_TOO_SMALL,
    CAPTION_FONT_ERROR_INTERNAL_RANGE
} Caption_Font_Result;

// Curated, sorted codepoints for Latin (including composed/decomposed accents),
// Greek, Cyrillic, common punctuation/currency, and a small caption/UI symbol
// set. Call count first, then provide an int buffer of exactly that bounded size
// to pass to Raylib's LoadFontEx. A short buffer is never partially written.
size_t caption_font_codepoint_count(void);
Caption_Font_Result caption_font_codepoints(int *output,
                                            size_t capacity,
                                            size_t *written);

#endif // MUSIALIZER_CAPTION_LAYOUT_H_
