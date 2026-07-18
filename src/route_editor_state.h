#ifndef MUSIALIZER_ROUTE_EDITOR_STATE_H_
#define MUSIALIZER_ROUTE_EDITOR_STATE_H_

#include <stdbool.h>
#include <stddef.h>

#include "scene_routes.h"

// Headless draft state for the Tune-inspector route editor. Editing happens
// on a draft copy so a half-finished route never touches the committed table;
// Apply commits the draft, Close discards it. A dirty draft participates in
// the application's close/context-change guards like other drafts.

// Minimum input-range width the editor maintains while either endpoint is
// dragged; scene_route_valid only demands input_max > input_min, this keeps
// the sliders from pinching the range into uselessness.
#define ROUTE_EDITOR_INPUT_GAP 0.01

typedef struct Route_Editor_State {
    bool open;
    // True when the edited setting already had a committed route at open (or
    // after Apply); `committed` then holds that route for dirty comparison.
    bool has_committed;
    // A fresh draft only guards once an edit actually changed a field.
    bool touched;
    size_t track_slot;
    size_t scene_index;
    size_t setting_index;
    Musi_Parameter_Mapping committed;
    Musi_Parameter_Mapping draft;
} Route_Editor_State;

void route_editor_init(Route_Editor_State *state);

// Opens a draft for one setting of one track's scene. With existing == NULL
// the draft starts as a full-range RMS route (input 0..1, output spanning the
// descriptor range, linear, clamped); a non-NULL existing must be a valid
// route for exactly this setting and becomes both draft and committed
// snapshot. Opening replaces any previous draft.
bool route_editor_open(Route_Editor_State *state, size_t track_slot,
                       size_t scene_index, size_t setting_index,
                       const Musi_Parameter_Mapping *existing);
void route_editor_close(Route_Editor_State *state);
bool route_editor_is_open(const Route_Editor_State *state);

// True when the open draft belongs to this exact setting / this context.
// The inspector uses these to decide which row hosts the editor and whether
// the draft survives a scene or track switch out of sight.
bool route_editor_targets(const Route_Editor_State *state, size_t track_slot,
                          size_t scene_index, size_t setting_index);
bool route_editor_matches_context(const Route_Editor_State *state,
                                  size_t track_slot, size_t scene_index);

// Draft mutations. Each returns false on misuse (closed editor, out-of-range
// enum, non-finite value) and true otherwise; values are clamped to keep the
// draft continuously valid, and `touched` is only set when a field actually
// changed. Switching away from the band source resets band_index to zero
// (schema rule); band stepping is clamped to the live band_limit.
bool route_editor_set_source(Route_Editor_State *state,
                             Musi_Analysis_Source source);
bool route_editor_step_band(Route_Editor_State *state, int delta,
                            size_t band_limit);
bool route_editor_set_input_min(Route_Editor_State *state, double value);
bool route_editor_set_input_max(Route_Editor_State *state, double value);
// Output endpoints are "value at input_min" / "value at input_max", clamped
// to the descriptor range and rounded for precision-0 settings. low > high
// is legal and means an inverted response; swap flips the direction.
bool route_editor_set_output_low(Route_Editor_State *state, double value);
bool route_editor_set_output_high(Route_Editor_State *state, double value);
bool route_editor_swap_output(Route_Editor_State *state);
bool route_editor_set_curve(Route_Editor_State *state,
                            Musi_Interpolation curve);
bool route_editor_set_clamp(Route_Editor_State *state, bool clamp);

// Dirty: an open draft that differs from its committed route, or a fresh
// draft the user actually edited. This is the guard predicate.
bool route_editor_dirty(const Route_Editor_State *state);
// Track-scoped form used by save/autosave and context-change policy. A hidden
// draft for another track must not make the active track claim its own file is
// dirty, while the owning track must never be reported as fully saved.
bool route_editor_dirty_for_track(const Route_Editor_State *state,
                                  size_t track_slot);
bool route_editor_can_apply(const Route_Editor_State *state);

// Commits the draft into the table, replacing a committed route for the same
// parameter in place. On success the draft becomes the committed snapshot and
// the editor stays open, clean. False (state and table unchanged) when the
// draft is invalid or a new route no longer fits the scene's capacity.
bool route_editor_apply(Route_Editor_State *state, Scene_Route_Table *table);

// Deletes the committed route from the table and closes the editor, returning
// the setting to its slider. False when nothing was removed.
bool route_editor_remove(Route_Editor_State *state, Scene_Route_Table *table);

// The committed route driving one setting, or NULL when the setting is a
// plain slider. Powers the per-row routed affordance.
const Musi_Parameter_Mapping *route_editor_find_route(
    const Scene_Route_Table *table, size_t scene_index, size_t setting_index);

// Display policy shared by the inspector and headless tests.
const char *route_editor_source_label(Musi_Analysis_Source source);
const char *route_editor_curve_label(Musi_Interpolation curve);
// A route is authored as two (source -> output) anchors. Anchor names follow
// the source so editor rows read as sentences; a generic "low/high" would be
// wrong for beat phase, whose axis is time inside the beat, not loudness.
const char *route_editor_anchor_label(Musi_Analysis_Source source, bool high);
// Compact row summary, e.g. "Band 2 · Smooth · 0.40 → 2.20" with an
// " · unclamped" suffix when clamping is off; precision comes from the
// setting descriptor so values read like the slider they replace.
void route_editor_summary(const Musi_Parameter_Mapping *route,
                          unsigned precision, char *buffer, size_t capacity);
// Where the live source value sits inside the route's input range, 0..1,
// for the meter strip; 0 when the value or range is unusable.
float route_editor_meter_position(const Musi_Parameter_Mapping *route,
                                  double source_value);

#endif // MUSIALIZER_ROUTE_EDITOR_STATE_H_
