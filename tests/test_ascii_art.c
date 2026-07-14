#include "ascii_art.h"
#include "test_support.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static void set_gray(uint8_t *pixels,
                     size_t width,
                     size_t x,
                     size_t y,
                     uint8_t value);

TEST(ascii_art_fits_landscape_portrait_and_small_images_without_distortion)
{
    size_t columns = 999;
    size_t rows = 999;
    REQUIRE_TRUE(ascii_art_fit_grid_dimensions(
        1408, 768, 96, 54, &columns, &rows));
    EXPECT_EQ_SIZE(columns, 96);
    EXPECT_EQ_SIZE(rows, 52);

    REQUIRE_TRUE(ascii_art_fit_grid_dimensions(
        768, 1408, 96, 54, &columns, &rows));
    EXPECT_EQ_SIZE(columns, 29);
    EXPECT_EQ_SIZE(rows, 54);

    REQUIRE_TRUE(ascii_art_fit_grid_dimensions(
        40, 20, 96, 54, &columns, &rows));
    EXPECT_EQ_SIZE(columns, 40);
    EXPECT_EQ_SIZE(rows, 20);
}

TEST(ascii_art_grid_fit_and_layout_are_atomic_on_invalid_input)
{
    size_t columns = 17;
    size_t rows = 19;
    EXPECT_FALSE(ascii_art_fit_grid_dimensions(
        0, 100, 96, 54, &columns, &rows));
    EXPECT_EQ_SIZE(columns, 17);
    EXPECT_EQ_SIZE(rows, 19);

    AsciiGridLayout layout = {.cell_width = 123.0f};
    EXPECT_FALSE(ascii_art_grid_layout(
        NAN, 360.0f, 96, 52, 1.0f, &layout));
    EXPECT_NEAR(layout.cell_width, 123.0f, 0.0f);
}

TEST(ascii_art_grid_layout_centers_and_scales_square_and_legacy_cells)
{
    AsciiGridLayout square;
    REQUIRE_TRUE(ascii_art_grid_layout(
        640.0f, 360.0f, 96, 52, 1.0f, &square));
    EXPECT_NEAR(square.cell_width, 640.0f/96.0f, 0.0001f);
    EXPECT_NEAR(square.cell_height, square.cell_width, 0.0001f);
    EXPECT_NEAR(square.field_width, 640.0f, 0.0001f);
    EXPECT_TRUE(square.offset_y > 0.0f);

    AsciiGridLayout doubled;
    REQUIRE_TRUE(ascii_art_grid_layout(
        1280.0f, 720.0f, 96, 52, 1.0f, &doubled));
    EXPECT_NEAR(doubled.cell_width, square.cell_width*2.0f, 0.0001f);
    EXPECT_NEAR(doubled.field_height, square.field_height*2.0f, 0.0001f);

    AsciiGridLayout legacy;
    REQUIRE_TRUE(ascii_art_grid_layout(
        640.0f, 360.0f, 96, 26, 0.5f, &legacy));
    EXPECT_NEAR(legacy.field_width/legacy.field_height,
                96.0f*0.5f/26.0f, 0.0001f);
}

static void set_gray(uint8_t *pixels,
                     size_t width,
                     size_t x,
                     size_t y,
                     uint8_t value)
{
    size_t offset = (y*width + x)*4u;
    pixels[offset] = value;
    pixels[offset + 1] = value;
    pixels[offset + 2] = value;
    pixels[offset + 3] = 255;
}

TEST(ascii_art_maps_flat_black_and_white_to_tone_extremes)
{
    const uint8_t black[4] = {0, 0, 0, 255};
    const uint8_t white[4] = {255, 255, 255, 255};
    AsciiCell cell;

    REQUIRE_TRUE(ascii_art_convert_rgba8(black, 1, 1, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.glyph, ' ');
    EXPECT_NEAR(cell.luminance, 0.0f, 0.0f);
    EXPECT_NEAR(cell.edge_strength, 0.0f, 0.0f);
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_NONE);

    REQUIRE_TRUE(ascii_art_convert_rgba8(white, 1, 1, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.glyph, '@');
    EXPECT_NEAR(cell.luminance, 1.0f, 0.0f);
    EXPECT_NEAR(cell.edge_strength, 0.0f, 0.0f);
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_NONE);
}

TEST(ascii_art_detects_vertical_and_horizontal_edges)
{
    uint8_t vertical[5*5*4] = {0};
    uint8_t horizontal[5*5*4] = {0};
    for (size_t y = 0; y < 5; ++y) {
        for (size_t x = 0; x < 5; ++x) {
            set_gray(vertical, 5, x, y, x >= 2 ? 255 : 0);
            set_gray(horizontal, 5, x, y, y >= 2 ? 255 : 0);
        }
    }

    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(vertical, 5, 5, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_VERTICAL);
    EXPECT_EQ_U64(cell.glyph, '|');
    EXPECT_TRUE(cell.edge_strength >= 0.05f);

    REQUIRE_TRUE(ascii_art_convert_rgba8(horizontal, 5, 5, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_HORIZONTAL);
    EXPECT_EQ_U64(cell.glyph, '-');
    EXPECT_TRUE(cell.edge_strength >= 0.05f);
}

TEST(ascii_art_detects_both_diagonal_edge_directions)
{
    uint8_t forward[7*7*4] = {0};
    uint8_t back[7*7*4] = {0};
    for (size_t y = 0; y < 7; ++y) {
        for (size_t x = 0; x < 7; ++x) {
            set_gray(forward, 7, x, y, x + y >= 6 ? 255 : 0);
            set_gray(back, 7, x, y, x >= y ? 255 : 0);
        }
    }

    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(forward, 7, 7, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_DIAGONAL_FORWARD);
    EXPECT_EQ_U64(cell.glyph, '/');

    REQUIRE_TRUE(ascii_art_convert_rgba8(back, 7, 7, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_DIAGONAL_BACK);
    EXPECT_EQ_U64(cell.glyph, '\\');
}

TEST(ascii_art_keeps_crossed_detail_as_tone_instead_of_arbitrary_edge)
{
    uint8_t cross[9*9*4] = {0};
    for (size_t y = 0; y < 9; ++y) {
        for (size_t x = 0; x < 9; ++x) {
            set_gray(cross, 9, x, y, (x == 4 || y == 4) ? 255 : 0);
        }
    }
    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(cross, 9, 9, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.edge_orientation, ASCII_EDGE_NONE);
    EXPECT_TRUE(cell.glyph != '-' && cell.glyph != '|' &&
                cell.glyph != '/' && cell.glyph != '\\');
}

TEST(ascii_art_averages_cell_color_and_alpha)
{
    const uint8_t pixels[2*2*4] = {
        10, 20, 30, 40,  30, 40, 50, 60,
        50, 60, 70, 80,  70, 80, 90, 100,
    };
    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixels, 2, 2, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.foreground.r, 47);
    EXPECT_EQ_U64(cell.foreground.g, 57);
    EXPECT_EQ_U64(cell.foreground.b, 67);
    EXPECT_EQ_U64(cell.foreground.a, 70);
}

TEST(ascii_art_ignores_fully_transparent_rgb_when_averaging_color)
{
    const uint8_t pixels[2*4] = {
        255, 0, 0, 0,
        0, 20, 240, 255,
    };
    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixels, 2, 1, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.foreground.r, 0);
    EXPECT_EQ_U64(cell.foreground.g, 20);
    EXPECT_EQ_U64(cell.foreground.b, 240);
    EXPECT_EQ_U64(cell.foreground.a, 128);
}

TEST(ascii_art_uses_robust_contrast_to_retain_narrow_range_detail)
{
    uint8_t pixels[64*4];
    for (size_t x = 0; x < 64; ++x) {
        set_gray(pixels, 64, x, 0, (uint8_t)(40 + x));
    }
    AsciiCell cells[4];
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixels, 64, 1, 4, 1, cells, 4));
    EXPECT_TRUE(cells[0].luminance < 0.35f);
    EXPECT_TRUE(cells[3].luminance > 0.80f);
    EXPECT_TRUE(cells[0].glyph != cells[3].glyph);
}

TEST(ascii_art_compensates_for_sparse_glyph_ink_in_dark_source_detail)
{
    const uint8_t pixel[4] = {32, 32, 32, 255};
    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixel, 1, 1, 1, 1, &cell, 1));
    EXPECT_TRUE(cell.luminance > 0.18f);
    EXPECT_TRUE(cell.luminance < 0.35f);
    EXPECT_TRUE(cell.glyph != ' ');
}

TEST(ascii_art_rejects_invalid_input_without_touching_output)
{
    const uint8_t pixel[4] = {0, 0, 0, 255};
    AsciiCell sentinel = {.glyph = 0x12345678u};
    EXPECT_FALSE(ascii_art_convert_rgba8(NULL, 1, 1, 1, 1, &sentinel, 1));
    EXPECT_FALSE(ascii_art_convert_rgba8(pixel, 0, 1, 1, 1, &sentinel, 1));
    EXPECT_FALSE(ascii_art_convert_rgba8(pixel, 1, 1, 2, 1, &sentinel, 2));
    EXPECT_FALSE(ascii_art_convert_rgba8(pixel, 1, 1, 1, 1, NULL, 1));
    EXPECT_FALSE(ascii_art_convert_rgba8(pixel, 1, 1, 1, 1, &sentinel, 0));
    EXPECT_EQ_U64(sentinel.glyph, 0x12345678u);
}

TEST(ascii_art_conversion_is_byte_reproducible)
{
    uint8_t pixels[8*6*4];
    for (size_t i = 0; i < sizeof(pixels); ++i) {
        pixels[i] = (uint8_t)((i*73u + 19u)%256u);
    }
    AsciiCell first[4*3];
    AsciiCell second[4*3];
    memset(first, 0xa5, sizeof(first));
    memset(second, 0x5a, sizeof(second));
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixels, 8, 6, 4, 3, first, 12));
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixels, 8, 6, 4, 3, second, 12));
    EXPECT_TRUE(memcmp(first, second, sizeof(first)) == 0);
}

TEST(ascii_art_animation_cycles_tone_glyphs_but_preserves_image_structure)
{
    AsciiCell tone = {
        .glyph = 'L',
        .luminance = 0.58f,
        .edge_orientation = ASCII_EDGE_NONE,
    };
    uint32_t first = ascii_art_animated_glyph(
        &tone, 7, 11, 0.0, 1.0f, UINT64_C(0x1234));
    bool changed = false;
    for (size_t frame = 1; frame < 80; ++frame) {
        uint32_t glyph = ascii_art_animated_glyph(
            &tone, 7, 11, (double)frame/12.0, 1.0f, UINT64_C(0x1234));
        EXPECT_TRUE(strchr(" .,:;i1tfLCG08#@", (int)glyph) != NULL);
        if (glyph != first) changed = true;
    }
    EXPECT_TRUE(changed);

    AsciiCell edge = tone;
    edge.glyph = '/';
    edge.edge_orientation = ASCII_EDGE_DIAGONAL_FORWARD;
    EXPECT_EQ_U64(ascii_art_animated_glyph(
        &edge, 7, 11, 99.0, 1.0f, UINT64_C(0x1234)), '/');

    AsciiCell empty = tone;
    empty.glyph = ' ';
    empty.luminance = 0.0f;
    EXPECT_EQ_U64(ascii_art_animated_glyph(
        &empty, 7, 11, 99.0, 1.0f, UINT64_C(0x1234)), ' ');
}

TEST(ascii_art_animation_is_seek_deterministic_and_sanitizes_inputs)
{
    AsciiCell cell = {
        .glyph = 'G',
        .luminance = 0.72f,
        .edge_orientation = ASCII_EDGE_NONE,
    };
    uint32_t seeked = ascii_art_animated_glyph(
        &cell, 19, 31, 47.125, 0.83f, UINT64_C(0xfeedbeef));
    EXPECT_EQ_U64(ascii_art_animated_glyph(
        &cell, 19, 31, 47.125, 0.83f, UINT64_C(0xfeedbeef)), seeked);
    EXPECT_EQ_U64(ascii_art_animated_glyph(
        &cell, 19, 31, NAN, NAN, UINT64_C(0xfeedbeef)),
        ascii_art_animated_glyph(
            &cell, 19, 31, 0.0, 0.0f, UINT64_C(0xfeedbeef)));
    EXPECT_EQ_U64(ascii_art_animated_glyph(
        NULL, 0, 0, 0.0, 0.0f, 0), 0);
}

TEST(ascii_art_grid_clear_is_explicit_bounded_and_atomic)
{
    AsciiCell cells[4];
    memset(cells, 0xa5, sizeof(cells));
    size_t width = 2;
    size_t height = 2;
    EXPECT_TRUE(ascii_art_grid_is_populated(width, height));
    REQUIRE_TRUE(ascii_art_grid_clear(cells, 4, &width, &height));
    EXPECT_EQ_SIZE(width, 0);
    EXPECT_EQ_SIZE(height, 0);
    AsciiCell zero[4] = {0};
    EXPECT_TRUE(memcmp(cells, zero, sizeof(cells)) == 0);
    EXPECT_FALSE(ascii_art_grid_is_populated(width, height));

    memset(cells, 0x3c, sizeof(cells));
    AsciiCell before[4];
    memcpy(before, cells, sizeof(cells));
    width = 3;
    height = 2;
    EXPECT_FALSE(ascii_art_grid_clear(cells, 4, &width, &height));
    EXPECT_EQ_SIZE(width, 3);
    EXPECT_EQ_SIZE(height, 2);
    EXPECT_TRUE(memcmp(cells, before, sizeof(cells)) == 0);
}
