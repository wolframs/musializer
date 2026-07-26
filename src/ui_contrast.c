#include "ui_contrast.h"

#include <math.h>

static double ui_contrast_channel(uint32_t rgba, unsigned shift)
{
    double value = (double)((rgba >> shift) & 0xFFu)/255.0;
    // sRGB gamma expansion, exactly as WCAG 2.1 defines it.
    if (value <= 0.03928) return value/12.92;
    return pow((value + 0.055)/1.055, 2.4);
}

double ui_contrast_relative_luminance(uint32_t rgba)
{
    return 0.2126*ui_contrast_channel(rgba, 24) +
           0.7152*ui_contrast_channel(rgba, 16) +
           0.0722*ui_contrast_channel(rgba, 8);
}

double ui_contrast_ratio(uint32_t foreground, uint32_t background)
{
    double first = ui_contrast_relative_luminance(foreground);
    double second = ui_contrast_relative_luminance(background);
    double lighter = first > second ? first : second;
    double darker = first > second ? second : first;
    return (lighter + 0.05)/(darker + 0.05);
}

uint32_t ui_contrast_blend(uint32_t foreground, uint32_t background, double alpha)
{
    if (!(alpha >= 0.0)) alpha = 0.0;   // also catches NaN
    if (alpha > 1.0) alpha = 1.0;
    uint32_t result = 0xFFu;            // opaque once composited
    for (unsigned shift = 24; shift >= 8; shift -= 8) {
        double front = (double)((foreground >> shift) & 0xFFu);
        double back = (double)((background >> shift) & 0xFFu);
        double mixed = front*alpha + back*(1.0 - alpha);
        if (mixed < 0.0) mixed = 0.0;
        if (mixed > 255.0) mixed = 255.0;
        result |= ((uint32_t)(mixed + 0.5) & 0xFFu) << shift;
    }
    return result;
}
