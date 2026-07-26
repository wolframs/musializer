#include "lyric_lane_edit.h"

#include <math.h>
#include <string.h>

static size_t find_cue_index(const Lyrics_Document *document, uint64_t id)
{
    for (size_t i = 0; i < document->count; ++i) {
        if (document->cues[i].id == id) return i;
    }
    return (size_t)-1;
}

Lyric_Lane_Hit lyric_lane_hit_test(const Lyrics_Document *document,
                                   const Timeline_View *view,
                                   double lane_x, double lane_width,
                                   double pointer_x)
{
    Lyric_Lane_Hit hit = {LYRIC_LANE_ZONE_NONE, 0};
    if (document == NULL || view == NULL) return hit;
    if (!isfinite(lane_x) || !isfinite(lane_width) || lane_width <= 0.0) return hit;
    if (!isfinite(pointer_x)) return hit;
    if (pointer_x < lane_x || pointer_x > lane_x + lane_width) return hit;

    // Later cues are painted over earlier ones, so walking backwards makes the
    // hit test agree with what the eye picks out of an overlap.
    for (size_t step = document->count; step > 0; --step) {
        const Lyric_Cue *cue = &document->cues[step - 1];
        double left = timeline_view_x_at(view, cue->start_seconds, lane_x, lane_width);
        double right = timeline_view_x_at(view, cue->end_seconds, lane_x, lane_width);
        if (!isfinite(left) || !isfinite(right)) continue;
        // Match the minimum drawn width so a cue too short to see is still a
        // target; the drawing does the same widening.
        if (right - left < 3.0) right = left + 3.0;
        if (pointer_x < left || pointer_x > right) continue;

        hit.id = cue->id;
        hit.zone = LYRIC_LANE_ZONE_BODY;
        if (right - left >= LYRIC_LANE_EDGE_MIN_BLOCK_PIXELS) {
            // Only offer a handle for a boundary that is actually on screen.
            bool start_visible = left >= lane_x;
            bool end_visible = right <= lane_x + lane_width;
            if (start_visible && pointer_x <= left + LYRIC_LANE_EDGE_GRAB_PIXELS) {
                hit.zone = LYRIC_LANE_ZONE_START_EDGE;
            } else if (end_visible && pointer_x >= right - LYRIC_LANE_EDGE_GRAB_PIXELS) {
                hit.zone = LYRIC_LANE_ZONE_END_EDGE;
            }
        }
        return hit;
    }
    return hit;
}

void lyric_lane_selection_clear(Lyric_Lane_Selection *selection)
{
    if (selection == NULL) return;
    memset(selection, 0, sizeof(*selection));
}

bool lyric_lane_selection_contains(const Lyric_Lane_Selection *selection,
                                   uint64_t id)
{
    if (selection == NULL || id == 0) return false;
    for (size_t i = 0; i < selection->count; ++i) {
        if (selection->ids[i] == id) return true;
    }
    return false;
}

static bool selection_add(Lyric_Lane_Selection *selection, uint64_t id)
{
    if (lyric_lane_selection_contains(selection, id)) return true;
    if (selection->count >= LYRIC_LANE_SELECTION_CAPACITY) return false;
    selection->ids[selection->count++] = id;
    return true;
}

static void selection_remove(Lyric_Lane_Selection *selection, uint64_t id)
{
    for (size_t i = 0; i < selection->count; ++i) {
        if (selection->ids[i] != id) continue;
        memmove(&selection->ids[i], &selection->ids[i + 1],
                (selection->count - i - 1)*sizeof(selection->ids[0]));
        selection->count -= 1;
        selection->ids[selection->count] = 0;
        return;
    }
}

bool lyric_lane_selection_apply(Lyric_Lane_Selection *selection,
                                const Lyrics_Document *document,
                                uint64_t id, Lyric_Lane_Click mode)
{
    if (selection == NULL || document == NULL || id == 0) return false;
    size_t index = find_cue_index(document, id);
    if (index == (size_t)-1) return false;

    switch (mode) {
    case LYRIC_LANE_CLICK_REPLACE:
        lyric_lane_selection_clear(selection);
        selection->ids[0] = id;
        selection->count = 1;
        selection->anchor_id = id;
        return true;

    case LYRIC_LANE_CLICK_TOGGLE:
        if (lyric_lane_selection_contains(selection, id)) {
            selection_remove(selection, id);
            // Removing the anchor would leave a later shift+click extending
            // from a cue that is no longer part of the selection.
            if (selection->anchor_id == id) {
                selection->anchor_id = selection->count > 0 ?
                    selection->ids[selection->count - 1] : 0;
            }
            return true;
        }
        if (!selection_add(selection, id)) return false;
        selection->anchor_id = id;
        return true;

    case LYRIC_LANE_CLICK_EXTEND: {
        size_t anchor = selection->anchor_id != 0 ?
            find_cue_index(document, selection->anchor_id) : (size_t)-1;
        // Shift with nothing to extend from behaves as a plain click rather
        // than doing nothing, which is what every list control does.
        if (anchor == (size_t)-1) {
            return lyric_lane_selection_apply(selection, document, id,
                                              LYRIC_LANE_CLICK_REPLACE);
        }
        size_t low = anchor < index ? anchor : index;
        size_t high = anchor < index ? index : anchor;
        if (high - low + 1u > LYRIC_LANE_SELECTION_CAPACITY) return false;
        uint64_t keep_anchor = selection->anchor_id;
        lyric_lane_selection_clear(selection);
        for (size_t i = low; i <= high; ++i) {
            selection->ids[selection->count++] = document->cues[i].id;
        }
        // The anchor stays put so dragging the shift+click back and forth
        // grows and shrinks one range instead of walking it along.
        selection->anchor_id = keep_anchor;
        return true;
    }
    }
    return false;
}

void lyric_lane_selection_prune(Lyric_Lane_Selection *selection,
                                const Lyrics_Document *document)
{
    if (selection == NULL || document == NULL) return;
    size_t kept = 0;
    for (size_t i = 0; i < selection->count; ++i) {
        if (find_cue_index(document, selection->ids[i]) == (size_t)-1) continue;
        selection->ids[kept++] = selection->ids[i];
    }
    for (size_t i = kept; i < selection->count; ++i) selection->ids[i] = 0;
    selection->count = kept;
    if (selection->anchor_id != 0 &&
        find_cue_index(document, selection->anchor_id) == (size_t)-1) {
        selection->anchor_id = kept > 0 ? selection->ids[kept - 1] : 0;
    }
}

double lyric_lane_clamp_move(const Lyrics_Document *document,
                             const Lyric_Lane_Selection *selection,
                             double delta_seconds)
{
    if (document == NULL || selection == NULL || selection->count == 0) return 0.0;
    if (!isfinite(delta_seconds)) return 0.0;
    double backward = 0.0;
    double forward = 0.0;
    if (lyrics_shift_headroom(document, selection->ids, selection->count,
                              &backward, &forward) != LYRICS_OK) {
        return 0.0;
    }
    if (delta_seconds < -backward) return -backward;
    if (delta_seconds > forward) return forward;
    return delta_seconds;
}

bool lyric_lane_clamp_resize(const Lyrics_Document *document, uint64_t id,
                             bool moving_start, double proposed_seconds,
                             double *start_seconds, double *end_seconds)
{
    if (document == NULL || start_seconds == NULL || end_seconds == NULL) return false;
    if (!isfinite(proposed_seconds)) return false;
    const Lyric_Cue *cue = lyrics_find(document, id);
    if (cue == NULL) return false;

    double start = cue->start_seconds;
    double end = cue->end_seconds;
    if (moving_start) {
        double latest = end - LYRIC_LANE_MIN_CUE_SECONDS;
        start = proposed_seconds;
        if (start < 0.0) start = 0.0;
        if (start > latest) start = latest;
        // A cue already shorter than the floor cannot be shortened further,
        // but must still be movable: leave it exactly as it was.
        if (start > end) start = cue->start_seconds;
    } else {
        double earliest = start + LYRIC_LANE_MIN_CUE_SECONDS;
        end = proposed_seconds;
        if (end > document->duration_seconds) end = document->duration_seconds;
        if (end < earliest) end = earliest;
        if (end > document->duration_seconds) end = cue->end_seconds;
    }
    if (!(end > start)) return false;
    *start_seconds = start;
    *end_seconds = end;
    return true;
}
