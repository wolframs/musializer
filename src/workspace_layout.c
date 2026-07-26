#include "workspace_layout.h"

#include <math.h>
#include <stddef.h>

bool ui_rect_is_finite(Ui_Rect rect)
{
    return isfinite(rect.x) && isfinite(rect.y) &&
           isfinite(rect.width) && isfinite(rect.height);
}

bool ui_rect_is_empty(Ui_Rect rect)
{
    return !ui_rect_is_finite(rect) || rect.width <= 0.0f || rect.height <= 0.0f;
}

bool ui_rect_contains(Ui_Rect outer, Ui_Rect inner)
{
    if (!ui_rect_is_finite(outer) || !ui_rect_is_finite(inner)) return false;
    // An empty inner rect has no pixels to fall outside, but it also has no
    // business being registered as a hit box, so report it as not contained.
    if (ui_rect_is_empty(inner) || ui_rect_is_empty(outer)) return false;
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

bool ui_rect_overlaps(Ui_Rect a, Ui_Rect b)
{
    if (ui_rect_is_empty(a) || ui_rect_is_empty(b)) return false;
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

Ui_Rect ui_rect_intersect(Ui_Rect a, Ui_Rect b)
{
    Ui_Rect empty = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!ui_rect_overlaps(a, b)) return empty;
    float x = a.x > b.x ? a.x : b.x;
    float y = a.y > b.y ? a.y : b.y;
    float right = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    float bottom = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    Ui_Rect result = {x, y, right - x, bottom - y};
    return result;
}

bool workspace_tracks_action_row(Tracks_Panel_Mode mode, float *top, float *height)
{
    if (top == NULL || height == NULL) return false;
    switch (mode) {
    case TRACKS_PANEL_SINGLE:
        *top = WORKSPACE_TRACKS_SINGLE_ACTION_TOP;
        *height = WORKSPACE_TRACKS_SINGLE_ACTION_HEIGHT;
        return true;
    case TRACKS_PANEL_STACKED:
        *top = WORKSPACE_TRACKS_STACKED_ACTION_TOP;
        // Two rows and the gap between them.
        *height = WORKSPACE_TRACKS_STACKED_ACTION_HEIGHT*2.0f +
                  WORKSPACE_TRACKS_ACTION_GAP;
        return true;
    case TRACKS_PANEL_HIDDEN:
        break;
    }
    return false;
}

bool workspace_sidebar_layout(float sidebar_width, float sidebar_height,
                              size_t track_count, Workspace_Sidebar *out)
{
    if (out == NULL) return false;
    if (!isfinite(sidebar_width) || !isfinite(sidebar_height)) return false;
    if (sidebar_width <= 0.0f || sidebar_height <= 0.0f) return false;

    // What the tracks panel actually needs for its chrome and its rows. Asking
    // for its content instead of taking the remainder is the whole point: an
    // empty or short list no longer turns a taller window into whitespace.
    float tracks_wanted = WORKSPACE_TRACKS_STACKED_HEADER +
                          (float)track_count*sidebar_width*WORKSPACE_TRACKS_ITEM_RATIO;
    if (!isfinite(tracks_wanted) || tracks_wanted < WORKSPACE_TRACKS_STACKED_MINIMUM) {
        tracks_wanted = WORKSPACE_TRACKS_STACKED_MINIMUM;
    }
    if (tracks_wanted > WORKSPACE_TRACKS_MAXIMUM) {
        tracks_wanted = WORKSPACE_TRACKS_MAXIMUM;
    }

    // The scene browser then takes what is left, still bounded by its own floor
    // and cap, so surplus height reaches the scene grid rather than the gap
    // under the track list.
    float scenes_height = sidebar_height - tracks_wanted;
    if (scenes_height > WORKSPACE_SCENES_MAXIMUM) {
        scenes_height = WORKSPACE_SCENES_MAXIMUM;
    }
    if (scenes_height < WORKSPACE_SCENES_MINIMUM) {
        scenes_height = WORKSPACE_SCENES_MINIMUM;
    }
    // A sidebar smaller than the scene floor gives the browser everything it can.
    if (scenes_height > sidebar_height) scenes_height = sidebar_height;

    float tracks_height = sidebar_height - scenes_height;
    if (tracks_height < 0.0f) tracks_height = 0.0f;

    Tracks_Panel_Mode mode =
        tracks_height >= WORKSPACE_TRACKS_STACKED_MINIMUM ? TRACKS_PANEL_STACKED :
        tracks_height >= WORKSPACE_TRACKS_SINGLE_MINIMUM  ? TRACKS_PANEL_SINGLE  :
                                                            TRACKS_PANEL_HIDDEN;
    // Below one action row the panel would draw its buttons past its own bottom
    // edge, over the scene browser, and win their clicks. Yield the space instead.
    if (mode == TRACKS_PANEL_HIDDEN) {
        tracks_height = 0.0f;
        scenes_height = sidebar_height;
    }

    Workspace_Sidebar layout = {
        .tracks = {0.0f, 0.0f, sidebar_width, tracks_height},
        .scenes = {0.0f, tracks_height, sidebar_width, scenes_height},
        .tracks_mode = mode,
    };
    *out = layout;
    return true;
}
