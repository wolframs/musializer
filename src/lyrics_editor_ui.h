#ifndef MUSIALIZER_LYRICS_EDITOR_UI_H_
#define MUSIALIZER_LYRICS_EDITOR_UI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <raylib.h>

#include "editor_draft.h"
#include "font_catalogue.h"
#include "font_import_state.h"
#include "lyric_lane_edit.h"
#include "lyrics.h"
#include "timeline_view.h"
#include "track.h"
#include "ui_notice.h"
#include "ui_theme.h"
#include "ui_widgets.h"

// Mutable draft state for the lyrics content/sync editor. The canonical
// Lyrics_Document lives with each Track; these fields are only the current UI
// draft. Embedded in Plug so hot reload memcpy carries them (AGENTS.md).
typedef struct {
    bool text_active;
    bool draft_new;
    uint64_t selected_id;
    double draft_start;
    double draft_end;
    char draft_text[LYRICS_TEXT_CAPACITY];
    size_t list_first;
    bool list_follow_selection;
    // Which editor the right-hand pane shows: the selected cue, or the caption
    // typography that governs how every cue is rendered. They are never needed
    // at once and the panel has room for one.
    bool style_pane;
    // Slider being dragged in the style pane, 0 for none. Held here rather than
    // recomputed from the pointer so a drag that leaves the track keeps moving
    // the control it started on.
    uint8_t style_drag;

    // Direct manipulation in the timeline lane. The selection is what a drag
    // moves; selected_id above stays the single cue the editing form is bound
    // to, and the two are kept consistent by lane clicks.
    Lyric_Lane_Selection lane_selection;
    Lyric_Lane_Zone lane_drag_zone;
    uint64_t lane_drag_id;
    // Where the press landed, in seconds and in pixels. The pixel origin is
    // what decides a click from a drag; the seconds origin survives a zoom or
    // pan mid-gesture, which a pixel delta would not.
    double lane_drag_origin_seconds;
    float lane_drag_origin_x;
    // Clamped live so the blocks stop at the ends of the track instead of
    // following the pointer and snapping back when the commit is rejected.
    double lane_drag_delta_seconds;
    double lane_drag_edge_seconds;
    bool lane_drag_moved;

    // The caption font browser, which is a third whole-panel pane beside the
    // cue editor and the style form. It takes the panel for the same reason
    // the style form does: a searchable list of eighteen hundred families does
    // not fit beside a cue list, and a control that does not fit must not be
    // drawn.
    bool font_pane;
    char font_query[FONT_CATALOGUE_FAMILY_CAPACITY];
    bool font_query_active;
    size_t font_list_first;
    // An index into the catalogue, meaningful only while font_selection_valid.
    // A catalogue refresh can renumber every row, so the family name is
    // re-resolved rather than the index being trusted across a reload.
    size_t font_selected;
    bool font_selection_valid;
} Lyric_Editor;

// What the browser needs from the host to talk to the network boundary. All of
// it is optional: a build or a state without an importer leaves the pointer
// NULL and the pane simply is not offered.
typedef struct Font_Browser_Services {
    const Font_Catalogue *(*catalogue)(void);
    Font_Import_Panel (*panel)(void);
    // The last thing that happened, for the failure and progress lines. Never
    // NULL; empty when there is nothing to say.
    const char *(*status)(void);
    void (*allow_network)(void);
    void (*browse)(void);
    void (*fetch)(const char *family);
    void (*cancel)(void);
    // The family currently imported into this project, or NULL for none. This
    // is what makes the third face choice appear at all.
    const char *(*imported_family)(void);
    void (*clear_import)(void);
} Font_Browser_Services;

// Services the editor needs from the host. The host implements these as thin
// wrappers over its notice queue and project-dirty flag; the editor never
// touches Plug directly.
typedef struct {
    uint64_t (*notice_push)(Ui_Notice_Severity severity, const char *title,
                            const char *detail, const char *path, bool persistent);
    void (*mark_project_dirty)(Track *track);
    Font (*font)(void);
    uint64_t *active_button_id;
    // NULL when this build cannot import a face, which is a supported state:
    // the browser is simply not offered.
    const Font_Browser_Services *fonts;
} Lyric_Editor_Services;

void lyric_editor_ui_init(Lyric_Editor *editor);
void lyric_editor_ui_clear_draft(Lyric_Editor *editor);
bool lyric_editor_ui_has_unsaved_draft(const Lyric_Editor *editor, const Track *track);
bool lyric_editor_ui_allow_context_change(Lyric_Editor *editor, const Track *track,
                                          const Lyric_Editor_Services *services);
// Binds the editing form to a cue. The lane selection is left alone, because
// the lane applies its own ctrl/shift rules before calling this.
void lyric_editor_ui_select(Lyric_Editor *editor, Track *track, uint64_t id);
// Binds the form and makes that cue the entire lane selection. This is what
// every selection outside the lane -- the cue list, the probe -- should use.
void lyric_editor_ui_select_single(Lyric_Editor *editor, Track *track, uint64_t id);
void lyric_editor_ui_begin_new(Lyric_Editor *editor, Track *track);
bool lyric_editor_ui_apply(Lyric_Editor *editor, Track *track,
                           const Lyric_Editor_Services *services);
void lyric_editor_ui_text_input_update(Lyric_Editor *editor,
                                       const Lyric_Editor_Services *services);

// Gives back the lane's press claim if it still holds one while the mouse is
// up. The lane can only release during its own draw, so a track unloaded or a
// window shrunk past the timeline strip mid-drag would otherwise strand the id
// and freeze every button in the application. Call once per frame after the UI.
void lyric_editor_ui_release_lane_claim(Lyric_Editor *editor,
                                        const Lyric_Editor_Services *services);

void lyric_editor_ui_draw_lane(Lyric_Editor *editor, Track *track, float track_length,
                               const Timeline_View *view,
                               Rectangle lane, Font font,
                               bool editor_open_on_click,
                               const Lyric_Editor_Services *services);
void lyric_editor_ui_draw(Lyric_Editor *editor, Track *track, double playhead,
                          Rectangle boundary, const Lyric_Editor_Services *services);

#endif // MUSIALIZER_LYRICS_EDITOR_UI_H_
