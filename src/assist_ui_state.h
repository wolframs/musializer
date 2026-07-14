#ifndef MUSIALIZER_ASSIST_UI_STATE_H_
#define MUSIALIZER_ASSIST_UI_STATE_H_

#include <stdbool.h>
#include <stddef.h>

#define ASSIST_JOB_TIMEOUT_SECONDS 2400.0

typedef enum Assist_Mode {
    ASSIST_MODE_LYRICS,
    ASSIST_MODE_SECTIONS,
    ASSIST_MODE_MIMO,
    ASSIST_MODE_ALL,
    ASSIST_MODE_COUNT,
} Assist_Mode;

typedef enum Assist_Job_State {
    ASSIST_JOB_IDLE,
    ASSIST_JOB_RUNNING,
    ASSIST_JOB_CANCELLING,
    ASSIST_JOB_TIMING_OUT,
    ASSIST_JOB_FAILING,
    ASSIST_JOB_SUCCEEDED,
    ASSIST_JOB_FAILED,
    ASSIST_JOB_CANCELLED,
    ASSIST_JOB_TIMED_OUT,
} Assist_Job_State;

typedef enum Assist_Start_Block {
    ASSIST_START_ALLOWED,
    ASSIST_START_HELPER_UNAVAILABLE,
    ASSIST_START_JOB_ACTIVE,
    ASSIST_START_RESULT_PENDING,
} Assist_Start_Block;

typedef enum Assist_Panel_Content {
    ASSIST_PANEL_READY,
    ASSIST_PANEL_CONFIRMATION,
    ASSIST_PANEL_RUNNING,
    ASSIST_PANEL_CANCELLING,
    ASSIST_PANEL_CANDIDATE,
} Assist_Panel_Content;

typedef struct Assist_Ui_Layout {
    size_t mode_columns;
    size_t mode_rows;
    float mode_top;
    float mode_row_height;
    float status_y;
    float content_y;
    float required_height;
} Assist_Ui_Layout;

bool assist_job_is_active(Assist_Job_State state);
bool assist_job_deadline_expired(Assist_Job_State state,
                                 double started_at,
                                 double now);
double assist_job_deadline_remaining(Assist_Job_State state,
                                     double started_at,
                                     double now);
Assist_Start_Block assist_start_block(bool helper_available,
                                      Assist_Job_State job_state,
                                      bool candidate_pending);
const char *assist_start_block_reason(Assist_Start_Block block);
Assist_Panel_Content assist_panel_content(Assist_Job_State job_state,
                                          bool confirmation_pending,
                                          bool candidate_pending);

const char *assist_mode_display_name(Assist_Mode mode);
const char *assist_mode_argument(Assist_Mode mode);
const char *assist_mode_badge(Assist_Mode mode);
const char *assist_mode_workflow(Assist_Mode mode);
const char *assist_mode_data_boundary(Assist_Mode mode);

// A staged lyric replacement must never clear an authored draft implicitly.
bool assist_candidate_conflicts_with_lyric_draft(bool replaces_lyrics,
                                                 bool targets_active_track,
                                                 bool draft_is_dirty);

// Pure layout policy used by the Raylib surface and headless tests. Widths at
// and above the supported 960 px window keep all modes on one row; narrower
// embedders receive a two-column grid rather than clipped buttons.
Assist_Ui_Layout assist_ui_layout(float panel_width,
                                  Assist_Panel_Content content);

// Converts desired Assist content height into a timeline height while keeping
// a useful scene preview at supported small-window sizes.
float assist_timeline_height(float screen_height, float toolbar_height,
                             float panel_height);

#endif // MUSIALIZER_ASSIST_UI_STATE_H_
