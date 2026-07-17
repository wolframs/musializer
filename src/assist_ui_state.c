#include "assist_ui_state.h"

#include <math.h>

bool assist_job_is_active(Assist_Job_State state)
{
    return state == ASSIST_JOB_RUNNING || state == ASSIST_JOB_CANCELLING ||
           state == ASSIST_JOB_TIMING_OUT || state == ASSIST_JOB_FAILING;
}

bool assist_job_deadline_expired(Assist_Job_State state,
                                 double started_at,
                                 double now)
{
    return state == ASSIST_JOB_RUNNING && isfinite(started_at) &&
           isfinite(now) && now >= started_at &&
           now - started_at >= ASSIST_JOB_TIMEOUT_SECONDS;
}

double assist_job_deadline_remaining(Assist_Job_State state,
                                     double started_at,
                                     double now)
{
    if (state != ASSIST_JOB_RUNNING || !isfinite(started_at) ||
        !isfinite(now) || now < started_at) return 0.0;
    double remaining = ASSIST_JOB_TIMEOUT_SECONDS - (now - started_at);
    return remaining > 0.0 ? remaining : 0.0;
}

Assist_Start_Block assist_start_block(bool helper_available,
                                      Assist_Job_State job_state,
                                      bool candidate_pending)
{
    if (assist_job_is_active(job_state)) return ASSIST_START_JOB_ACTIVE;
    if (candidate_pending) return ASSIST_START_RESULT_PENDING;
    if (!helper_available) return ASSIST_START_HELPER_UNAVAILABLE;
    return ASSIST_START_ALLOWED;
}

const char *assist_start_block_reason(Assist_Start_Block block)
{
    switch (block) {
    case ASSIST_START_ALLOWED: return "";
    case ASSIST_START_HELPER_UNAVAILABLE:
        return "Assist helper script not found in this installation.";
    case ASSIST_START_JOB_ACTIVE:
        return "Cancel the active analysis before starting another workflow.";
    case ASSIST_START_RESULT_PENDING:
        return "Apply or discard the staged result before starting another workflow.";
    }
    return "Assist is unavailable.";
}

Assist_Panel_Content assist_panel_content(Assist_Job_State job_state,
                                          bool confirmation_pending,
                                          bool candidate_pending)
{
    if (candidate_pending) return ASSIST_PANEL_CANDIDATE;
    if (job_state == ASSIST_JOB_CANCELLING ||
        job_state == ASSIST_JOB_TIMING_OUT ||
        job_state == ASSIST_JOB_FAILING) return ASSIST_PANEL_CANCELLING;
    if (job_state == ASSIST_JOB_RUNNING) return ASSIST_PANEL_RUNNING;
    if (confirmation_pending) return ASSIST_PANEL_CONFIRMATION;
    if (job_state == ASSIST_JOB_SUCCEEDED || job_state == ASSIST_JOB_FAILED ||
        job_state == ASSIST_JOB_CANCELLED || job_state == ASSIST_JOB_TIMED_OUT) {
        return ASSIST_PANEL_EMPTY;
    }
    return ASSIST_PANEL_READY;
}

const char *assist_mode_display_name(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS: return "Timed lyrics";
    case ASSIST_MODE_SECTIONS: return "Scene changes";
    case ASSIST_MODE_MIMO: return "MiMo feelings";
    case ASSIST_MODE_ALL: return "Full assist";
    case ASSIST_MODE_COUNT: break;
    }
    return "Analysis";
}

const char *assist_mode_argument(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS: return "lyrics";
    case ASSIST_MODE_SECTIONS: return "sections";
    case ASSIST_MODE_MIMO: return "mimo";
    case ASSIST_MODE_ALL: return "all";
    case ASSIST_MODE_COUNT: break;
    }
    return "sections";
}

const char *assist_mode_badge(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS: return "LOCAL AUDIO / CODEX TEXT";
    case ASSIST_MODE_SECTIONS: return "LOCAL AUDIO";
    case ASSIST_MODE_MIMO: return "OPENROUTER AUDIO";
    case ASSIST_MODE_ALL: return "LOCAL + REMOTE";
    case ASSIST_MODE_COUNT: break;
    }
    return "";
}

const char *assist_mode_workflow(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS:
        return "Workflow: local Whisper transcription, then Codex timing review.";
    case ASSIST_MODE_SECTIONS:
        return "Workflow: measured local audio analysis proposes scene-change sections.";
    case ASSIST_MODE_MIMO:
        return "Workflow: MiMo describes how the music feels and stages feeling cues.";
    case ASSIST_MODE_ALL:
        return "Workflow: lyrics, measured scene changes, and MiMo feeling cues.";
    case ASSIST_MODE_COUNT: break;
    }
    return "";
}

const char *assist_mode_data_boundary(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS:
        return "Audio stays local. Transcript evidence is sent to headless Codex.";
    case ASSIST_MODE_SECTIONS:
        return "Runs locally. Audio and analysis output do not leave this computer.";
    case ASSIST_MODE_MIMO:
        return "Track audio is sent to OpenRouter for MiMo; Zero Data Retention is requested.";
    case ASSIST_MODE_ALL:
        return "Transcript evidence goes to Codex; track audio goes to OpenRouter MiMo with ZDR requested.";
    case ASSIST_MODE_COUNT: break;
    }
    return "";
}

const char *assist_mode_empty_result(Assist_Mode mode)
{
    switch (mode) {
    case ASSIST_MODE_LYRICS:
        return "Whisper and Codex produced no validated lyric cues. Existing lyrics were left unchanged.";
    case ASSIST_MODE_SECTIONS:
        return "Measured analysis produced no validated scene changes. Existing cues were left unchanged.";
    case ASSIST_MODE_MIMO:
        return "MiMo produced no validated feeling cues. Existing semantic events were left unchanged.";
    case ASSIST_MODE_ALL:
        return "The completed workflow produced no validated editor changes. Existing content was left unchanged.";
    case ASSIST_MODE_COUNT: break;
    }
    return "The completed workflow produced no validated editor changes.";
}

bool assist_result_has_changes(uint32_t authorized_lanes,
                               uint32_t available_lanes)
{
    return (authorized_lanes & available_lanes) != 0;
}

bool assist_candidate_conflicts_with_lyric_draft(bool replaces_lyrics,
                                                 bool targets_active_track,
                                                 bool draft_is_dirty)
{
    return replaces_lyrics && targets_active_track && draft_is_dirty;
}

Assist_Ui_Layout assist_ui_layout(float panel_width,
                                  Assist_Panel_Content content)
{
    Assist_Ui_Layout layout = {0};
    layout.mode_columns = isfinite(panel_width) && panel_width >= 760.0f ? 4u : 2u;
    layout.mode_rows = (ASSIST_MODE_COUNT + layout.mode_columns - 1u)/
                       layout.mode_columns;
    layout.mode_top = 60.0f;
    layout.mode_row_height = 58.0f;
    layout.status_y = layout.mode_top +
                      (float)layout.mode_rows*layout.mode_row_height + 3.0f;
    layout.content_y = layout.status_y + 25.0f;

    float content_height = 36.0f;
    switch (content) {
    case ASSIST_PANEL_READY: content_height = 22.0f; break;
    case ASSIST_PANEL_CONFIRMATION: content_height = 84.0f; break;
    case ASSIST_PANEL_RUNNING: content_height = 84.0f; break;
    case ASSIST_PANEL_CANCELLING: content_height = 44.0f; break;
    case ASSIST_PANEL_CANDIDATE: content_height = 118.0f; break;
    case ASSIST_PANEL_EMPTY: content_height = 84.0f; break;
    }
    layout.required_height = layout.content_y + content_height + 10.0f;
    return layout;
}

float assist_timeline_height(float screen_height, float toolbar_height,
                             float panel_height)
{
    if (!isfinite(screen_height) || !isfinite(toolbar_height) ||
        !isfinite(panel_height) || screen_height <= 0.0f ||
        toolbar_height < 0.0f || panel_height <= 0.0f) {
        return 0.0f;
    }
    // Controls, transport, waveform lane, and their margins consume 158 px in
    // timeline(). Preserve at least 150 px for the scene at the small-window
    // boundary; larger windows receive the exact requested panel height.
    float desired = panel_height + 158.0f;
    float maximum = screen_height - toolbar_height - 150.0f;
    if (maximum < 150.0f) maximum = 150.0f;
    return desired < maximum ? desired : maximum;
}
