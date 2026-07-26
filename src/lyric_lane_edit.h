#ifndef MUSIALIZER_LYRIC_LANE_EDIT_H_
#define MUSIALIZER_LYRIC_LANE_EDIT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lyrics.h"
#include "timeline_view.h"

// Hit testing, selection and drag clamping for direct manipulation of lyric
// cues in the timeline lane. None of it touches Raylib: the immediate-mode
// caller polls the mouse and paints, this decides what the gesture means.
//
// Keeping it separate is what makes the rules checkable at all. The lane is
// 22 px tall and its blocks are a few pixels wide on a long track, so "did that
// press land on the trailing edge of cue 7 or the body of cue 8" is not a
// question a screenshot can answer.

// How many cues one gesture can move together. A drag has to hold the whole
// selection in a fixed-capacity list because the commit is a single atomic
// operation; 64 is far past what anyone hand-picks and keeps the struct small
// enough to live in the hot-reloaded Plug state.
#define LYRIC_LANE_SELECTION_CAPACITY 64u

// Pixels at each end of a block that grab the boundary instead of the body.
#define LYRIC_LANE_EDGE_GRAB_PIXELS 5.0

// A block narrower than this offers no edge handles at all. Without the rule a
// 9 px block would be nothing but handles and could never be moved, only
// resized -- and at that width the two edges are not distinguishable by hand.
#define LYRIC_LANE_EDGE_MIN_BLOCK_PIXELS 18.0

// Shortest cue a resize may produce. lyrics_update only demands end > start, so
// without a floor a single sloppy drag can collapse a cue to a sliver that is
// then impossible to grab again.
#define LYRIC_LANE_MIN_CUE_SECONDS 0.02

// Pointer travel before a press counts as a drag rather than a click. Below it
// the gesture is a selection, so a click never nudges a cue by a pixel's worth
// of time and silently dirties the project.
#define LYRIC_LANE_DRAG_THRESHOLD_PIXELS 3.0

typedef enum Lyric_Lane_Zone {
    LYRIC_LANE_ZONE_NONE = 0,
    LYRIC_LANE_ZONE_BODY,
    LYRIC_LANE_ZONE_START_EDGE,
    LYRIC_LANE_ZONE_END_EDGE,
} Lyric_Lane_Zone;

typedef struct Lyric_Lane_Hit {
    Lyric_Lane_Zone zone;
    uint64_t id;
} Lyric_Lane_Hit;

typedef struct Lyric_Lane_Selection {
    uint64_t ids[LYRIC_LANE_SELECTION_CAPACITY];
    size_t count;
    // The fixed end of a shift+click range. Cleared with the selection.
    uint64_t anchor_id;
} Lyric_Lane_Selection;

typedef enum Lyric_Lane_Click {
    // A plain click: the cue becomes the whole selection.
    LYRIC_LANE_CLICK_REPLACE = 0,
    // Ctrl: add or remove this one cue, leaving the rest alone.
    LYRIC_LANE_CLICK_TOGGLE,
    // Shift: select every cue between the anchor and this one inclusive.
    LYRIC_LANE_CLICK_EXTEND,
} Lyric_Lane_Click;

// The cue under the pointer, preferring the later one where blocks overlap,
// because that is the one painted on top. Cues whose block is off-screen are
// never hit: an edge that scrolled out of view must not offer a handle at the
// lane border for a boundary that is not there.
Lyric_Lane_Hit lyric_lane_hit_test(const Lyrics_Document *document,
                                   const Timeline_View *view,
                                   double lane_x, double lane_width,
                                   double pointer_x);

void lyric_lane_selection_clear(Lyric_Lane_Selection *selection);
bool lyric_lane_selection_contains(const Lyric_Lane_Selection *selection,
                                   uint64_t id);

// Applies a click. Returns false and leaves the selection untouched when the
// request does not fit the capacity or the id is not in the document, so a
// range drag across a thousand cues fails visibly instead of silently
// selecting the first sixty-four.
bool lyric_lane_selection_apply(Lyric_Lane_Selection *selection,
                                const Lyrics_Document *document,
                                uint64_t id, Lyric_Lane_Click mode);

// Drops ids the document no longer holds. Deleting a cue through the editing
// form leaves a stale id behind, and lyrics_shift_many rejects the whole move
// when it sees one -- correctly, but the user would just see dragging stop
// working with no explanation.
void lyric_lane_selection_prune(Lyric_Lane_Selection *selection,
                                const Lyrics_Document *document);

// Largest part of the proposed move that lyrics_shift_many will accept, so the
// blocks track the pointer up to the end of the track and then stop, instead of
// following it and snapping back when the commit is rejected.
double lyric_lane_clamp_move(const Lyrics_Document *document,
                             const Lyric_Lane_Selection *selection,
                             double delta_seconds);

// Resolves an edge drag to the boundaries lyrics_retime should be given.
// Returns false when the cue is missing or the input is not finite.
bool lyric_lane_clamp_resize(const Lyrics_Document *document, uint64_t id,
                             bool moving_start, double proposed_seconds,
                             double *start_seconds, double *end_seconds);

#endif // MUSIALIZER_LYRIC_LANE_EDIT_H_
