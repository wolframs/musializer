#ifndef MUSIALIZER_WORKSPACE_LAYOUT_H_
#define MUSIALIZER_WORKSPACE_LAYOUT_H_

#include <stdbool.h>
#include <stddef.h>

// The workspace sidebar splits one vertical budget between the tracks panel and
// the scene browser. That split used to be three lines of inline arithmetic in
// plug.c, and it could hand the tracks panel zero height while the panel drew
// and registered its action buttons at a fixed offset anyway. Because the tracks
// panel is drawn before the scene browser, and ui_widgets_button_with_id awards a
// press to whichever widget claimed it first, those invisible buttons took
// clicks aimed at the scene tiles painted over them: choosing a scene ran Save,
// or opened a file dialog.
//
// The rule this module enforces is that a panel which cannot contain its own
// controls is not drawn at all. Keeping it Raylib-free lets the budget and the
// containment promise be asserted headlessly, following caption_layout and
// scene_settings_ui_layout.

typedef struct {
    float x, y, width, height;
} Ui_Rect;

bool ui_rect_is_finite(Ui_Rect rect);
bool ui_rect_is_empty(Ui_Rect rect);
// Inclusive containment: an inner rect flush with an outer edge is contained.
bool ui_rect_contains(Ui_Rect outer, Ui_Rect inner);
// Touching edges do not overlap; only a positive-area intersection does.
bool ui_rect_overlaps(Ui_Rect a, Ui_Rect b);
Ui_Rect ui_rect_intersect(Ui_Rect a, Ui_Rect b);

// How much of the tracks panel the budget can actually host.
typedef enum {
    TRACKS_PANEL_HIDDEN = 0,  // Not drawn: no room for even one action row.
    TRACKS_PANEL_SINGLE,      // One row of four actions, then the track list.
    TRACKS_PANEL_STACKED,     // Two rows of two actions, then the track list.
} Tracks_Panel_Mode;

// Action row geometry, shared with the drawing code so the two cannot drift.
#define WORKSPACE_TRACKS_SINGLE_ACTION_TOP 50.0f
#define WORKSPACE_TRACKS_SINGLE_ACTION_HEIGHT 36.0f
#define WORKSPACE_TRACKS_STACKED_ACTION_TOP 46.0f
#define WORKSPACE_TRACKS_STACKED_ACTION_HEIGHT 32.0f
#define WORKSPACE_TRACKS_ACTION_GAP 4.0f
#define WORKSPACE_TRACKS_SINGLE_HEADER 96.0f
#define WORKSPACE_TRACKS_STACKED_HEADER 124.0f

// A panel qualifies for a mode only when it contains that mode's action row.
#define WORKSPACE_TRACKS_SINGLE_MINIMUM \
    (WORKSPACE_TRACKS_SINGLE_ACTION_TOP + WORKSPACE_TRACKS_SINGLE_ACTION_HEIGHT)
// Two rows are only worth their vertical cost once a track row still fits below.
#define WORKSPACE_TRACKS_STACKED_MINIMUM 168.0f

// scene_browser draws a 27 px header, a 36 px footer, 16 px of padding, four
// 4 px gaps, and five rows that clamp to a 24 px floor: 27+36+16+16+120.
#define WORKSPACE_SCENES_MINIMUM 215.0f
// Raised from 292 together with the scene-tile cap in plug.c's scene_browser.
// 292 was exactly enough for 38 px tiles, so raising either one alone changes
// nothing: the browser either cannot use the extra height or is not given it.
// 355 = 27 header + 36 footer + 16 padding + 5 rows of 52 + 4 gaps of 4.
#define WORKSPACE_SCENES_MAXIMUM 355.0f

// A track row is `sidebar_width*0.2` tall (see plug.c's tracks_panel), so the
// panel's content height depends on the width it was given.
#define WORKSPACE_TRACKS_ITEM_RATIO 0.2f
// Enough for a long track list to stay usable without starving the browser;
// the scene floor is what actually protects the browser, this just stops an
// enormous list asking for the whole sidebar.
#define WORKSPACE_TRACKS_MAXIMUM 520.0f

typedef struct {
    Ui_Rect tracks;   // Zero height when tracks_mode is TRACKS_PANEL_HIDDEN.
    Ui_Rect scenes;
    Tracks_Panel_Mode tracks_mode;
} Workspace_Sidebar;

// Splits the sidebar top to bottom. The two rects always tile the budget exactly
// and never overlap. The scene browser is served first because it has a hard
// content floor and no collapsed form; the tracks panel takes the remainder and
// degrades through the mode ladder, disappearing rather than overflowing.
//
// Returns false and leaves *out untouched for non-finite or non-positive input.
// track_count sizes the tracks panel to its content rather than letting it
// absorb every spare pixel. The empty list used to be the only elastic region
// in the sidebar, so a taller window grew whitespace under the tracks while the
// scene grid stayed pinned at its cap. Surplus now raises the browser instead.
bool workspace_sidebar_layout(float sidebar_width, float sidebar_height,
                              size_t track_count, Workspace_Sidebar *out);

// Offset and height of the action row a mode draws, measured from the panel top.
// Returns false for TRACKS_PANEL_HIDDEN, which draws no row.
bool workspace_tracks_action_row(Tracks_Panel_Mode mode, float *top, float *height);

#endif // MUSIALIZER_WORKSPACE_LAYOUT_H_
