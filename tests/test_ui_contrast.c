#include "ui_contrast.h"
#include "ui_palette.h"
#include "test_support.h"

#include <math.h>

// WCAG AA for normal-size body text. Every text pair below has to clear this.
#define AA_TEXT 4.5
// WCAG 1.4.11 for non-text UI components and graphical objects.
#define AA_COMPONENT 3.0

// A macro rather than a helper so a failure reports the offending pair's line
// and names the colour in words, instead of an opaque expression.
#define EXPECT_TEXT_CONTRAST(what, foreground, background)                       \
    do {                                                                         \
        double contrast_ = ui_contrast_ratio((foreground), (background));        \
        if (contrast_ < AA_TEXT) {                                               \
            TEST_FAIL("%s reads at %.2f:1, below the %.1f:1 body-text minimum",  \
                      (what), contrast_, AA_TEXT);                               \
        }                                                                        \
    } while (0)

TEST(ui_contrast_matches_the_wcag_reference_values)
{
    // Anchors from the specification, so a bug in the gamma expansion cannot
    // hide behind palette values that happen to pass anyway.
    EXPECT_NEAR(ui_contrast_ratio(0x000000FFu, 0xFFFFFFFFu), 21.0, 0.001);
    EXPECT_NEAR(ui_contrast_ratio(0xFFFFFFFFu, 0xFFFFFFFFu), 1.0, 0.001);
    EXPECT_NEAR(ui_contrast_relative_luminance(0xFFFFFFFFu), 1.0, 0.0001);
    EXPECT_NEAR(ui_contrast_relative_luminance(0x000000FFu), 0.0, 0.0001);
    // Mid grey: the classic 0x777777 on white is just under AA.
    EXPECT_NEAR(ui_contrast_ratio(0x777777FFu, 0xFFFFFFFFu), 4.48, 0.01);
    // Symmetric in its arguments.
    EXPECT_NEAR(ui_contrast_ratio(0x002FA7FFu, 0xF7F7F8FFu),
                ui_contrast_ratio(0xF7F7F8FFu, 0x002FA7FFu), 0.0001);
}

TEST(ui_contrast_blend_composites_before_measuring)
{
    // A translucent colour must be measured at the opacity it is drawn with,
    // not as its source value.
    EXPECT_TRUE(ui_contrast_blend(0x000000FFu, 0xFFFFFFFFu, 0.0) == 0xFFFFFFFFu);
    EXPECT_TRUE(ui_contrast_blend(0x000000FFu, 0xFFFFFFFFu, 1.0) == 0x000000FFu);
    uint32_t half = ui_contrast_blend(0x000000FFu, 0xFFFFFFFFu, 0.5);
    EXPECT_TRUE(((half >> 24) & 0xFFu) == 128u);
    // Out-of-range and NaN alphas clamp rather than producing nonsense.
    EXPECT_TRUE(ui_contrast_blend(0x000000FFu, 0xFFFFFFFFu, -1.0) == 0xFFFFFFFFu);
    EXPECT_TRUE(ui_contrast_blend(0x000000FFu, 0xFFFFFFFFu, 2.0) == 0x000000FFu);
    EXPECT_TRUE(ui_contrast_blend(0x000000FFu, 0xFFFFFFFFu, NAN) == 0xFFFFFFFFu);
}

TEST(ui_palette_body_text_clears_wcag_aa)
{
    // Panel and button surfaces are the two backgrounds text lands on.
    EXPECT_TEXT_CONTRAST("ink on the panel surface",
                         UI_RGBA_UI_INK, UI_RGBA_UI_SURFACE);
    EXPECT_TEXT_CONTRAST("ink on a raised control",
                         UI_RGBA_UI_INK, UI_RGBA_UI_RAISED);
    EXPECT_TEXT_CONTRAST("ink on a hovered control",
                         UI_RGBA_UI_INK, UI_RGBA_TRACK_BUTTON_HOVEROVER);
    EXPECT_TEXT_CONTRAST("muted labels on the panel surface",
                         UI_RGBA_UI_MUTED, UI_RGBA_UI_SURFACE);
    EXPECT_TEXT_CONTRAST("muted labels on a raised control",
                         UI_RGBA_UI_MUTED, UI_RGBA_UI_RAISED);
    EXPECT_TEXT_CONTRAST("accent values on the panel surface",
                         UI_RGBA_ACCENT, UI_RGBA_UI_SURFACE);
    EXPECT_TEXT_CONTRAST("accent values on a raised control",
                         UI_RGBA_ACCENT, UI_RGBA_UI_RAISED);
    EXPECT_TEXT_CONTRAST("a selected control's white label",
                         UI_RGBA_WHITE, UI_RGBA_ACCENT);
    EXPECT_TEXT_CONTRAST("tooltip text", UI_RGBA_WHITE, UI_RGBA_UI_INK);

    // Notice severity labels are drawn in these colours on the notice card,
    // which is the panel surface. The amber failed here at 3.96:1 until it was
    // darkened from 0xB26A00 to 0x9E5D00.
    EXPECT_TEXT_CONTRAST("the warning severity label",
                         UI_RGBA_UI_WARNING, UI_RGBA_UI_SURFACE);
    EXPECT_TEXT_CONTRAST("the success severity label",
                         UI_RGBA_UI_SUCCESS, UI_RGBA_UI_SURFACE);
    EXPECT_TEXT_CONTRAST("danger labels on a raised control",
                         UI_RGBA_UI_DANGER, UI_RGBA_UI_RAISED);
}

TEST(ui_palette_disabled_text_stays_readable_but_clearly_recessed)
{
    // WCAG exempts disabled controls, so this is a house rule rather than a
    // conformance requirement: a disabled label should still be legible enough
    // to read the option you cannot pick.
    EXPECT_TRUE(ui_contrast_ratio(UI_RGBA_UI_DISABLED, UI_RGBA_UI_SURFACE) >=
                AA_COMPONENT);
    EXPECT_TRUE(ui_contrast_ratio(UI_RGBA_UI_DISABLED, UI_RGBA_UI_RAISED) >=
                AA_COMPONENT);

    // The design intent that actually matters: disabled must read as weaker
    // than muted, or "unavailable" and "secondary" become the same signal.
    EXPECT_TRUE(ui_contrast_ratio(UI_RGBA_UI_DISABLED, UI_RGBA_UI_SURFACE) <
                ui_contrast_ratio(UI_RGBA_UI_MUTED, UI_RGBA_UI_SURFACE));
}

TEST(ui_palette_control_borders_are_a_recorded_deviation)
{
    // PINS A KNOWN DEVIATION, not a passing standard. An enabled button is
    // COLOR_UI_RAISED (white) on COLOR_UI_SURFACE (near-white), which is about
    // 1.02:1, so its 1 px COLOR_UI_RULE border is very nearly the only thing
    // that identifies the control. WCAG 1.4.11 asks for 3:1 there.
    //
    // Raising COLOR_UI_RULE to clear it would darken every divider, rail, and
    // panel separator in the workspace, which is a visual-design decision
    // rather than a bug fix -- recorded as gate D7 in EXTENSION_PLAN.md.
    //
    // This test exists so the deviation is measured rather than forgotten. If
    // it fails because the rule got darker, that is the gate being answered:
    // update the numbers and delete this comment.
    double on_surface = ui_contrast_ratio(UI_RGBA_UI_RULE, UI_RGBA_UI_SURFACE);
    double on_raised = ui_contrast_ratio(UI_RGBA_UI_RULE, UI_RGBA_UI_RAISED);
    EXPECT_NEAR(on_surface, 1.41, 0.02);
    EXPECT_NEAR(on_raised, 1.51, 0.02);
    EXPECT_TRUE(on_surface < AA_COMPONENT);

    // A selected control does not rely on the rule: it fills with the accent.
    EXPECT_TRUE(ui_contrast_ratio(UI_RGBA_ACCENT, UI_RGBA_UI_SURFACE) >=
                AA_COMPONENT);
}
