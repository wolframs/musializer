#ifndef MUSIALIZER_ASSIST_UI_STATE_H_
#define MUSIALIZER_ASSIST_UI_STATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    ASSIST_PANEL_EMPTY,
} Assist_Panel_Content;

// Where the authored lyric text a lyrics run will synchronize against is coming
// from. The helper's own priority is override, then a sibling
// <stem>.lyrics.txt, then an unsynchronized tag embedded in the audio. The
// first two are knowable from here; the third is not, because reading it needs
// ffprobe, so it is never claimed -- only mentioned as still possible.
typedef enum Assist_Lyric_Reference {
    ASSIST_LYRIC_REFERENCE_NONE = 0,
    ASSIST_LYRIC_REFERENCE_SIBLING,
    ASSIST_LYRIC_REFERENCE_CHOSEN,
} Assist_Lyric_Reference;

// Whether a mode aligns authored lyrics at all. Offering the control on a mode
// that ignores it would promise something the run does not do.
bool assist_mode_uses_lyric_reference(Assist_Mode mode);
const char *assist_lyric_reference_summary(Assist_Lyric_Reference reference);

// The sibling the helper looks for: "<directory>/<stem>.lyrics.txt", matching
// pathlib's stem rule, which strips only the final extension and treats a
// leading dot as part of the name rather than as an extension. Returns false
// when the result would not fit, leaving the output untouched.
bool assist_lyric_sibling_path(const char *audio_path, char *output,
                               size_t capacity);

typedef struct Assist_Ui_Layout {
    size_t mode_columns;
    size_t mode_rows;
    float mode_top;
    float mode_row_height;
    float status_y;
    float content_y;
    // Where the lyric-reference line starts, relative to the panel. Zero when
    // there is no such row, which is also what makes "is the row present?" a
    // question the drawing code can ask without repeating the policy.
    float reference_y;
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
const char *assist_mode_empty_result(Assist_Mode mode);

// A completed helper is applyable only when it produced at least one lane the
// selected workflow was authorized to replace. Empty validated results are a
// truthful terminal outcome, not a staged no-op.
bool assist_result_has_changes(uint32_t authorized_lanes,
                               uint32_t available_lanes);

// A staged lyric replacement must never clear an authored draft implicitly.
bool assist_candidate_conflicts_with_lyric_draft(bool replaces_lyrics,
                                                 bool targets_active_track,
                                                 bool draft_is_dirty);

// Pure layout policy used by the Raylib surface and headless tests. Widths at
// and above the supported 960 px window keep all modes on one row; narrower
// embedders receive a two-column grid rather than clipped buttons.
// reference_row adds the lyric-reference line and its controls to the
// confirmation step. It is a parameter rather than an assumption because the
// row only exists for modes that use a reference, and a panel that reserves
// height it never draws pushes the scene preview down for nothing.
Assist_Ui_Layout assist_ui_layout(float panel_width,
                                  Assist_Panel_Content content,
                                  bool reference_row);

// Converts desired Assist content height into a timeline height while keeping
// a useful scene preview at supported small-window sizes.
float assist_timeline_height(float screen_height, float toolbar_height,
                             float panel_height);

#endif // MUSIALIZER_ASSIST_UI_STATE_H_
