#ifndef MUSIALIZER_UI_CONTRAST_H_
#define MUSIALIZER_UI_CONTRAST_H_

#include <stdbool.h>
#include <stdint.h>

// WCAG 2.1 relative luminance and contrast ratio over packed 0xRRGGBBAA colours
// (see ui_palette.h). Deliberately raylib-free so the palette can be checked in
// the headless test suite rather than by eye.
//
// Thresholds, for reference at the call sites:
//   4.5 : normal-size body text (AA)
//   3.0 : large text, and non-text UI components such as rules and borders
// Alpha is ignored: these ratios describe fully composited colours, so a caller
// blending a colour must pass the composited result, not the source.

// 0 for the darkest colour, 1 for the brightest.
double ui_contrast_relative_luminance(uint32_t rgba);

// Symmetric: order of arguments does not matter. Ranges from 1.0 to 21.0.
double ui_contrast_ratio(uint32_t foreground, uint32_t background);

// Composites `foreground` over `background` at the given alpha (0..1) and
// returns the packed result, so a translucent overlay can be checked at the
// opacity it is actually drawn with.
uint32_t ui_contrast_blend(uint32_t foreground, uint32_t background, double alpha);

#endif // MUSIALIZER_UI_CONTRAST_H_
