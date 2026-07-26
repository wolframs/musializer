#ifndef MUSIALIZER_UI_PALETTE_H_
#define MUSIALIZER_UI_PALETTE_H_

// The raylib-free half of ui_theme.h: the palette as packed 0xRRGGBBAA values.
// ui_theme.h wraps each of these in GetColor, so a colour is written down once
// and the contrast tests in tests/test_ui_contrast.c can read the same numbers
// the application draws with. Splitting the values out follows the precedent of
// scene_settings_values.h.
//
// Do not add a colour to ui_theme.h without adding it here; a constant defined
// only there is invisible to the contrast suite.

#define UI_RGBA_ACCENT                  0x002FA7FFu
#define UI_RGBA_BACKGROUND              0x151515FFu
#define UI_RGBA_UI_SURFACE              0xF7F7F8FFu
#define UI_RGBA_UI_RAISED               0xFFFFFFFFu
#define UI_RGBA_UI_INK                  0x141414FFu
#define UI_RGBA_UI_MUTED                0x66666BFFu
#define UI_RGBA_UI_DISABLED             0x8C8C92FFu
#define UI_RGBA_UI_RULE                 0xD2D2D6FFu
#define UI_RGBA_UI_DANGER               0xC62828FFu
#define UI_RGBA_UI_WARNING              0x9E5D00FFu
#define UI_RGBA_UI_SUCCESS              0x18794EFFu
#define UI_RGBA_TRACK_BUTTON_HOVEROVER  0xE7EAF2FFu
#define UI_RGBA_WHITE                   0xFFFFFFFFu

#endif // MUSIALIZER_UI_PALETTE_H_
