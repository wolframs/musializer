#ifndef MUSIALIZER_LYRICS_EDITOR_LAYOUT_H_
#define MUSIALIZER_LYRICS_EDITOR_LAYOUT_H_

#include <stdbool.h>
#include <stddef.h>

#include "workspace_layout.h"

// The lyric editor used to be given a fixed 172 px panel at every window size,
// carved out of a timeline whose height was the constant 330. Its editing form
// needs 223 px, so Apply, Discard and Delete were drawn 20 px below the panel
// and 14 px below the bottom of the framebuffer -- at 960x640, 1280x720 and
// 1920x1080 alike. The list showed three cues out of a 1024-cue capacity on a
// full-HD display while half the window sat empty.
//
// The panel now asks for the height its content needs, bounded so the sidebar
// keeps a usable scene browser and tracks panel. Keeping the policy here, free
// of Raylib, is what lets the "form fits inside the panel" promise be a test.

// One cue row, and the chrome around the list: padding above and below, the
// "LYRIC CUES" header, and the slack the row loop already assumes.
#define LYRIC_EDITOR_ROW_HEIGHT 36.0f
#define LYRIC_EDITOR_LIST_CHROME 52.0f

// The editing form's own extent: 10 px padding, then the header, the two time
// rows, the text field, the action row at +146 with its 36 px height, and the
// Ctrl+Enter hint below it, then 10 px padding.
#define LYRIC_EDITOR_FORM_MINIMUM 223.0f

// Rows worth growing for. Below four the form dominates anyway; above twelve the
// panel would eat the preview for a list that scrolls perfectly well.
#define LYRIC_EDITOR_MIN_ROWS 4u
#define LYRIC_EDITOR_MAX_ROWS 12u

// Controls, transport, waveform lane and their margins, above the panel. This is
// the same constant assist_timeline_height documents, so the two cannot drift.
#define LYRIC_EDITOR_TIMELINE_CHROME 158.0f

// Sidebar the panel refuses to squeeze below: the scene browser's content floor
// plus a tracks panel that can still host its action row.
#define LYRIC_EDITOR_SIDEBAR_MINIMUM \
    (WORKSPACE_SCENES_MINIMUM + WORKSPACE_TRACKS_SINGLE_MINIMUM)

// Panel height that would show every cue, clamped to the row bounds above.
float lyric_editor_required_height(size_t cue_count);

// Height to request for this window: what the content wants, reduced to protect
// the sidebar, but never below the form minimum. The form is the point of the
// panel, so on the shortest windows the sidebar yields rather than the form.
float lyric_editor_panel_height(float screen_height, size_t cue_count);

// Cue rows a panel of this height can show, and whether the form fits at all.
size_t lyric_editor_visible_rows(float panel_height);
bool lyric_editor_form_fits(float panel_height);

#endif // MUSIALIZER_LYRICS_EDITOR_LAYOUT_H_
