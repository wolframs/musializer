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
        if (result == LYRICS_OK) lyric_editor_ui_select(editor, track, id);
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

void lyric_editor_ui_draw_lane(Lyric_Editor *editor, Track *track, float track_length,
                            Rectangle lane, Font font,
                            bool editor_open_on_click,
                            const Lyric_Editor_Services *services)
{
    DrawRectangleRec(lane, COLOR_UI_RAISED);
    DrawLineEx((Vector2){lane.x, lane.y}, (Vector2){lane.x + lane.width, lane.y},
               1.0f, COLOR_UI_RULE);
    const Color lyric_color = (Color){242, 190, 66, 255};
    for (size_t i = 0; i < track->scene_switches.count; ++i) {
        const Scene_Switch_Cue *cue = &track->scene_switches.cues[i];
        float x = lane.x + (float)(cue->start_seconds/track_length)*lane.width;
        DrawLineEx((Vector2){x, lane.y}, (Vector2){x, lane.y + lane.height},
                   1.0f + cue->strength*2.0f, ColorAlpha((Color){0, 230, 118, 255}, 0.58f));
    }
    for (size_t i = 0; i < track->lyrics.count; ++i) {
        const Lyric_Cue *cue = &track->lyrics.cues[i];
        float left = lane.x + (float)(cue->start_seconds/track_length)*lane.width;
        float right = lane.x + (float)(cue->end_seconds/track_length)*lane.width;
        if (right - left < 3.0f) right = left + 3.0f;
        Rectangle block = {left, lane.y + 3.0f, right - left, lane.height - 6.0f};
        bool selected = cue->id == editor->selected_id;
        Color fill = ColorAlpha(lyric_color, selected ? 0.82f : 0.38f);
        if (CheckCollisionPointRec(GetMousePosition(), block)) fill = ColorAlpha(lyric_color, 0.68f);
        DrawRectangleRec(block, fill);
        DrawRectangleLinesEx(block, 1.0f, lyric_color);
        if (CheckCollisionPointRec(GetMousePosition(), block) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (cue->id == editor->selected_id ||
                lyric_editor_ui_allow_context_change(editor, track, services)) {
                lyric_editor_ui_select(editor, track, cue->id);
                (void)editor_open_on_click;
            }
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
            float x = lane.x + (float)(cue->start_seconds/track_length)*lane.width;
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
                lyric_editor_ui_select(editor, track, cue->id);
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

    DrawTextEx(svc_font(services), editor->draft_new ? "NEW CUE" : "SELECTED CUE",
               (Vector2){form.x, form.y}, 18.0f, 1.0f, signal);
    Rectangle add = {form.x + form.width - 92.0f, form.y - 3.0f, 92.0f, 34.0f};
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
