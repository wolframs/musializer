#ifndef MUSIALIZER_UI_THEME_H_
#define MUSIALIZER_UI_THEME_H_

#include <raylib.h>

#include "ui_palette.h"

// Shared desktop-UI palette, HUD metrics, font metrics, and key bindings.
// These were originally private to plug.c; they move here so extracted UI
// modules use the same visual language without re-defining it.
//
// The colour values themselves live in ui_palette.h, which is raylib-free so
// tests/test_ui_contrast.c can check the same numbers the application draws
// with. Add a colour there, not here.

#define COLOR_ACCENT                  GetColor(UI_RGBA_ACCENT)
#define COLOR_BACKGROUND              GetColor(UI_RGBA_BACKGROUND)
#define COLOR_UI_SURFACE              GetColor(UI_RGBA_UI_SURFACE)
#define COLOR_UI_RAISED               GetColor(UI_RGBA_UI_RAISED)
#define COLOR_UI_INK                  GetColor(UI_RGBA_UI_INK)
#define COLOR_UI_MUTED                GetColor(UI_RGBA_UI_MUTED)
#define COLOR_UI_DISABLED             GetColor(UI_RGBA_UI_DISABLED)
#define COLOR_UI_RULE                 GetColor(UI_RGBA_UI_RULE)
#define COLOR_UI_DANGER               GetColor(UI_RGBA_UI_DANGER)
#define COLOR_UI_WARNING              GetColor(UI_RGBA_UI_WARNING)
#define COLOR_UI_SUCCESS              GetColor(UI_RGBA_UI_SUCCESS)
#define COLOR_TRACK_PANEL_BACKGROUND  COLOR_UI_SURFACE
#define COLOR_TRACK_BUTTON_BACKGROUND COLOR_UI_RAISED
#define COLOR_TRACK_BUTTON_HOVEROVER  GetColor(UI_RGBA_TRACK_BUTTON_HOVEROVER)
#define COLOR_TRACK_BUTTON_SELECTED   COLOR_ACCENT
#define COLOR_TIMELINE_CURSOR         COLOR_ACCENT
#define COLOR_TIMELINE_BACKGROUND     COLOR_UI_SURFACE
#define COLOR_HUD_BUTTON_BACKGROUND   COLOR_TRACK_BUTTON_BACKGROUND
#define COLOR_HUD_BUTTON_HOVEROVER    COLOR_TRACK_BUTTON_HOVEROVER
#define COLOR_TOOLTIP_BACKGROUND      COLOR_UI_INK
#define COLOR_TOOLTIP_FOREGROUND      WHITE

#define HUD_TIMER_SECS 1.0f
#define HUD_BUTTON_SIZE 50
#define HUD_BUTTON_MARGIN 50
#define HUD_ICON_SCALE 0.5
#define HUD_POPUP_LIFETIME_SECS 2.0f
#define HUD_POPUP_SLIDEIN_SECS 0.1f
#define TOOLTIP_PADDING 20.0f
#define TRACKLABEL_SCROLL_SECS 0.05f

#define UI_FONT_HEADER 19.0f
#define UI_FONT_LABEL 16.0f
#define UI_FONT_CAPTION 13.0f
#define UI_FONT_VALUE 15.0f
#define UI_PANEL_PADDING 10.0f
#define UI_CONTROL_GAP 8.0f
#define UI_BUTTON_HEIGHT 36.0f
#define UI_COMPACT_BUTTON_HEIGHT 30.0f

#define KEY_TOGGLE_PLAY KEY_SPACE
#define KEY_RENDER      KEY_R
#define KEY_FULLSCREEN  KEY_F
#define KEY_CAPTURE     KEY_C
#define KEY_TOGGLE_MUTE KEY_M

#endif // MUSIALIZER_UI_THEME_H_
