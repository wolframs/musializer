#include "ascii_art.h"
#include "test_support.h"

#include <stdint.h>
#include <string.h>

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

TEST(ascii_art_averages_cell_color_and_alpha)
{
    const uint8_t pixels[2*2*4] = {
        10, 20, 30, 40,  30, 40, 50, 60,
        50, 60, 70, 80,  70, 80, 90, 100,
    };
    AsciiCell cell;
    REQUIRE_TRUE(ascii_art_convert_rgba8(pixels, 2, 2, 1, 1, &cell, 1));
    EXPECT_EQ_U64(cell.foreground.r, 40);
    EXPECT_EQ_U64(cell.foreground.g, 50);
    EXPECT_EQ_U64(cell.foreground.b, 60);
    EXPECT_EQ_U64(cell.foreground.a, 70);
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
