#include "lyrics_editor_ui.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lyrics_editor_layout.h"
#include "scene.h"
#include "thirdparty/tinyfiledialogs.h"

#ifndef NOB_ARRAY_LEN
#define NOB_ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))
#endif

// Local widget wrappers that route through the services vtable.
static Font svc_font(const Lyric_Editor_Services *s) { return s->font(); }

static int text_button_sized(const Lyric_Editor_Services *s, uint64_t id,
                             Rectangle boundary, const char *label, bool selected,
                             float font_size)
{
    return ui_widgets_text_button_sized(s->active_button_id, svc_font(s), id,
                                        boundary, label, selected, font_size);
}

static int danger_text_button_sized(const Lyric_Editor_Services *s, uint64_t id,
                                    Rectangle boundary, const char *label,
                                    bool armed, float font_size)
{
    return ui_widgets_danger_text_button_sized(s->active_button_id, svc_font(s), id,
                                               boundary, label, armed, font_size);
}

// Shared label size for a row of buttons that should agree, so a long label does
// not leave its neighbours visibly larger than itself.
static float row_font_size(const Lyric_Editor_Services *s,
                           const char *const *labels, const float *widths,
                           size_t count, float box_height)
{
    return ui_widgets_row_font_size(svc_font(s), labels, widths, count, box_height);
}

static int button_with_id(const Lyric_Editor_Services *s, uint64_t id,
                          Rectangle boundary)
{
    return ui_widgets_button_with_id(s->active_button_id, id, boundary);
}

void lyric_editor_ui_init(Lyric_Editor *editor)
{
    if (editor == NULL) return;
    memset(editor, 0, sizeof(*editor));
    editor->list_follow_selection = true;
}

void lyric_editor_ui_clear_draft(Lyric_Editor *editor)
{
    editor->selected_id = 0;
    editor->draft_new = false;
    editor->text_active = false;
    editor->draft_start = 0.0;
    editor->draft_end = 0.0;
    editor->draft_text[0] = '\0';
}

bool lyric_editor_ui_has_unsaved_draft(const Lyric_Editor *editor, const Track *track)
{
    if (track == NULL) return false;
    const Lyric_Cue *cue = lyrics_find(&track->lyrics, editor->selected_id);
    Editor_Lyric_Draft_State state = {
        .is_new = editor->draft_new,
        .selected_id = editor->selected_id,
        .canonical_exists = cue != NULL,
        .canonical_start_seconds = cue != NULL ? cue->start_seconds : 0.0,
        .canonical_end_seconds = cue != NULL ? cue->end_seconds : 0.0,
        .canonical_text = cue != NULL ? cue->text : NULL,
        .draft_start_seconds = editor->draft_start,
        .draft_end_seconds = editor->draft_end,
        .draft_text = editor->draft_text,
    };
    return editor_lyric_draft_is_dirty(&state);
}

bool lyric_editor_ui_allow_context_change(Lyric_Editor *editor, const Track *track,
                                       const Lyric_Editor_Services *services)
{
    if (!lyric_editor_ui_has_unsaved_draft(editor, track)) return true;
    services->notice_push(UI_NOTICE_WARNING, "Finish the lyric edit first",
                "Apply or discard the current draft before changing tracks or panels.",
                NULL, false);
    return false;
}

void lyric_editor_ui_select_single(Lyric_Editor *editor, Track *track, uint64_t id)
{
    if (lyrics_find(&track->lyrics, id) == NULL) return;
    lyric_editor_ui_select(editor, track, id);
    // Selecting from the cue list, or from the probe, has to agree with the
    // lane about what is selected. Without this the lane draws the form's cue
    // with its "bound to the form" outline and the pale unselected fill at the
    // same time, and a drag would then move nothing.
    (void)lyric_lane_selection_apply(&editor->lane_selection, &track->lyrics, id,
                                     LYRIC_LANE_CLICK_REPLACE);
}

void lyric_editor_ui_select(Lyric_Editor *editor, Track *track, uint64_t id)
{
    const Lyric_Cue *cue = lyrics_find(&track->lyrics, id);
    if (cue == NULL) return;
    editor->selected_id = cue->id;
    editor->draft_new = false;
    editor->draft_start = cue->start_seconds;
    editor->draft_end = cue->end_seconds;
    snprintf(editor->draft_text, sizeof(editor->draft_text), "%s", cue->text);
    editor->list_follow_selection = true;
}

void lyric_editor_ui_begin_new(Lyric_Editor *editor, Track *track)
{
    double duration = track->lyrics.duration_seconds;
    double start = GetMusicTimePlayed(track->music);
    if (start + 0.05 > duration) start = fmax(0.0, duration - 2.0);
    editor->selected_id = 0;
    editor->draft_new = true;
    editor->draft_start = start;
    editor->draft_end = fmin(duration, start + 2.0);
    editor->draft_text[0] = '\0';
    editor->text_active = true;
}

bool lyric_editor_ui_apply(Lyric_Editor *editor, Track *track,
                        const Lyric_Editor_Services *services)
{
    Lyrics_Result result;
    if (editor->draft_new) {
        Lyric_Cue cue = {
            .start_seconds = editor->draft_start,
            .end_seconds = editor->draft_end,
        };
        snprintf(cue.text, sizeof(cue.text), "%s", editor->draft_text);
        uint64_t id = 0;
        result = lyrics_insert(&track->lyrics, &cue, &id);
        if (result == LYRICS_OK) lyric_editor_ui_select_single(editor, track, id);
    } else {
        result = lyrics_update(&track->lyrics, editor->selected_id,
                                editor->draft_start, editor->draft_end,
                                editor->draft_text);
        if (result == LYRICS_OK) {
            lyric_editor_ui_select(editor, track, editor->selected_id);
        }
    }
    if (result != LYRICS_OK) {
        TraceLog(LOG_WARNING, "LYRICS: could not apply edit: %s",
                 lyrics_result_string(result));
        services->notice_push(UI_NOTICE_ERROR, "Lyric edit was not applied",
                    lyrics_result_string(result), NULL, true);
        return false;
    }
    editor->text_active = false;
    services->mark_project_dirty(track);
    return true;
}

static void lyric_text_backspace(char *text)
{
    size_t length = strlen(text);
    if (length == 0) return;
    length -= 1;
    while (length > 0 && (((unsigned char)text[length] & 0xC0u) == 0x80u)) {
        length -= 1;
    }
    text[length] = '\0';
}

void lyric_editor_ui_text_input_update(Lyric_Editor *editor,
                                       const Lyric_Editor_Services *services)
{
    if (!editor->text_active) return;
    if (IsKeyPressed(KEY_BACKSPACE)) lyric_text_backspace(editor->draft_text);
    if (IsKeyPressed(KEY_ESCAPE)) editor->text_active = false;
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_V)) {
        const char *clipboard = GetClipboardText();
        // Appending is all-or-nothing, so a clipboard that is too long or holds
        // control characters leaves the draft exactly as it was and says why,
        // rather than silently landing a truncated or stripped version.
        bool flattened = false;
        Lyrics_Result pasted = clipboard != NULL ?
            lyrics_text_append(editor->draft_text, sizeof(editor->draft_text),
                               clipboard, &flattened) :
            LYRICS_ERROR_NULL;
        if (pasted != LYRICS_OK && services != NULL && services->notice_push != NULL) {
            services->notice_push(UI_NOTICE_WARNING, "Nothing was pasted",
                                  lyrics_result_string(pasted), NULL, false);
        } else if (flattened && services != NULL && services->notice_push != NULL) {
            services->notice_push(UI_NOTICE_INFO, "Pasted as one line",
                                  "A cue is a single line, so line breaks became "
                                  "spaces.", NULL, false);
        }
    }
    // A shortcut must not also type its own letter. Characters are drained
    // either way so a chord cannot leave one queued for the next frame.
    bool chord = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
        if (chord || codepoint < 0x20 || codepoint == 0x7F) continue;
        int encoded_size = 0;
        const char *encoded = CodepointToUTF8(codepoint, &encoded_size);
        size_t length = strlen(editor->draft_text);
        if (encoded != NULL && encoded_size > 0 &&
            length + (size_t)encoded_size < sizeof(editor->draft_text)) {
            memcpy(editor->draft_text + length, encoded, (size_t)encoded_size);
            editor->draft_text[length + (size_t)encoded_size] = '\0';
        }
    }
}

static void lyric_time_row(const Lyric_Editor_Services *s, Rectangle boundary,
                           const char *label, double *value,
                           double other, bool is_start, double playhead,
                           double duration, uint64_t id_base)
{
    const float gap = 4.0f;
    DrawTextEx(svc_font(s), label, (Vector2){boundary.x, boundary.y + 8.0f},
               16.0f, 1.0f, COLOR_UI_MUTED);
    char timestamp[32];
    ui_widgets_format_timestamp(*value, timestamp, sizeof(timestamp));
    DrawTextEx(svc_font(s), timestamp, (Vector2){boundary.x + 58.0f, boundary.y + 7.0f},
               18.0f, 1.0f, COLOR_UI_INK);
    Rectangle minus = {boundary.x + 154.0f, boundary.y, 42.0f, boundary.height};
    Rectangle plus = {minus.x + minus.width + gap, boundary.y, 42.0f, boundary.height};
    Rectangle set = {plus.x + plus.width + gap, boundary.y, 78.0f, boundary.height};
    const char *nudge_labels[3] = {"-0.1", "+0.1", "Set here"};
    const float nudge_widths[3] = {minus.width, plus.width, set.width};
    float nudge_font = row_font_size(s, nudge_labels, nudge_widths, 3, boundary.height);
    if (text_button_sized(s, id_base, minus, nudge_labels[0], false,
                          nudge_font) & BS_CLICKED) *value -= 0.1;
    if (text_button_sized(s, id_base + 1, plus, nudge_labels[1], false,
                          nudge_font) & BS_CLICKED) *value += 0.1;
    if (text_button_sized(s, id_base + 2, set, nudge_labels[2], false,
                          nudge_font) & BS_CLICKED) *value = playhead;
    if (is_start) {
        if (*value < 0.0) *value = 0.0;
        if (*value > other - 0.001) *value = other - 0.001;
    } else {
        if (*value < other + 0.001) *value = other + 0.001;
        if (*value > duration) *value = duration;
    }
}

// The lane owns the press for the whole gesture. It used to act on release
// only, which meant the timeline scrubber -- whose hit region covers the lane --
// claimed the press first and a drag inside the lane seeked the transport.
static const uint64_t LYRIC_LANE_GESTURE_ID = UINT64_C(0x4C59524943444147);

static Lyric_Lane_Click lane_click_mode(void)
{
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        return LYRIC_LANE_CLICK_TOGGLE;
    }
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        return LYRIC_LANE_CLICK_EXTEND;
    }
    return LYRIC_LANE_CLICK_REPLACE;
}

static void lane_reset_drag(Lyric_Editor *editor)
{
    editor->lane_drag_zone = LYRIC_LANE_ZONE_NONE;
    editor->lane_drag_id = 0;
    editor->lane_drag_origin_seconds = 0.0;
    editor->lane_drag_origin_x = 0.0f;
    editor->lane_drag_delta_seconds = 0.0;
    editor->lane_drag_edge_seconds = 0.0;
    editor->lane_drag_moved = false;
}

void lyric_editor_ui_release_lane_claim(Lyric_Editor *editor,
                                        const Lyric_Editor_Services *services)
{
    // The lane can only release during its own draw. A track unloaded, or a
    // window shrunk until the timeline strip is gone, would otherwise leave the
    // drag state set: plug.c's global fallback frees the id on mouse-up, but
    // nothing clears lane_drag_moved, so every block in the selection would go
    // on being drawn at its dragged offset for the rest of the session.
    // Abandoning rather than committing is deliberate -- a drag whose result
    // was never on screen must not be written to the project.
    bool ours = *services->active_button_id == LYRIC_LANE_GESTURE_ID;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ours) return;
    if (ours) *services->active_button_id = 0;
    lane_reset_drag(editor);
}

// Where a cue should be drawn right now: its stored timing, unless the current
// drag proposes something else. Preview and commit read the same clamped
// numbers, so what the blocks show during a drag is what gets written.
static void lane_preview_span(const Lyric_Editor *editor, const Lyric_Cue *cue,
                              double *start, double *end)
{
    *start = cue->start_seconds;
    *end = cue->end_seconds;
    if (!editor->lane_drag_moved) return;
    if (editor->lane_drag_zone == LYRIC_LANE_ZONE_BODY) {
        if (!lyric_lane_selection_contains(&editor->lane_selection, cue->id)) return;
        *start += editor->lane_drag_delta_seconds;
        *end += editor->lane_drag_delta_seconds;
    } else if (cue->id == editor->lane_drag_id) {
        if (editor->lane_drag_zone == LYRIC_LANE_ZONE_START_EDGE) {
            *start = editor->lane_drag_edge_seconds;
        } else if (editor->lane_drag_zone == LYRIC_LANE_ZONE_END_EDGE) {
            *end = editor->lane_drag_edge_seconds;
        }
    }
}

static void lane_commit_drag(Lyric_Editor *editor, Track *track,
                             const Lyric_Editor_Services *services)
{
    Lyrics_Result result = LYRICS_OK;
    if (editor->lane_drag_zone == LYRIC_LANE_ZONE_BODY) {
        if (editor->lane_drag_delta_seconds == 0.0) return;
        result = lyrics_shift_many(&track->lyrics, editor->lane_selection.ids,
                                   editor->lane_selection.count,
                                   editor->lane_drag_delta_seconds);
    } else {
        const Lyric_Cue *cue = lyrics_find(&track->lyrics, editor->lane_drag_id);
        if (cue == NULL) return;
        double start = cue->start_seconds;
        double end = cue->end_seconds;
        if (editor->lane_drag_zone == LYRIC_LANE_ZONE_START_EDGE) {
            start = editor->lane_drag_edge_seconds;
        } else {
            end = editor->lane_drag_edge_seconds;
        }
        if (start == cue->start_seconds && end == cue->end_seconds) return;
        result = lyrics_retime(&track->lyrics, editor->lane_drag_id, start, end);
    }
    if (result != LYRICS_OK) {
        services->notice_push(UI_NOTICE_ERROR, "The cues were not moved",
                              lyrics_result_string(result), NULL, false);
        return;
    }
    // The editing form holds a copy of the cue it is bound to. Leaving it
    // behind would make an untouched form read as a dirty draft and block
    // every panel change until the user discarded an edit they never made.
    if (editor->selected_id != 0) {
        lyric_editor_ui_select(editor, track, editor->selected_id);
    }
    services->mark_project_dirty(track);
}

static void lane_gesture_update(Lyric_Editor *editor, Track *track,
                                float track_length, const Timeline_View *view,
                                Rectangle lane,
                                const Lyric_Editor_Services *services)
{
    uint64_t *active = services->active_button_id;
    Vector2 mouse = GetMousePosition();

    if (*active == 0 && CheckCollisionPointRec(mouse, lane) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Lyric_Lane_Hit hit = lyric_lane_hit_test(&track->lyrics, view, lane.x,
                                                 lane.width, mouse.x);
        // Claim regardless of what was hit: an empty-lane press inside the lane
        // must not fall through to the scrubber, or clearing a selection would
        // also seek the transport.
        *active = LYRIC_LANE_GESTURE_ID;
        lane_reset_drag(editor);
        editor->lane_drag_origin_x = mouse.x;
        editor->lane_drag_origin_seconds = timeline_view_seconds_at(
            view, mouse.x, lane.x, lane.width, (double)track_length);

        if (hit.id == 0) {
            lyric_lane_selection_clear(&editor->lane_selection);
            return;
        }
        // A press that would rebind the editing form goes through the same
        // unsaved-draft guard as every other context change.
        if (hit.id != editor->selected_id &&
            !lyric_editor_ui_allow_context_change(editor, track, services)) {
            return;
        }
        editor->lane_drag_zone = hit.zone;
        editor->lane_drag_id = hit.id;

        Lyric_Lane_Click mode = lane_click_mode();
        bool already = lyric_lane_selection_contains(&editor->lane_selection, hit.id);
        // A plain press on a block that is already part of the selection keeps
        // the selection so the whole set can be dragged; the collapse to one
        // cue happens on release, and only if nothing was dragged.
        if (!(mode == LYRIC_LANE_CLICK_REPLACE && already)) {
            if (!lyric_lane_selection_apply(&editor->lane_selection, &track->lyrics,
                                            hit.id, mode)) {
                services->notice_push(UI_NOTICE_WARNING, "That range is too large",
                                      "A lane selection holds at most 64 cues.",
                                      NULL, false);
                lane_reset_drag(editor);
                return;
            }
        }
        lyric_editor_ui_select(editor, track, hit.id);
        return;
    }

    if (*active != LYRIC_LANE_GESTURE_ID) return;

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (editor->lane_drag_zone == LYRIC_LANE_ZONE_NONE) return;
        if (fabsf(mouse.x - editor->lane_drag_origin_x) >=
            (float)LYRIC_LANE_DRAG_THRESHOLD_PIXELS) {
            editor->lane_drag_moved = true;
        }
        if (!editor->lane_drag_moved) return;
        double pointer = timeline_view_seconds_at(view, mouse.x, lane.x, lane.width,
                                                  (double)track_length);
        if (editor->lane_drag_zone == LYRIC_LANE_ZONE_BODY) {
            editor->lane_drag_delta_seconds = lyric_lane_clamp_move(
                &track->lyrics, &editor->lane_selection,
                pointer - editor->lane_drag_origin_seconds);
        } else {
            double start = 0.0;
            double end = 0.0;
            if (lyric_lane_clamp_resize(&track->lyrics, editor->lane_drag_id,
                                        editor->lane_drag_zone ==
                                            LYRIC_LANE_ZONE_START_EDGE,
                                        pointer, &start, &end)) {
                editor->lane_drag_edge_seconds =
                    editor->lane_drag_zone == LYRIC_LANE_ZONE_START_EDGE ? start : end;
            }
        }
        return;
    }

    // Released -- or the button was let go while the window was unfocused and
    // no release edge ever arrived. Either way the claim is given back here,
    // unconditionally: ui_widgets only frees an id through its owning widget,
    // so a stranded claim freezes every button in the application (AGENTS.md).
    if (editor->lane_drag_moved) {
        lane_commit_drag(editor, track, services);
    } else if (editor->lane_drag_id != 0 &&
               lane_click_mode() == LYRIC_LANE_CLICK_REPLACE) {
        (void)lyric_lane_selection_apply(&editor->lane_selection, &track->lyrics,
                                         editor->lane_drag_id,
                                         LYRIC_LANE_CLICK_REPLACE);
    }
    lane_reset_drag(editor);
    *active = 0;
}

// Height the caption controls need below the pane header. Checked before any
// of them is drawn, so a short panel says so instead of painting sliders
// through the bottom edge -- the defect this panel already shipped once.
#define CAPTION_STYLE_FORM_HEIGHT 152.0f

// A plain slider. plug.c's horz_slider needs its circle shader, which this
// module deliberately has no access to, and the caption controls do not need a
// soft handle to be usable.
static bool caption_slider(Lyric_Editor *editor, uint8_t id, Rectangle track_rect,
                           double minimum, double maximum, double *value)
{
    float radius = track_rect.height*0.5f;
    float left = track_rect.x + radius;
    float span = track_rect.width - radius*2.0f;
    if (span <= 0.0) return false;
    double range = maximum - minimum;
    if (!(range > 0.0)) return false;
    double fraction = (*value - minimum)/range;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    DrawLineEx((Vector2){left, track_rect.y + radius},
               (Vector2){left + span, track_rect.y + radius},
               2.0f, COLOR_UI_RULE);
    float handle_x = left + (float)fraction*span;
    DrawLineEx((Vector2){left, track_rect.y + radius},
               (Vector2){handle_x, track_rect.y + radius}, 2.0f, COLOR_ACCENT);
    DrawCircleV((Vector2){handle_x, track_rect.y + radius}, radius*0.62f, COLOR_ACCENT);

    Vector2 mouse = GetMousePosition();
    if (editor->style_drag == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, track_rect)) {
        editor->style_drag = id;
    }
    if (editor->style_drag != id) return false;
    // Released, or the button went up while the window was unfocused. This
    // widget owns no active_button_id, so nothing can be stranded here, but the
    // drag must still end on its own.
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        editor->style_drag = 0;
        return false;
    }
    double moved = minimum + (double)((mouse.x - left)/span)*range;
    if (moved < minimum) moved = minimum;
    if (moved > maximum) moved = maximum;
    if (moved == *value) return false;
    *value = moved;
    return true;
}

// One row of mutually exclusive choices. Returns the newly chosen index, or -1.
static int caption_choice_row(const Lyric_Editor_Services *services, uint64_t id,
                              Rectangle row, const char *const *labels,
                              size_t count, int current)
{
    if (count == 0 || row.width <= 0.0f) return -1;
    int chosen = -1;
    float gap = 4.0f;
    float width = (row.width - gap*(float)(count - 1u))/(float)count;
    if (width < 24.0f) return -1;
    float font_size = ui_widgets_row_font_size(svc_font(services), labels, NULL,
                                               count, row.height);
    for (size_t i = 0; i < count; ++i) {
        Rectangle box = {row.x + (float)i*(width + gap), row.y, width, row.height};
        if (text_button_sized(services, id + i, box, labels[i],
                              (int)i == current, font_size) & BS_CLICKED) {
            chosen = (int)i;
        }
    }
    return chosen;
}

static void caption_label(const Lyric_Editor_Services *services, const char *text,
                          float x, float y, float height)
{
    Vector2 size = MeasureTextEx(svc_font(services), text, 12.0f, 1.0f);
    DrawTextEx(svc_font(services), text, (Vector2){x, y + (height - size.y)*0.5f},
               12.0f, 1.0f, COLOR_UI_MUTED);
}

// Swatches, not a colour wheel. A caption has to stay legible over moving
// material, so the useful choices are few and opinionated; the alpha is baked
// into each entry because "white at 40%" is a different decision from "grey".
static const uint32_t CAPTION_TEXT_SWATCHES[6] = {
    0xFFFFFFFFu, 0xF2BE42FFu, 0x7FB2FFFFu, 0x9BE8B0FFu, 0x1A1A1EFFu, 0xFFFFFFC0u,
};
static const uint32_t CAPTION_BOX_SWATCHES[5] = {
    0x000000B7u, 0x00000066u, 0x000000FFu, 0xFFFFFFCCu, 0x1B2A5AC0u,
};

static void caption_swatch_row(const Lyric_Editor_Services *services, uint64_t id,
                               Rectangle row, const uint32_t *swatches,
                               size_t count, uint32_t current, uint32_t *value,
                               bool *changed)
{
    float gap = 4.0f;
    float width = (row.width - gap*(float)(count - 1u))/(float)count;
    if (width < 12.0f) return;
    for (size_t i = 0; i < count; ++i) {
        Rectangle box = {row.x + (float)i*(width + gap), row.y, width, row.height};
        // A checkerboard behind every swatch, so a translucent choice reads as
        // translucent rather than as a slightly different flat colour.
        for (int cell = 0; cell < 8; ++cell) {
            float cw = box.width*0.25f;
            float ch = box.height*0.5f;
            Rectangle tile = {box.x + (float)(cell%4)*cw,
                              box.y + (float)(cell/4)*ch, cw, ch};
            DrawRectangleRec(tile, ((cell%4) + (cell/4))%2 == 0 ?
                             COLOR_UI_RAISED : COLOR_UI_SURFACE);
        }
        Color color = {
            (unsigned char)((swatches[i] >> 24) & 0xFFu),
            (unsigned char)((swatches[i] >> 16) & 0xFFu),
            (unsigned char)((swatches[i] >> 8) & 0xFFu),
            (unsigned char)(swatches[i] & 0xFFu),
        };
        DrawRectangleRec(box, color);
        bool selected = swatches[i] == current;
        DrawRectangleLinesEx(box, selected ? 2.0f : 1.0f,
                             selected ? COLOR_ACCENT : COLOR_UI_RULE);
        if (button_with_id(services, id + i, box) & BS_CLICKED) {
            *value = swatches[i];
            *changed = true;
        }
    }
}

// Smallest pane that can hold a search field, three rows, and the action row.
// Below this the browser is not drawn at all, because a list you cannot read is
// worse than a sentence saying why. Measured against the panel the 960x640
// minimum window actually produces, not guessed: the style form next door spent
// a release refusing to draw at every size because its threshold was a guess.
#define FONT_BROWSER_MIN_HEIGHT 150.0f
#define FONT_BROWSER_MIN_WIDTH 420.0f
#define FONT_BROWSER_ROW_HEIGHT 26.0f

static void font_query_backspace(char *text)
{
    size_t length = strlen(text);
    while (length > 0) {
        --length;
        // Step back over UTF-8 continuation bytes so one press deletes one
        // character rather than half of one.
        if (((unsigned char)text[length] & 0xC0) != 0x80) break;
    }
    text[length] = '\0';
}

static void font_query_input(Lyric_Editor *editor)
{
    if (!editor->font_query_active) return;
    if (IsKeyPressed(KEY_BACKSPACE)) font_query_backspace(editor->font_query);
    if (IsKeyPressed(KEY_ESCAPE)) editor->font_query_active = false;
    bool chord = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
        // A family name is ASCII by the helper's own validation, so anything
        // else could never match and is simply not accepted into the query.
        if (chord || codepoint < 0x20 || codepoint > 0x7E) continue;
        size_t length = strlen(editor->font_query);
        if (length + 1 < sizeof(editor->font_query)) {
            editor->font_query[length] = (char)codepoint;
            editor->font_query[length + 1] = '\0';
        }
    }
}

static void draw_font_consent(Rectangle form,
                              const Lyric_Editor_Services *services)
{
    const Font_Browser_Services *fonts = services->fonts;
    DrawTextEx(svc_font(services), "Import a caption face from Google Fonts",
               (Vector2){form.x, form.y}, 16.0f, 1.0f, COLOR_UI_INK);
    // The consequence, stated before the button that causes it. What is sent
    // is genuinely small, and saying so is more useful than a vague warning.
    static const char *const lines[] = {
        "Musializer will contact fonts.google.com and fonts.gstatic.com to list",
        "and download faces, and raw.githubusercontent.com for the licence each",
        "face is distributed under.",
        "",
        "Only a family name is sent. No audio, lyrics, or project data leaves",
        "this machine. Musializer asks again next time it starts.",
    };
    // The button is placed from the bottom and the explanation fills what is
    // left above it. A consent panel that keeps its prose and loses its button
    // is a question with no way to answer it.
    Rectangle allow = {form.x, form.y + form.height - 30.0f, 200.0f, 30.0f};
    for (size_t i = 0; i < sizeof(lines)/sizeof(lines[0]); ++i) {
        float y = form.y + 26.0f + (float)i*17.0f;
        if (y + 17.0f > allow.y - 6.0f) break;
        DrawTextEx(svc_font(services), lines[i], (Vector2){form.x, y}, 13.0f, 1.0f,
                   COLOR_UI_MUTED);
    }
    if (text_button_sized(services, UINT64_C(0x464F4E54434E5354), allow,
                          "Allow and browse fonts", false, 14.0f) & BS_CLICKED) {
        if (fonts->allow_network != NULL) fonts->allow_network();
        if (fonts->browse != NULL) fonts->browse();
    }
}

static void draw_font_browser(Lyric_Editor *editor, Rectangle form,
                              const Lyric_Editor_Services *services)
{
    const Font_Browser_Services *fonts = services->fonts;
    if (form.height < FONT_BROWSER_MIN_HEIGHT || form.width < FONT_BROWSER_MIN_WIDTH) {
        DrawTextEx(svc_font(services), "Enlarge the window to browse caption faces.",
                   (Vector2){form.x, form.y}, 15.0f, 1.0f, COLOR_UI_MUTED);
        return;
    }
    Font_Import_Panel panel = fonts->panel != NULL ? fonts->panel()
                                                   : FONT_IMPORT_PANEL_CONSENT;
    const char *status = fonts->status != NULL ? fonts->status() : "";
    if (panel == FONT_IMPORT_PANEL_CONSENT) {
        draw_font_consent(form, services);
        return;
    }
    if (panel == FONT_IMPORT_PANEL_LOADING || panel == FONT_IMPORT_PANEL_FETCHING ||
        panel == FONT_IMPORT_PANEL_CANCELLING) {
        static const char *const headings[] = {
            [FONT_IMPORT_PANEL_LOADING]    = "Fetching the family list...",
            [FONT_IMPORT_PANEL_FETCHING]   = "Downloading the face...",
            [FONT_IMPORT_PANEL_CANCELLING] = "Stopping...",
        };
        DrawTextEx(svc_font(services), headings[panel],
                   (Vector2){form.x, form.y}, 16.0f, 1.0f, COLOR_UI_INK);
        if (status[0] != '\0') {
            DrawTextEx(svc_font(services), status,
                       (Vector2){form.x, form.y + 24.0f}, 13.0f, 1.0f, COLOR_UI_MUTED);
        }
        Rectangle cancel = {form.x, form.y + 52.0f, 110.0f, 30.0f};
        // A cancel that is already in flight must not be offered again: the
        // second press would claim the id and do nothing visible.
        if (panel != FONT_IMPORT_PANEL_CANCELLING &&
            cancel.y + cancel.height <= form.y + form.height &&
            (text_button_sized(services, UINT64_C(0x464F4E5443414E43), cancel,
                               "Cancel", false, 14.0f) & BS_CLICKED)) {
            if (fonts->cancel != NULL) fonts->cancel();
        }
        return;
    }
    if (panel == FONT_IMPORT_PANEL_FAILED) {
        DrawTextEx(svc_font(services), "The font request did not finish",
                   (Vector2){form.x, form.y}, 16.0f, 1.0f, COLOR_UI_INK);
        DrawTextEx(svc_font(services), status[0] != '\0' ? status :
                   "No further detail was reported.",
                   (Vector2){form.x, form.y + 24.0f}, 13.0f, 1.0f, COLOR_UI_MUTED);
        Rectangle retry = {form.x, form.y + 52.0f, 110.0f, 30.0f};
        if (retry.y + retry.height <= form.y + form.height &&
            (text_button_sized(services, UINT64_C(0x464F4E5452455452), retry,
                               "Try again", false, 14.0f) & BS_CLICKED)) {
            if (fonts->browse != NULL) fonts->browse();
        }
        return;
    }

    const Font_Catalogue *catalogue = fonts->catalogue != NULL ? fonts->catalogue()
                                                               : NULL;
    if (catalogue == NULL || catalogue->count == 0) {
        DrawTextEx(svc_font(services), "No families are loaded.",
                   (Vector2){form.x, form.y}, 15.0f, 1.0f, COLOR_UI_MUTED);
        return;
    }

    Rectangle search = {form.x, form.y, form.width - 160.0f, 28.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        editor->font_query_active =
            CheckCollisionPointRec(GetMousePosition(), search);
    }
    DrawRectangleRec(search, COLOR_UI_RAISED);
    DrawRectangleLinesEx(search, editor->font_query_active ? 2.0f : 1.0f,
                         editor->font_query_active ? COLOR_ACCENT : COLOR_UI_RULE);
    const char *shown = editor->font_query[0] != '\0' ? editor->font_query
                                                      : "Search families";
    DrawTextEx(svc_font(services), shown,
               (Vector2){search.x + 8.0f, search.y + 6.0f}, 15.0f, 1.0f,
               editor->font_query[0] != '\0' ? COLOR_UI_INK : COLOR_UI_MUTED);
    font_query_input(editor);

    size_t indices[64];
    size_t written = 0;
    size_t matched = font_catalogue_filter(catalogue, editor->font_query, 0,
                                           NULL, 0, NULL);

    // The list gets whatever is left after the search field and the action row.
    float list_top = form.y + 36.0f;
    float list_bottom = form.y + form.height - 38.0f;
    size_t visible = list_bottom > list_top ?
                     (size_t)((list_bottom - list_top)/FONT_BROWSER_ROW_HEIGHT) : 0;
    if (visible > sizeof(indices)/sizeof(indices[0])) {
        visible = sizeof(indices)/sizeof(indices[0]);
    }
    size_t first = editor->font_list_first;
    if (CheckCollisionPointRec(GetMousePosition(),
            (Rectangle){form.x, list_top, form.width, list_bottom - list_top})) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            int64_t moved = (int64_t)first - (int64_t)(wheel*3.0f);
            first = moved < 0 ? 0 : (size_t)moved;
        }
    }
    if (first + visible > matched) first = matched > visible ? matched - visible : 0;
    editor->font_list_first = first;

    // Only the visible window is collected, so scrolling a catalogue of
    // eighteen hundred families never walks more than a screenful.
    written = 0;
    if (visible > 0) {
        size_t skipped = 0;
        size_t stored = 0;
        for (size_t i = 0; i < catalogue->count && stored < visible; ++i) {
            const Font_Catalogue_Entry *entry = &catalogue->entries[i];
            if (!font_catalogue_entry_matches(entry, editor->font_query)) continue;
            if (skipped < first) { ++skipped; continue; }
            indices[stored++] = i;
        }
        written = stored;
    }

    for (size_t row = 0; row < written; ++row) {
        const Font_Catalogue_Entry *entry = &catalogue->entries[indices[row]];
        Rectangle row_boundary = {
            form.x, list_top + (float)row*FONT_BROWSER_ROW_HEIGHT,
            form.width, FONT_BROWSER_ROW_HEIGHT - 2.0f,
        };
        bool selected = editor->font_selection_valid &&
                        editor->font_selected == indices[row];
        int state = button_with_id(services,
                                   UINT64_C(0x464F4E5452000000) + indices[row],
                                   row_boundary);
        Color background = selected ? GetColor(0xE7ECFAFF) : COLOR_UI_RAISED;
        if (state & BS_HOVEROVER) background = COLOR_TRACK_BUTTON_HOVEROVER;
        DrawRectangleRec(row_boundary, background);
        DrawTextEx(svc_font(services), entry->family,
                   (Vector2){row_boundary.x + 8.0f, row_boundary.y + 4.0f},
                   15.0f, 1.0f, COLOR_UI_INK);
        char coverage[64];
        font_scripts_describe(entry->scripts, coverage, sizeof(coverage));
        char note[128];
        snprintf(note, sizeof(note), "%s  -  %s", font_category_name(entry->category),
                 coverage);
        Vector2 note_size = MeasureTextEx(svc_font(services), note, 12.0f, 1.0f);
        // Drawn only where it fits, so a narrow pane loses the note rather
        // than printing it through the family name.
        float note_x = row_boundary.x + row_boundary.width - note_size.x - 8.0f;
        if (note_x > row_boundary.x + 160.0f) {
            DrawTextEx(svc_font(services), note,
                       (Vector2){note_x, row_boundary.y + 6.0f}, 12.0f, 1.0f,
                       COLOR_UI_MUTED);
        }
        if (state & BS_CLICKED) {
            editor->font_selected = indices[row];
            editor->font_selection_valid = true;
        }
    }

    char count[64];
    snprintf(count, sizeof(count), "%zu of %zu", written, matched);
    Vector2 count_size = MeasureTextEx(svc_font(services), count, 13.0f, 1.0f);
    DrawTextEx(svc_font(services), count,
               (Vector2){form.x + form.width - count_size.x, form.y + 8.0f},
               13.0f, 1.0f, COLOR_UI_MUTED);

    Rectangle action = {form.x, form.y + form.height - 32.0f, 150.0f, 30.0f};
    bool can_import = editor->font_selection_valid &&
                      editor->font_selected < catalogue->count;
    if (text_button_sized(services, UINT64_C(0x464F4E54494D5054), action,
                          "Download and use", false, 14.0f) & BS_CLICKED) {
        if (can_import && fonts->fetch != NULL) {
            fonts->fetch(catalogue->entries[editor->font_selected].family);
        }
    }
    if (!can_import) {
        DrawTextEx(svc_font(services), "Choose a family first.",
                   (Vector2){action.x + action.width + 10.0f, action.y + 8.0f},
                   13.0f, 1.0f, COLOR_UI_MUTED);
    } else {
        DrawTextEx(svc_font(services),
                   catalogue->entries[editor->font_selected].family,
                   (Vector2){action.x + action.width + 10.0f, action.y + 8.0f},
                   13.0f, 1.0f, COLOR_UI_INK);
    }
}

static void draw_caption_style_form(Lyric_Editor *editor, Track *track,
                                    Rectangle form,
                                    const Lyric_Editor_Services *services)
{
    Musi_Caption_Style *style = &track->caption_style;
    if (form.height < CAPTION_STYLE_FORM_HEIGHT || form.width < 520.0f) {
        DrawTextEx(svc_font(services), "Enlarge the window to edit caption style.",
                   (Vector2){form.x, form.y}, 15.0f, 1.0f, COLOR_UI_MUTED);
        return;
    }
    // Two columns across the whole panel. The cue list is hidden while this
    // pane is open, which is what buys the width: stacking these controls into
    // the 48% form column needed 208 px of height that the panel does not have.
    const float column_gap = 24.0f;
    const float label_width = 62.0f;
    float column_width = (form.width - column_gap)*0.5f;
    float left_x = form.x + label_width;
    float left_width = column_width - label_width;
    float right_label_x = form.x + column_width + column_gap;
    float right_x = right_label_x + label_width;
    float right_width = column_width - label_width;
    bool changed = false;

    // The imported face is offered only when the project actually carries one.
    // A third choice that selects a face the file does not have would be a
    // control whose only outcome is a validation failure on save.
    const Font_Browser_Services *fonts = services->fonts;
    const char *imported = fonts != NULL && fonts->imported_family != NULL ?
                           fonts->imported_family() : NULL;
    const char *face_labels[3] = {"Alegreya", "Space Grotesk", imported};
    size_t face_choices = imported != NULL ? 3u : 2u;
    Rectangle face_row = {left_x, form.y, left_width, 26.0f};
    caption_label(services, "FACE", form.x, face_row.y, face_row.height);
    int face = caption_choice_row(services, UINT64_C(0x4341504641434500), face_row,
                                  face_labels, face_choices, (int)style->face);
    if (face >= 0 && (Musi_Caption_Face)face != style->face) {
        style->face = (Musi_Caption_Face)face;
        changed = true;
    }

    static const char *const box_labels[3] = {"None", "Shadow", "Plate"};
    Rectangle box_row = {left_x, form.y + 32.0f, left_width, 26.0f};
    caption_label(services, "BACKING", form.x, box_row.y, box_row.height);
    int box = caption_choice_row(services, UINT64_C(0x4341504242475200), box_row,
                                 box_labels, 3, (int)style->box);
    if (box >= 0 && (Musi_Caption_Box)box != style->box) {
        style->box = (Musi_Caption_Box)box;
        changed = true;
    }

    // The anchor grid mirrors the frame: the top-left cell puts captions in the
    // top-left of the video. The choice is spatial, and a row of nine names
    // would be worse than nine 22 px cells.
    caption_label(services, "PLACE", form.x, form.y + 64.0f, 66.0f);
    for (int cell = 0; cell < 9; ++cell) {
        // The enum counts up from the bottom row because bottom-centre is the
        // default and belongs at zero; the grid draws downward from the top.
        int drawn_row = cell/3;
        int value = (2 - drawn_row)*3 + cell%3;
        Rectangle cell_rect = {left_x + (float)(cell%3)*24.0f,
                               form.y + 64.0f + (float)drawn_row*24.0f, 22.0f, 22.0f};
        bool selected = (int)style->anchor == value;
        DrawRectangleRec(cell_rect, selected ? COLOR_ACCENT : COLOR_UI_RAISED);
        DrawRectangleLinesEx(cell_rect, 1.0f, COLOR_UI_RULE);
        if (button_with_id(services, UINT64_C(0x4341504150430000) + (unsigned)cell,
                           cell_rect) & BS_CLICKED) {
            style->anchor = (Musi_Caption_Anchor)value;
            changed = true;
        }
    }
    DrawTextEx(svc_font(services),
               "Sizes are fractions of the frame, so exports match the preview.",
               (Vector2){form.x, form.y + 140.0f}, 12.0f, 1.0f, COLOR_UI_MUTED);

    // Reaching the browser from the face row is what makes the third choice
    // obtainable. Offered only when the host can actually import one.
    if (fonts != NULL) {
        Rectangle import_face = {left_x, form.y + 158.0f, 132.0f, 26.0f};
        Rectangle forget_face = {import_face.x + import_face.width + 6.0f,
                                 import_face.y, 74.0f, 26.0f};
        bool room = import_face.y + import_face.height <= form.y + form.height;
        if (room && (text_button_sized(services, UINT64_C(0x464F4E544F50454E),
                                       import_face, "Import a face...", false,
                                       13.0f) & BS_CLICKED)) {
            editor->font_pane = true;
        }
        if (room && imported != NULL &&
            forget_face.x + forget_face.width <= form.x + column_width &&
            (text_button_sized(services, UINT64_C(0x464F4E5446474554), forget_face,
                               "Remove", false, 13.0f) & BS_CLICKED)) {
            // Dropping the asset must drop the face that named it in the same
            // step, or the project would validate as an imported face with
            // nothing behind it.
            style->face = MUSI_CAPTION_FACE_ALEGREYA;
            if (fonts->clear_import != NULL) fonts->clear_import();
            changed = true;
        }
    }

    const float readout_width = 44.0f;
    float slider_width = right_width - readout_width - 6.0f;
    struct {
        const char *label;
        double *value;
        double minimum;
        double maximum;
        uint8_t id;
    } sliders[3] = {
        {"SIZE", &style->size_scale, MUSI_CAPTION_SIZE_MINIMUM,
         MUSI_CAPTION_SIZE_MAXIMUM, 1},
        {"WIDTH", &style->width_scale, MUSI_CAPTION_WIDTH_MINIMUM,
         MUSI_CAPTION_WIDTH_MAXIMUM, 2},
        {"INSET", &style->margin_scale, MUSI_CAPTION_MARGIN_MINIMUM,
         MUSI_CAPTION_MARGIN_MAXIMUM, 3},
    };
    for (size_t i = 0; i < 3; ++i) {
        float row_y = form.y + (float)i*26.0f;
        caption_label(services, sliders[i].label, right_label_x, row_y, 22.0f);
        if (slider_width < 60.0f) continue;
        Rectangle bar = {right_x, row_y, slider_width, 22.0f};
        if (caption_slider(editor, sliders[i].id, bar, sliders[i].minimum,
                           sliders[i].maximum, sliders[i].value)) {
            changed = true;
        }
        char readout[16];
        snprintf(readout, sizeof(readout), "%.1f%%", *sliders[i].value*100.0);
        Vector2 size = MeasureTextEx(svc_font(services), readout, 12.0f, 1.0f);
        DrawTextEx(svc_font(services), readout,
                   (Vector2){right_x + right_width - size.x,
                             row_y + (22.0f - size.y)*0.5f},
                   12.0f, 1.0f, COLOR_UI_INK);
    }

    Rectangle text_row = {right_x, form.y + 86.0f, right_width, 22.0f};
    caption_label(services, "INK", right_label_x, text_row.y, text_row.height);
    caption_swatch_row(services, UINT64_C(0x4341504954585400), text_row,
                       CAPTION_TEXT_SWATCHES, 6, style->text_rgba,
                       &style->text_rgba, &changed);

    Rectangle plate_row = {right_x, form.y + 114.0f, right_width, 22.0f};
    caption_label(services, "PLATE", right_label_x, plate_row.y, plate_row.height);
    // Kept live even with backing "None": switching back should restore the
    // colour that was chosen rather than reset it.
    caption_swatch_row(services, UINT64_C(0x43415042504C4100), plate_row,
                       CAPTION_BOX_SWATCHES, 5, style->box_rgba,
                       &style->box_rgba, &changed);

    if (changed) services->mark_project_dirty(track);
}

void lyric_editor_ui_draw_lane(Lyric_Editor *editor, Track *track, float track_length,
                            const Timeline_View *view,
                            Rectangle lane, Font font,
                            bool editor_open_on_click,
                            const Lyric_Editor_Services *services)
{
    // A cue deleted through the form leaves a stale id behind, and
    // lyrics_shift_many rejects the whole move when it sees one -- correctly,
    // but the user would only see dragging stop working.
    lyric_lane_selection_prune(&editor->lane_selection, &track->lyrics);
    lane_gesture_update(editor, track, track_length, view, lane, services);

    DrawRectangleRec(lane, COLOR_UI_RAISED);
    DrawLineEx((Vector2){lane.x, lane.y}, (Vector2){lane.x + lane.width, lane.y},
               1.0f, COLOR_UI_RULE);
    const Color lyric_color = (Color){242, 190, 66, 255};
    for (size_t i = 0; i < track->scene_switches.count; ++i) {
        const Scene_Switch_Cue *cue = &track->scene_switches.cues[i];
        float x = (float)timeline_view_x_at(view, cue->start_seconds, lane.x, lane.width);
        if (x < lane.x || x > lane.x + lane.width) continue;
        DrawLineEx((Vector2){x, lane.y}, (Vector2){x, lane.y + lane.height},
                   1.0f + cue->strength*2.0f, ColorAlpha((Color){0, 230, 118, 255}, 0.58f));
    }
    Lyric_Lane_Hit hover = *services->active_button_id == 0 ?
        lyric_lane_hit_test(&track->lyrics, view, lane.x, lane.width,
                            GetMousePosition().x) :
        (Lyric_Lane_Hit){LYRIC_LANE_ZONE_NONE, 0};
    (void)editor_open_on_click;

    for (size_t i = 0; i < track->lyrics.count; ++i) {
        const Lyric_Cue *cue = &track->lyrics.cues[i];
        double preview_start = 0.0;
        double preview_end = 0.0;
        lane_preview_span(editor, cue, &preview_start, &preview_end);
        float left = (float)timeline_view_x_at(view, preview_start, lane.x, lane.width);
        float right = (float)timeline_view_x_at(view, preview_end, lane.x, lane.width);
        if (right < lane.x || left > lane.x + lane.width) continue;
        if (right - left < 3.0f) right = left + 3.0f;
        // Clip the drawn block to the lane. The hit test works from the true
        // edges instead, so a block whose start scrolled off the left never
        // offers a start-edge handle at the window border for a boundary that
        // is not being shown.
        Rectangle block = {left, lane.y + 3.0f, right - left, lane.height - 6.0f};
        if (block.x < lane.x) {
            block.width -= lane.x - block.x;
            block.x = lane.x;
        }
        if (block.x + block.width > lane.x + lane.width) {
            block.width = lane.x + lane.width - block.x;
        }
        if (block.width < 1.0f) continue;

        bool in_selection = lyric_lane_selection_contains(&editor->lane_selection,
                                                          cue->id);
        bool hovered = hover.id == cue->id;
        Color fill = ColorAlpha(lyric_color,
                                in_selection ? 0.82f : hovered ? 0.68f : 0.38f);
        DrawRectangleRec(block, fill);
        DrawRectangleLinesEx(block, 1.0f, lyric_color);
        // The cue the editing form is bound to gets a second, inset outline.
        // Selection and form target are different things now that a drag can
        // move several cues at once, and one shade of amber cannot say both.
        if (cue->id == editor->selected_id) {
            DrawRectangleLinesEx((Rectangle){block.x + 1.0f, block.y + 1.0f,
                                             block.width - 2.0f, block.height - 2.0f},
                                 1.0f, COLOR_UI_INK);
        }
        // Show the grab handles only where a press would actually take them,
        // so the affordance and the hit test cannot drift apart.
        if (hovered && hover.zone != LYRIC_LANE_ZONE_BODY) {
            float handle_x = hover.zone == LYRIC_LANE_ZONE_START_EDGE ?
                block.x : block.x + block.width - 2.0f;
            DrawRectangleRec((Rectangle){handle_x, block.y, 2.0f, block.height},
                             COLOR_UI_INK);
        }
    }

    // Scene names are drawn last, on top of the lyric blocks. They used to be
    // drawn first and were then painted over by every overlapping block, and
    // they were gated on a lane at least 28 px high while the caller caps this
    // lane at 22 px -- so in practice they never appeared at all. Ink rather
    // than muted grey keeps them legible against the amber block as well as
    // the bare lane.
    if (lane.height >= 18.0f) {
        for (size_t i = 0; i < track->scene_switches.count; ++i) {
            const Scene_Switch_Cue *cue = &track->scene_switches.cues[i];
            float x = (float)timeline_view_x_at(view, cue->start_seconds,
                                                lane.x, lane.width);
            if (x < lane.x) continue;
            const char *name = scene_stable_name((Scene_Id)cue->scene_index);
            Vector2 size = MeasureTextEx(font, name, 11.0f, 1.0f);
            if (x + size.x + 8.0f >= lane.x + lane.width) continue;
            Vector2 position = {x + 4.0f, lane.y + (lane.height - size.y)*0.5f};
            DrawRectangleRec((Rectangle){position.x - 2.0f, position.y - 1.0f,
                                         size.x + 4.0f, size.y + 2.0f},
                             ColorAlpha(COLOR_UI_RAISED, 0.88f));
            DrawTextEx(font, name, position, 11.0f, 1.0f, COLOR_UI_INK);
        }
    }
}

static bool export_lyrics_document(const Lyrics_Document *document)
{
    const char *filters[] = {"*.lyrics.tsv"};
    char *path = tinyfd_saveFileDialog("Export timed lyrics", "lyrics.lyrics.tsv",
                                       NOB_ARRAY_LEN(filters), filters,
                                       "Musializer timed lyrics");
    if (path == NULL) return true;
    size_t required = 0;
    Lyrics_Result measured = lyrics_bridge_export(document, NULL, 0, &required);
    if (measured != LYRICS_ERROR_BUFFER_TOO_SMALL || required == 0 || required > INT_MAX) {
        return false;
    }
    char *output = malloc(required);
    if (output == NULL) return false;
    Lyrics_Result exported = lyrics_bridge_export(document, output, required, &required);
    bool saved = exported == LYRICS_OK &&
                 SaveFileData(path, output, (int)(required - 1));
    free(output);
    return saved;
}

// 1 imported, 0 cancelled, -1 failed.
static int import_lyrics_document(Lyrics_Document *document)
{
    const char *filters[] = {"*.lyrics.tsv", "*.tsv"};
    char *path = tinyfd_openFileDialog("Import timed lyrics", "./",
                                       NOB_ARRAY_LEN(filters), filters,
                                       "Musializer timed lyrics", 0);
    if (path == NULL) return 0;
    int file_size = GetFileLength(path);
    if (file_size <= 0 || (size_t)file_size > LYRICS_BRIDGE_MAX_BYTES) return -1;
    int input_size = 0;
    unsigned char *input = LoadFileData(path, &input_size);
    if (input == NULL || input_size <= 0) {
        if (input != NULL) UnloadFileData(input);
        return -1;
    }
    Lyrics_Document *candidate = malloc(sizeof(*candidate));
    if (candidate == NULL) {
        UnloadFileData(input);
        return -1;
    }
    lyrics_document_init(candidate, document->duration_seconds);
    Lyrics_Result imported = lyrics_bridge_import(
        candidate, (const char *)input, (size_t)input_size);
    UnloadFileData(input);
    bool matches = imported == LYRICS_OK &&
                   fabs(candidate->duration_seconds - document->duration_seconds) <= 0.25;
    if (matches) {
        matches = lyrics_document_normalize_duration(
            candidate, candidate, document->duration_seconds) == LYRICS_OK;
    }
    bool replaced = matches &&
                    lyrics_document_replace(document, candidate) == LYRICS_OK;
    free(candidate);
    return replaced ? 1 : -1;
}

void lyric_editor_ui_draw(Lyric_Editor *editor, Track *track, double playhead,
                       Rectangle boundary, const Lyric_Editor_Services *services)
{
    const Color signal = COLOR_ACCENT;
    const float padding = UI_PANEL_PADDING;
    const float gap = UI_CONTROL_GAP;
    DrawRectangleRec(boundary, COLOR_UI_SURFACE);
    DrawRectangleLinesEx(boundary, 1.0f, COLOR_UI_RULE);

    Rectangle list = {
        boundary.x + padding, boundary.y + padding,
        boundary.width*0.48f - padding - gap*0.5f,
        boundary.height - padding*2.0f,
    };
    Rectangle form = {
        list.x + list.width + gap, list.y,
        boundary.x + boundary.width - padding - (list.x + list.width + gap),
        list.height,
    };

    // The caption pane takes the whole panel. Typography needs two columns of
    // controls and the cue list is not part of the decision being made, so
    // hiding it is what makes the controls fit at all.
    if (editor->style_pane) {
        bool browsing = editor->font_pane && services->fonts != NULL;
        Rectangle toggle = {list.x, list.y - 3.0f, 58.0f, 34.0f};
        if (text_button_sized(services, UINT64_C(0x43415053544C4500), toggle,
                              browsing ? "Back" : "Cues", false, 14.0f) & BS_CLICKED) {
            // Back leaves the browser without leaving the style pane, which is
            // where the face the browser just imported is chosen.
            if (browsing) editor->font_pane = false;
            else editor->style_pane = false;
        }
        DrawTextEx(svc_font(services), browsing ? "CAPTION FACE" : "CAPTION STYLE",
                   (Vector2){toggle.x + toggle.width + 8.0f, list.y},
                   18.0f, 1.0f, signal);
        Rectangle style_form = {
            boundary.x + padding, list.y + 38.0f,
            boundary.width - padding*2.0f, boundary.height - padding*2.0f - 38.0f,
        };
        if (browsing) draw_font_browser(editor, style_form, services);
        else draw_caption_style_form(editor, track, style_form, services);
        return;
    }

    DrawTextEx(svc_font(services), "LYRIC CUES", (Vector2){list.x, list.y},
               18.0f, 1.0f, signal);
    char cue_count[48];
    snprintf(cue_count, sizeof(cue_count), "%zu / %u", track->lyrics.count,
             (unsigned)LYRICS_CUE_CAPACITY);
    Vector2 count_size = MeasureTextEx(svc_font(services), cue_count, 15.0f, 1.0f);
    DrawTextEx(svc_font(services), cue_count,
               (Vector2){list.x + list.width - count_size.x, list.y + 2.0f},
               15.0f, 1.0f, COLOR_UI_MUTED);

    const float row_height = 36.0f;
    size_t visible = list.height > 36.0f ? (size_t)((list.height - 32.0f)/row_height) : 0;
    size_t focus = 0;
    for (size_t i = 0; i < track->lyrics.count; ++i) {
        const Lyric_Cue *cue = &track->lyrics.cues[i];
        if (cue->id == editor->selected_id ||
            (editor->selected_id == 0 && cue->start_seconds <= playhead && playhead < cue->end_seconds)) {
            focus = i;
            break;
        }
    }
    size_t first = editor->list_first;
    if (editor->list_follow_selection) {
        first = focus > visible/2 ? focus - visible/2 : 0;
    }
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            editor->list_follow_selection = false;
            int64_t moved = (int64_t)first - (int64_t)(wheel*3.0f);
            if (moved < 0) moved = 0;
            first = (size_t)moved;
        }
    }
    if (first + visible > track->lyrics.count) {
        first = track->lyrics.count > visible ? track->lyrics.count - visible : 0;
    }
    editor->list_first = first;
    if (track->lyrics.count == 0) {
        DrawTextEx(svc_font(services), "No lyric cues. Add one at the playhead.",
                   (Vector2){list.x, list.y + 38.0f}, 16.0f, 1.0f,
                   COLOR_UI_MUTED);
    }
    for (size_t row = 0; row < visible && first + row < track->lyrics.count; ++row) {
        const Lyric_Cue *cue = &track->lyrics.cues[first + row];
        Rectangle row_boundary = {
            list.x, list.y + 28.0f + row*row_height, list.width, row_height - 2.0f,
        };
        bool selected = cue->id == editor->selected_id;
        bool current = cue->start_seconds <= playhead && playhead < cue->end_seconds;
        int state = button_with_id(services, UINT64_C(0x4C59524943000000) + cue->id, row_boundary);
        Color background = selected ? GetColor(0xE7ECFAFF) : COLOR_UI_RAISED;
        if (state & BS_HOVEROVER) background = COLOR_TRACK_BUTTON_HOVEROVER;
        DrawRectangleRec(row_boundary, background);
        DrawRectangleLinesEx(row_boundary, 1.0f, COLOR_UI_RULE);
        if (current) DrawRectangle((int)row_boundary.x, (int)row_boundary.y, 3,
                                   (int)row_boundary.height, signal);
        char time[24];
        ui_widgets_format_timestamp(cue->start_seconds, time, sizeof(time));
        DrawTextEx(svc_font(services), time,
                   (Vector2){row_boundary.x + 8.0f, row_boundary.y + 5.0f},
                   15.0f, 1.0f, COLOR_UI_MUTED);
        BeginScissorMode((int)(row_boundary.x + 90.0f), (int)row_boundary.y,
                         (int)(row_boundary.width - 94.0f), (int)row_boundary.height);
        DrawTextEx(svc_font(services), cue->text,
                   (Vector2){row_boundary.x + 94.0f, row_boundary.y + 5.0f},
                   15.0f, 1.0f, COLOR_UI_INK);
        EndScissorMode();
        if (state & BS_CLICKED) {
            if (cue->id == editor->selected_id ||
                lyric_editor_ui_allow_context_change(editor, track, services)) {
                lyric_editor_ui_select_single(editor, track, cue->id);
            }
        }
    }
    if (track->lyrics.count > visible && visible > 0) {
        float track_height = fmaxf(24.0f, (list.height - 32.0f)*
                                   (float)visible/(float)track->lyrics.count);
        float travel = list.height - 32.0f - track_height;
        float amount = (float)first/(float)(track->lyrics.count - visible);
        Rectangle scrollbar = {
            list.x + list.width - 3.0f, list.y + 28.0f + travel*amount,
            3.0f, track_height,
        };
        DrawRectangleRec(scrollbar, COLOR_ACCENT);
    }

    // The pane toggle takes the header's place. A caption style and a cue edit
    // are never wanted at once, and the panel has room for exactly one of them.
    Rectangle style_toggle = {form.x, form.y - 3.0f, 58.0f, 34.0f};
    if (text_button_sized(services, UINT64_C(0x43415053544C4500), style_toggle,
                          "Style", editor->style_pane, 14.0f) & BS_CLICKED) {
        // Switching pane hides the draft, so it goes through the same guard as
        // every other context change.
        if (!editor->style_pane) {
            if (lyric_editor_ui_allow_context_change(editor, track, services)) {
                editor->style_pane = true;
                editor->text_active = false;
            }
        } else {
            editor->style_pane = false;
            // Leaving typography behind closes the browser with it: coming
            // back to a half-finished search nobody remembers starting is
            // worse than coming back to the controls.
            editor->font_pane = false;
            editor->font_query_active = false;
        }
    }
    Rectangle add = {form.x + form.width - 92.0f, form.y - 3.0f, 92.0f, 34.0f};
    const char *pane_header = editor->style_pane ? "CAPTION STYLE" :
                              editor->draft_new ? "NEW CUE" : "SELECTED CUE";
    // Drawn only where it fits between the toggle and the document buttons; a
    // header printed through Export is how this row would fail on a narrow
    // panel, and the toggle already names the pane.
    Vector2 header_size = MeasureTextEx(svc_font(services), pane_header, 18.0f, 1.0f);
    float header_x = style_toggle.x + style_toggle.width + 8.0f;
    if (header_x + header_size.x < form.x + form.width - 258.0f - 8.0f) {
        DrawTextEx(svc_font(services), pane_header,
                   (Vector2){header_x, form.y}, 18.0f, 1.0f, signal);
    }
    Rectangle import_button = {add.x - 83.0f, add.y, 77.0f, add.height};
    Rectangle export_button = {import_button.x - 83.0f, add.y, 77.0f, add.height};
    const char *document_labels[3] = {"Export", "Import", "Add cue"};
    const float document_widths[3] = {export_button.width, import_button.width,
                                      add.width};
    float document_font = row_font_size(services, document_labels, document_widths,
                                        3, add.height);
    if (text_button_sized(services, UINT64_C(0x4C59524943494D50), import_button,
                          document_labels[1], false, document_font) & BS_CLICKED) {
        int imported = lyric_editor_ui_allow_context_change(editor, track, services) ?
                       import_lyrics_document(&track->lyrics) : 0;
        if (imported < 0) {
            services->notice_push(UI_NOTICE_ERROR, "Lyrics were not imported",
                        "The selected file is invalid, too large, or could not be read.",
                        NULL, true);
        }
        if (imported > 0) {
            lyric_editor_ui_clear_draft(editor);
            services->mark_project_dirty(track);
        }
    }
    if (text_button_sized(services, UINT64_C(0x4C59524943455850), export_button,
                          document_labels[0], false, document_font) & BS_CLICKED) {
        if (!export_lyrics_document(&track->lyrics)) {
            services->notice_push(UI_NOTICE_ERROR, "Lyrics were not exported",
                        "Choose a writable destination and try again.", NULL, true);
        }
    }
    if (text_button_sized(services, UINT64_C(0x4C59524943414444), add,
                          document_labels[2], false, document_font) & BS_CLICKED) {
        if (lyric_editor_ui_allow_context_change(editor, track, services)) lyric_editor_ui_begin_new(editor, track);
    }

    // A panel too short for the form must show the list alone rather than draw
    // Apply, Discard and Delete past its own bottom edge, which is what shipped.
    if (!lyric_editor_form_fits(boundary.height)) {
        DrawTextEx(svc_font(services), "Enlarge the window to edit a cue.",
                   (Vector2){form.x, form.y}, 15.0f, 1.0f, COLOR_UI_MUTED);
        return;
    }

    bool has_draft = editor->draft_new || editor->selected_id != 0;
    if (!has_draft) {
        DrawTextEx(svc_font(services), "Select a cue or add one at the current playhead.",
                   (Vector2){form.x, form.y + 42.0f}, 16.0f, 1.0f,
                   COLOR_UI_MUTED);
        return;
    }

    Rectangle start_row = {form.x, form.y + 30.0f, form.width, 30.0f};
    Rectangle end_row = {form.x, form.y + 64.0f, form.width, 30.0f};
    lyric_time_row(services, start_row, "START", &editor->draft_start, editor->draft_end,
                   true, playhead, track->lyrics.duration_seconds,
                   UINT64_C(0x4C59525300000000));
    lyric_time_row(services, end_row, "END", &editor->draft_end, editor->draft_start,
                   false, playhead, track->lyrics.duration_seconds,
                   UINT64_C(0x4C59524500000000));

    Rectangle text_field = {form.x, form.y + 101.0f, form.width, 37.0f};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        editor->text_active = CheckCollisionPointRec(GetMousePosition(), text_field);
    }
    DrawRectangleRec(text_field, COLOR_UI_RAISED);
    DrawRectangleLinesEx(text_field, editor->text_active ? 2.0f : 1.0f,
                         editor->text_active ? signal : COLOR_UI_RULE);
    const char *display_text = editor->draft_text[0] != '\0' ?
                               editor->draft_text : "Type lyric content";
    Color text_color = editor->draft_text[0] != '\0' ? COLOR_UI_INK : COLOR_UI_MUTED;
    BeginScissorMode((int)text_field.x + 7, (int)text_field.y,
                     (int)text_field.width - 14, (int)text_field.height);
    DrawTextEx(svc_font(services), display_text,
               (Vector2){text_field.x + 8.0f, text_field.y + 9.0f},
               17.0f, 1.0f, text_color);
    if (editor->text_active && ((int)(GetTime()*2.0) & 1) == 0) {
        Vector2 measured = MeasureTextEx(svc_font(services), editor->draft_text, 17.0f, 1.0f);
        DrawLineEx((Vector2){text_field.x + 9.0f + measured.x, text_field.y + 8.0f},
                   (Vector2){text_field.x + 9.0f + measured.x, text_field.y + 29.0f},
                   1.0f, signal);
    }
    EndScissorMode();
    lyric_editor_ui_text_input_update(editor, services);

    Rectangle apply = {form.x, form.y + 146.0f, 92.0f, UI_BUTTON_HEIGHT};
    Rectangle discard_button = {apply.x + apply.width + gap, apply.y, 104.0f, apply.height};
    Rectangle delete_button = {
        discard_button.x + discard_button.width + gap, apply.y, 92.0f, apply.height,
    };
    const char *discard_label = editor->draft_new ? "Cancel edit" : "Discard edit";
    const char *draft_labels[3] = {"Apply", discard_label, "Delete"};
    const float draft_widths[3] = {apply.width, discard_button.width,
                                   delete_button.width};
    float draft_font = row_font_size(services, draft_labels, draft_widths, 3,
                                     apply.height);
    if (text_button_sized(services, UINT64_C(0x4C59524943415050), apply,
                          draft_labels[0], false, draft_font) & BS_CLICKED) {
        lyric_editor_ui_apply(editor, track, services);
    }
    if (text_button_sized(services, UINT64_C(0x4C59524943444953), discard_button,
                          discard_label, false, draft_font) & BS_CLICKED) {
        lyric_editor_ui_clear_draft(editor);
    }
    if (!editor->draft_new &&
        (danger_text_button_sized(services, UINT64_C(0x4C5952494344454C),
                                  delete_button, draft_labels[2], false,
                                  draft_font) & BS_CLICKED)) {
        if (lyrics_delete(&track->lyrics, editor->selected_id) == LYRICS_OK) {
            lyric_editor_ui_clear_draft(editor);
            services->mark_project_dirty(track);
        }
    }
    DrawTextEx(svc_font(services), "Ctrl+Enter applies the edit  |  Ctrl+V pastes",
               (Vector2){form.x, apply.y + apply.height + 7.0f}, 14.0f, 1.0f,
               COLOR_UI_MUTED);
    if (editor->text_active &&
        (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_ENTER)) {
        lyric_editor_ui_apply(editor, track, services);
    }
}
