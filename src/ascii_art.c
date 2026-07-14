#include "ascii_art.h"

#include <limits.h>
#include <math.h>
#include <string.h>

/* Dark to light. More intermediate ink densities retain faces and line art. */
static const char tone_ramp[] = " .,:;i1tfLCG08#@";

static float clamp01(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static float seed_phase(uint64_t seed)
{
    const float tau = 6.28318530717958647692f;
    seed ^= seed >> 30;
    seed *= UINT64_C(0xbf58476d1ce4e5b9);
    seed ^= seed >> 27;
    seed *= UINT64_C(0x94d049bb133111eb);
    seed ^= seed >> 31;
    return (float)(seed & UINT64_C(0xffff))/65535.0f*tau;
}

static bool multiply_add_half(size_t value,
                              size_t multiplier,
                              size_t divisor,
                              size_t *result)
{
    if (divisor == 0 || multiplier == 0 || result == NULL ||
        value > SIZE_MAX/multiplier) return false;
    size_t product = value*multiplier;
    size_t half = divisor/2u;
    if (product > SIZE_MAX - half) return false;
    *result = (product + half)/divisor;
    return true;
}

bool ascii_art_fit_grid_dimensions(size_t source_width,
                                   size_t source_height,
                                   size_t maximum_columns,
                                   size_t maximum_rows,
                                   size_t *columns,
                                   size_t *rows)
{
    if (source_width == 0 || source_height == 0 ||
        maximum_columns == 0 || maximum_rows == 0 ||
        columns == NULL || rows == NULL) return false;

    size_t fitted_columns = source_width < maximum_columns
        ? source_width : maximum_columns;
    size_t fitted_rows = 0;
    if (!multiply_add_half(source_height, fitted_columns, source_width,
                           &fitted_rows)) return false;
    if (fitted_rows < 1) fitted_rows = 1;

    if (fitted_rows > maximum_rows) {
        fitted_rows = source_height < maximum_rows ? source_height : maximum_rows;
        if (!multiply_add_half(source_width, fitted_rows, source_height,
                               &fitted_columns)) return false;
        if (fitted_columns < 1) fitted_columns = 1;
    }
    if (fitted_columns > maximum_columns || fitted_rows > maximum_rows ||
        fitted_columns > source_width || fitted_rows > source_height) return false;

    *columns = fitted_columns;
    *rows = fitted_rows;
    return true;
}

bool ascii_art_grid_layout(float area_width,
                           float area_height,
                           size_t columns,
                           size_t rows,
                           float cell_aspect,
                           AsciiGridLayout *layout)
{
    if (layout == NULL || columns == 0 || rows == 0 ||
        !isfinite(area_width) || !isfinite(area_height) ||
        !isfinite(cell_aspect) || area_width <= 0.0f ||
        area_height <= 0.0f || cell_aspect <= 0.0f ||
        columns > (size_t)INT_MAX || rows > (size_t)INT_MAX) return false;

    float by_width = area_width/((float)columns*cell_aspect);
    float by_height = area_height/(float)rows;
    float cell_height = fminf(by_width, by_height);
    float cell_width = cell_height*cell_aspect;
    float field_width = cell_width*(float)columns;
    float field_height = cell_height*(float)rows;
    if (!isfinite(cell_width) || !isfinite(cell_height) ||
        !isfinite(field_width) || !isfinite(field_height) ||
        cell_width <= 0.0f || cell_height <= 0.0f) return false;

    AsciiGridLayout result = {
        .cell_width = cell_width,
        .cell_height = cell_height,
        .field_width = field_width,
        .field_height = field_height,
        .offset_x = (area_width - field_width)*0.5f,
        .offset_y = (area_height - field_height)*0.5f,
    };
    *layout = result;
    return true;
}

static uint8_t luminance_u8(const uint8_t *pixel)
{
    /* Integer Rec. 709 approximation. Coefficients sum to 256. */
    return (uint8_t)((54u*pixel[0] + 183u*pixel[1] + 19u*pixel[2] + 128u) >> 8);
}

static uint8_t visible_luminance_u8(const uint8_t *pixel)
{
    unsigned luminance = luminance_u8(pixel);
    return (uint8_t)((luminance*pixel[3] + 127u)/255u);
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
    return visible_luminance_u8(pixels + source_offset(x, y, width));
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

static uint8_t histogram_percentile(const size_t histogram[256],
                                    size_t rank)
{
    size_t cumulative = 0;
    for (size_t value = 0; value < 256; ++value) {
        if (histogram[value] > SIZE_MAX - cumulative) return (uint8_t)value;
        cumulative += histogram[value];
        if (cumulative > rank) return (uint8_t)value;
    }
    return 255;
}

static uint8_t contrast_luminance(uint8_t luminance,
                                  uint8_t black_point,
                                  uint8_t white_point)
{
    if ((unsigned)white_point <= (unsigned)black_point + 24u) return luminance;
    if (luminance <= black_point) return 0;
    if (luminance >= white_point) return 255;
    unsigned span = (unsigned)white_point - black_point;
    return (uint8_t)(((unsigned)(luminance - black_point)*255u + span/2u)/span);
}

static unsigned glyph_density_luminance(unsigned luminance)
{
    /* Character ramps put far less ink on screen than a filled image pixel.
     * A mild perceptual lift keeps shadow structure visible without moving
     * either endpoint or flattening the source contrast. */
    float normalized = (float)luminance/255.0f;
    return (unsigned)(powf(normalized, 0.72f)*255.0f + 0.5f);
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
    if (source_count > SIZE_MAX/4u ||
        source_count > UINT64_MAX/(2040u*100u) ||
        source_count > UINT64_MAX/65025u) {
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

    size_t source_count = source_width*source_height;
    size_t histogram[256] = {0};
    for (size_t index = 0; index < source_count; ++index) {
        uint8_t luminance = visible_luminance_u8(pixels + index*4u);
        histogram[luminance] += 1;
    }
    size_t trim = source_count/100u;
    uint8_t black_point = histogram_percentile(histogram, trim);
    uint8_t white_point = histogram_percentile(
        histogram, source_count - trim - 1u);

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

            uint64_t weighted_red = 0;
            uint64_t weighted_green = 0;
            uint64_t weighted_blue = 0;
            uint64_t alpha = 0;
            uint64_t luma = 0;
            uint64_t edge_sum = 0;
            uint64_t orientation_energy[ASCII_EDGE_DIAGONAL_BACK + 1] = {0};
            size_t pixel_count = (next_x - source_x)*(next_y - source_y);

            for (size_t y = source_y; y < next_y; ++y) {
                for (size_t x = source_x; x < next_x; ++x) {
                    const uint8_t *pixel = pixels + source_offset(x, y, source_width);
                    int gradient_x = 0;
                    int gradient_y = 0;
                    weighted_red += (uint64_t)pixel[0]*pixel[3];
                    weighted_green += (uint64_t)pixel[1]*pixel[3];
                    weighted_blue += (uint64_t)pixel[2]*pixel[3];
                    alpha += pixel[3];
                    luma += visible_luminance_u8(pixel);
                    sobel_at(pixels, source_width, source_height, x, y,
                             &gradient_x, &gradient_y);
                    uint64_t magnitude = absolute_int(gradient_x) +
                                         absolute_int(gradient_y);
                    AsciiEdgeOrientation orientation = classify_orientation(
                        gradient_x, gradient_y);
                    edge_sum += magnitude;
                    orientation_energy[orientation] += magnitude;
                }
            }

            uint64_t color_weight = alpha > 0 ? alpha : (uint64_t)pixel_count;
            AsciiCell cell;
            cell.foreground = (AsciiRgba){
                alpha > 0 ? (uint8_t)((weighted_red + color_weight/2u)/color_weight) : 0,
                alpha > 0 ? (uint8_t)((weighted_green + color_weight/2u)/color_weight) : 0,
                alpha > 0 ? (uint8_t)((weighted_blue + color_weight/2u)/color_weight) : 0,
                (uint8_t)((alpha + pixel_count/2u)/pixel_count),
            };
            cell.edge_strength = (float)edge_sum/(2040.0f*(float)pixel_count);
            uint64_t dominant_energy = 0;
            cell.edge_orientation = ASCII_EDGE_NONE;
            for (AsciiEdgeOrientation orientation = ASCII_EDGE_HORIZONTAL;
                 orientation <= ASCII_EDGE_DIAGONAL_BACK;
                 orientation = (AsciiEdgeOrientation)(orientation + 1)) {
                if (orientation_energy[orientation] > dominant_energy) {
                    dominant_energy = orientation_energy[orientation];
                    cell.edge_orientation = orientation;
                }
            }

            uint8_t mean_luminance = (uint8_t)((luma + pixel_count/2u)/pixel_count);
            unsigned display_luminance = glyph_density_luminance(
                contrast_luminance(mean_luminance, black_point, white_point));
            unsigned detail_boost = (unsigned)((edge_sum*88u +
                (uint64_t)2040u*pixel_count/2u)/
                ((uint64_t)2040u*pixel_count));
            display_luminance += detail_boost;
            if (display_luminance > 255u) display_luminance = 255u;
            cell.luminance = (float)display_luminance/255.0f;

            /* Only coherent gradients become line glyphs. Curves, text, and
             * crossed detail retain their tone instead of collapsing into a
             * randomly selected slash. */
            bool coherent_edge = edge_sum > 0 &&
                dominant_energy*100u >= edge_sum*60u;
            if (cell.edge_strength >= 0.035f && coherent_edge &&
                cell.edge_orientation != ASCII_EDGE_NONE) {
                cell.glyph = glyph_for_edge(cell.edge_orientation);
            } else {
                cell.edge_orientation = ASCII_EDGE_NONE;
                size_t ramp_count = sizeof(tone_ramp) - 1u;
                size_t ramp_index = (display_luminance*(ramp_count - 1u) + 127u)/255u;
                cell.glyph = (uint8_t)tone_ramp[ramp_index];
            }
            output[cell_y*grid_width + cell_x] = cell;
            source_x = next_x;
        }
        source_y = next_y;
    }
    return true;
}

uint32_t ascii_art_animated_glyph(const AsciiCell *cell,
                                  size_t row,
                                  size_t column,
                                  double time_seconds,
                                  float activity,
                                  uint64_t seed)
{
    if (cell == NULL) return 0;
    if (cell->edge_orientation != ASCII_EDGE_NONE) {
        return cell->glyph <= 0x10ffffu ? cell->glyph : 0xfffdu;
    }
    if (cell->glyph == ' ' && clamp01(cell->luminance) < 0.04f) return ' ';

    float time = isfinite(time_seconds)
        ? (float)fmod(time_seconds, 4096.0) : 0.0f;
    float response = clamp01(activity);
    float phase = seed_phase(seed) +
                  (float)column*0.17f - (float)row*0.29f;
    /* A coherent diagonal wave advances through adjacent cells. A slower
     * counter-wave stops the entire image changing glyph on the same frame. */
    float primary = sinf(time*(1.10f + response*0.72f) + phase);
    float counter = sinf(time*0.41f - (float)column*0.055f -
                         (float)row*0.083f + seed_phase(seed ^ UINT64_C(0xa0761d6478bd642f)));
    float displacement = primary*(0.82f + response*1.35f) + counter*0.34f;

    size_t ramp_count = sizeof(tone_ramp) - 1u;
    float luminance = clamp01(cell->luminance);
    int base = (int)floorf(luminance*(float)(ramp_count - 1u) + 0.5f);
    int offset = displacement >= 0.0f
        ? (int)floorf(displacement + 0.5f)
        : (int)ceilf(displacement - 0.5f);
    int animated = base + offset;
    if (animated < 1 && base > 0) animated = 1;
    if (animated < 0) animated = 0;
    if (animated >= (int)ramp_count) animated = (int)ramp_count - 1;
    return (uint8_t)tone_ramp[animated];
}

bool ascii_art_grid_is_populated(size_t grid_width, size_t grid_height)
{
    return grid_width > 0 && grid_height > 0;
}

bool ascii_art_grid_clear(AsciiCell *cells,
                          size_t cell_capacity,
                          size_t *grid_width,
                          size_t *grid_height)
{
    if (cells == NULL || grid_width == NULL || grid_height == NULL ||
        !ascii_art_grid_is_populated(*grid_width, *grid_height) ||
        *grid_width > SIZE_MAX/ *grid_height) {
        return false;
    }
    size_t count = *grid_width* *grid_height;
    if (count > cell_capacity) return false;
    memset(cells, 0, count*sizeof(*cells));
    *grid_width = 0;
    *grid_height = 0;
    return true;
}
