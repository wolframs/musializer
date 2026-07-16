#ifndef MUSIALIZER_UI_THEME_H_
#define MUSIALIZER_UI_THEME_H_

#include <raylib.h>

// Shared desktop-UI palette, HUD metrics, font metrics, and key bindings.
// These were originally private to plug.c; they move here unchanged so
// extracted UI modules use the same visual language without re-defining it.
// Values are verbatim from plug.c; do not "tidy" them here.

#define COLOR_ACCENT                  GetColor(0x002FA7FF)
#define COLOR_BACKGROUND              GetColor(0x151515FF)
#define COLOR_UI_SURFACE              GetColor(0xF7F7F8FF)
#define COLOR_UI_RAISED               GetColor(0xFFFFFFFF)
#define COLOR_UI_INK                  GetColor(0x141414FF)
#define COLOR_UI_MUTED                GetColor(0x66666BFF)
#define COLOR_UI_DISABLED             GetColor(0x929298FF)
#define COLOR_UI_RULE                 GetColor(0xD2D2D6FF)
#define COLOR_UI_DANGER               GetColor(0xC62828FF)
#define COLOR_UI_WARNING              GetColor(0xB26A00FF)
#define COLOR_UI_SUCCESS              GetColor(0x18794EFF)
#define COLOR_TRACK_PANEL_BACKGROUND  COLOR_UI_SURFACE
#define COLOR_TRACK_BUTTON_BACKGROUND COLOR_UI_RAISED
#define COLOR_TRACK_BUTTON_HOVEROVER  GetColor(0xE7EAF2FF)
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
