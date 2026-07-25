#include "lyrics_editor_layout.h"

#include <math.h>

float lyric_editor_required_height(size_t cue_count)
{
    size_t rows = cue_count;
    if (rows < LYRIC_EDITOR_MIN_ROWS) rows = LYRIC_EDITOR_MIN_ROWS;
    if (rows > LYRIC_EDITOR_MAX_ROWS) rows = LYRIC_EDITOR_MAX_ROWS;
    float wanted = LYRIC_EDITOR_LIST_CHROME + (float)rows*LYRIC_EDITOR_ROW_HEIGHT;
    return wanted < LYRIC_EDITOR_FORM_MINIMUM ? LYRIC_EDITOR_FORM_MINIMUM : wanted;
}

float lyric_editor_panel_height(float screen_height, size_t cue_count)
{
    if (!isfinite(screen_height) || screen_height <= 0.0f) {
        return LYRIC_EDITOR_FORM_MINIMUM;
    }
    float wanted = lyric_editor_required_height(cue_count);
    float ceiling = screen_height - LYRIC_EDITOR_SIDEBAR_MINIMUM -
                    LYRIC_EDITOR_TIMELINE_CHROME;
    if (wanted > ceiling) wanted = ceiling;
    // A window too short to give both is a window where the editing controls
    // matter more than the track list; workspace_sidebar_layout collapses the
    // tracks panel cleanly rather than letting it overflow.
    if (wanted < LYRIC_EDITOR_FORM_MINIMUM) wanted = LYRIC_EDITOR_FORM_MINIMUM;
    return wanted;
}

size_t lyric_editor_visible_rows(float panel_height)
{
    if (!isfinite(panel_height)) return 0;
    // The drawing code derives the list from the panel the same way: the list
    // loses the panel's vertical padding, then the header above the first row.
    float list_height = panel_height - 20.0f;
    if (list_height <= LYRIC_EDITOR_ROW_HEIGHT) return 0;
    float rows = (list_height - 32.0f)/LYRIC_EDITOR_ROW_HEIGHT;
    if (rows <= 0.0f) return 0;
    return (size_t)rows;
}

bool lyric_editor_form_fits(float panel_height)
{
    return isfinite(panel_height) && panel_height >= LYRIC_EDITOR_FORM_MINIMUM;
}
