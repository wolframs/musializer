#ifndef MUSIALIZER_ASCII_ART_H_
#define MUSIALIZER_ASCII_ART_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AsciiRgba {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} AsciiRgba;

typedef enum AsciiEdgeOrientation {
    ASCII_EDGE_NONE,
    ASCII_EDGE_HORIZONTAL,
    ASCII_EDGE_VERTICAL,
    ASCII_EDGE_DIAGONAL_FORWARD,
    ASCII_EDGE_DIAGONAL_BACK,
} AsciiEdgeOrientation;

typedef struct AsciiCell {
    /* Unicode codepoint. The built-in ramp and edge glyphs are all ASCII. */
    uint32_t glyph;
    AsciiRgba foreground;
    float luminance;
    float edge_strength;
    AsciiEdgeOrientation edge_orientation;
} AsciiCell;

typedef struct AsciiGridLayout {
    float cell_width;
    float cell_height;
    float field_width;
    float field_height;
    float offset_x;
    float offset_y;
} AsciiGridLayout;

/*
 * Fits a source image into a bounded grid without changing its sample aspect.
 * The result never upscales beyond the source dimensions. Outputs are replaced
 * only after a complete, non-zero fit has been found.
 */
bool ascii_art_fit_grid_dimensions(size_t source_width,
                                   size_t source_height,
                                   size_t maximum_columns,
                                   size_t maximum_rows,
                                   size_t *columns,
                                   size_t *rows);

/*
 * Centers a grid inside a drawing area. cell_aspect is cell width/height: use
 * 1 for square samples, or 0.5 to display legacy half-height ASCII grids.
 * The result is untouched when any input is invalid.
 */
bool ascii_art_grid_layout(float area_width,
                           float area_height,
                           size_t columns,
                           size_t rows,
                           float cell_aspect,
                           AsciiGridLayout *layout);

/*
 * Converts tightly packed, row-major RGBA8 pixels into a row-major ASCII grid.
 *
 * Each output cell averages a non-empty rectangular source region. For that
 * reason the requested grid may not be larger than the source in either axis.
 * output_count must be at least grid_width*grid_height. No allocation occurs.
 * Returns false without modifying output when arguments or dimensions are
 * invalid.
 */
bool ascii_art_convert_rgba8(const uint8_t *pixels,
                             size_t source_width,
                             size_t source_height,
                             size_t grid_width,
                             size_t grid_height,
                             AsciiCell *output,
                             size_t output_count);

/*
 * Selects a deterministic, time-varying tone glyph for an imported cell.
 * Coherent edge glyphs and empty background cells remain stable so animation
 * does not erase the source silhouette. activity is clamped to [0, 1].
 */
uint32_t ascii_art_animated_glyph(const AsciiCell *cell,
                                  size_t row,
                                  size_t column,
                                  double time_seconds,
                                  float activity,
                                  uint64_t seed);

/* A grid is populated only when both dimensions are non-zero. */
bool ascii_art_grid_is_populated(size_t grid_width, size_t grid_height);

/*
 * Explicitly discards an imported grid. Invalid dimensions leave all inputs
 * untouched. On success the previously populated cells are zeroed and both
 * dimensions become zero.
 */
bool ascii_art_grid_clear(AsciiCell *cells,
                          size_t cell_capacity,
                          size_t *grid_width,
                          size_t *grid_height);

#endif
