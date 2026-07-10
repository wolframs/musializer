#include "ascii_art.h"

#include <limits.h>

/* Dark to light. Kept deliberately renderer/font agnostic. */
static const char tone_ramp[] = " .:-=+*#%@";

static uint8_t luminance_u8(const uint8_t *pixel)
{
    /* Integer Rec. 709 approximation. Coefficients sum to 256. */
    return (uint8_t)((54u*pixel[0] + 183u*pixel[1] + 19u*pixel[2] + 128u) >> 8);
}

static size_t source_offset(size_t x, size_t y, size_t width)
{
    return (y*width + x)*4u;
}

static uint8_t sample_luminance(const uint8_t *pixels,
                                size_t width,
                                size_t height,
                                size_t x,
                                size_t y,
                                int dx,
                                int dy)
{
    if (dx < 0 && x == 0) {
        x = 0;
    } else if (dx > 0 && x + 1 < width) {
        x += 1;
    } else if (dx < 0) {
        x -= 1;
    }

    if (dy < 0 && y == 0) {
        y = 0;
    } else if (dy > 0 && y + 1 < height) {
        y += 1;
    } else if (dy < 0) {
        y -= 1;
    }
    return luminance_u8(pixels + source_offset(x, y, width));
}

static void sobel_at(const uint8_t *pixels,
                     size_t width,
                     size_t height,
                     size_t x,
                     size_t y,
                     int *gradient_x,
                     int *gradient_y)
{
    int upper_left = sample_luminance(pixels, width, height, x, y, -1, -1);
    int upper = sample_luminance(pixels, width, height, x, y, 0, -1);
    int upper_right = sample_luminance(pixels, width, height, x, y, 1, -1);
    int left = sample_luminance(pixels, width, height, x, y, -1, 0);
    int right = sample_luminance(pixels, width, height, x, y, 1, 0);
    int lower_left = sample_luminance(pixels, width, height, x, y, -1, 1);
    int lower = sample_luminance(pixels, width, height, x, y, 0, 1);
    int lower_right = sample_luminance(pixels, width, height, x, y, 1, 1);

    *gradient_x = -upper_left + upper_right - 2*left + 2*right
        - lower_left + lower_right;
    *gradient_y = -upper_left - 2*upper - upper_right
        + lower_left + 2*lower + lower_right;
}

static uint64_t absolute_int(int value)
{
    return (uint64_t)(value < 0 ? -value : value);
}

static uint64_t absolute_i64(int64_t value)
{
    return (uint64_t)(value < 0 ? -value : value);
}

static AsciiEdgeOrientation classify_orientation(int64_t gradient_x,
                                                 int64_t gradient_y)
{
    uint64_t x = absolute_i64(gradient_x);
    uint64_t y = absolute_i64(gradient_y);
    if (x == 0 && y == 0) return ASCII_EDGE_NONE;
    if (x > 2u*y) return ASCII_EDGE_VERTICAL;
    if (y > 2u*x) return ASCII_EDGE_HORIZONTAL;
    return ((gradient_x < 0) == (gradient_y < 0))
        ? ASCII_EDGE_DIAGONAL_FORWARD
        : ASCII_EDGE_DIAGONAL_BACK;
}

static uint32_t glyph_for_edge(AsciiEdgeOrientation orientation)
{
    switch (orientation) {
    case ASCII_EDGE_HORIZONTAL: return '-';
    case ASCII_EDGE_VERTICAL: return '|';
    case ASCII_EDGE_DIAGONAL_FORWARD: return '/';
    case ASCII_EDGE_DIAGONAL_BACK: return '\\';
    case ASCII_EDGE_NONE: return 0;
    }
    return 0;
}

static bool dimensions_valid(size_t source_width,
                             size_t source_height,
                             size_t grid_width,
                             size_t grid_height,
                             size_t output_count)
{
    if (source_width == 0 || source_height == 0 ||
        grid_width == 0 || grid_height == 0 ||
        grid_width > source_width || grid_height > source_height) {
        return false;
    }
    if (source_width > SIZE_MAX/source_height) return false;
    size_t source_count = source_width*source_height;
    if (source_count > SIZE_MAX/4u || source_count > (uint64_t)INT64_MAX/2040u) {
        return false;
    }
    if (grid_width > SIZE_MAX/grid_height) return false;
    return output_count >= grid_width*grid_height;
}

bool ascii_art_convert_rgba8(const uint8_t *pixels,
                             size_t source_width,
                             size_t source_height,
                             size_t grid_width,
                             size_t grid_height,
                             AsciiCell *output,
                             size_t output_count)
{
    if (pixels == NULL || output == NULL ||
        !dimensions_valid(source_width, source_height,
                          grid_width, grid_height, output_count)) {
        return false;
    }

    size_t source_y = 0;
    size_t y_error = 0;
    const size_t y_quotient = source_height/grid_height;
    const size_t y_remainder = source_height%grid_height;
    for (size_t cell_y = 0; cell_y < grid_height; ++cell_y) {
        size_t next_y = source_y + y_quotient;
        y_error += y_remainder;
        if (y_error >= grid_height) {
            next_y += 1;
            y_error -= grid_height;
        }

        size_t source_x = 0;
        size_t x_error = 0;
        const size_t x_quotient = source_width/grid_width;
        const size_t x_remainder = source_width%grid_width;
        for (size_t cell_x = 0; cell_x < grid_width; ++cell_x) {
            size_t next_x = source_x + x_quotient;
            x_error += x_remainder;
            if (x_error >= grid_width) {
                next_x += 1;
                x_error -= grid_width;
            }

            uint64_t red = 0;
            uint64_t green = 0;
            uint64_t blue = 0;
            uint64_t alpha = 0;
            uint64_t luma = 0;
            uint64_t edge_sum = 0;
            int64_t gradient_x_sum = 0;
            int64_t gradient_y_sum = 0;
            size_t pixel_count = (next_x - source_x)*(next_y - source_y);

            for (size_t y = source_y; y < next_y; ++y) {
                for (size_t x = source_x; x < next_x; ++x) {
                    const uint8_t *pixel = pixels + source_offset(x, y, source_width);
                    int gradient_x = 0;
                    int gradient_y = 0;
                    red += pixel[0];
                    green += pixel[1];
                    blue += pixel[2];
                    alpha += pixel[3];
                    luma += luminance_u8(pixel);
                    sobel_at(pixels, source_width, source_height, x, y,
                             &gradient_x, &gradient_y);
                    gradient_x_sum += gradient_x;
                    gradient_y_sum += gradient_y;
                    edge_sum += absolute_int(gradient_x) + absolute_int(gradient_y);
                }
            }

            AsciiCell cell;
            cell.foreground = (AsciiRgba){
                (uint8_t)((red + pixel_count/2u)/pixel_count),
                (uint8_t)((green + pixel_count/2u)/pixel_count),
                (uint8_t)((blue + pixel_count/2u)/pixel_count),
                (uint8_t)((alpha + pixel_count/2u)/pixel_count),
            };
            cell.luminance = (float)luma/(255.0f*(float)pixel_count);
            cell.edge_strength = (float)edge_sum/(2040.0f*(float)pixel_count);
            cell.edge_orientation = classify_orientation(gradient_x_sum, gradient_y_sum);

            /* Very weak gradients are visual noise; represent them by tone. */
            if (cell.edge_strength >= 0.05f &&
                cell.edge_orientation != ASCII_EDGE_NONE) {
                cell.glyph = glyph_for_edge(cell.edge_orientation);
            } else {
                cell.edge_orientation = ASCII_EDGE_NONE;
                size_t ramp_count = sizeof(tone_ramp) - 1u;
                size_t ramp_index = (size_t)((luma*(ramp_count - 1u) +
                                              (uint64_t)pixel_count*127u)/
                                             ((uint64_t)pixel_count*255u));
                cell.glyph = (uint8_t)tone_ramp[ramp_index];
            }
            output[cell_y*grid_width + cell_x] = cell;
            source_x = next_x;
        }
        source_y = next_y;
    }
    return true;
}
