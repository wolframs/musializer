#ifndef MUSIALIZER_FONT_IMPORT_STATE_H_
#define MUSIALIZER_FONT_IMPORT_STATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The decisions the caption font browser has to get right, kept away from the
// raylib surface so they can be tested without a window: when a network call
// is allowed, when a job has run out of time, and which result belongs to the
// job the user is actually waiting for.
//
// Downloading a face is the second network boundary in this application and
// the weaker of the two. MiMo sends the user's audio; this sends a family
// name. The consent is still explicit and still separate, because "I asked for
// a font" is not "I agreed to send my music somewhere".

// A catalogue is one request against a static file. A face is three requests
// and a few hundred kilobytes. Neither should be able to hang the panel.
#define FONT_CATALOGUE_JOB_TIMEOUT_SECONDS 120.0
#define FONT_FETCH_JOB_TIMEOUT_SECONDS 180.0
// How long a finished job's outcome stays on screen before the panel returns
// to browsing. Failures are not cleared on a timer; they wait to be read.
#define FONT_IMPORT_SUCCESS_LINGER_SECONDS 6.0

typedef enum Font_Import_Job {
    FONT_IMPORT_JOB_NONE = 0,
    FONT_IMPORT_JOB_CATALOGUE,
    FONT_IMPORT_JOB_FETCH,
} Font_Import_Job;

typedef enum Font_Import_Job_State {
    FONT_IMPORT_IDLE = 0,
    FONT_IMPORT_RUNNING,
    FONT_IMPORT_CANCELLING,
    FONT_IMPORT_SUCCEEDED,
    FONT_IMPORT_FAILED,
    FONT_IMPORT_CANCELLED,
    FONT_IMPORT_TIMED_OUT,
} Font_Import_Job_State;

typedef enum Font_Import_Block {
    FONT_IMPORT_ALLOWED = 0,
    FONT_IMPORT_BLOCK_HELPER_UNAVAILABLE,
    FONT_IMPORT_BLOCK_CONSENT_REQUIRED,
    FONT_IMPORT_BLOCK_JOB_ACTIVE,
    FONT_IMPORT_BLOCK_NO_FAMILY,
    FONT_IMPORT_BLOCK_NO_TRACK,
} Font_Import_Block;

typedef enum Font_Import_Panel {
    // Before the first network call, the panel explains what leaves the
    // machine and asks. It is not a dialog to dismiss on the way to the list.
    FONT_IMPORT_PANEL_CONSENT = 0,
    FONT_IMPORT_PANEL_LOADING,
    FONT_IMPORT_PANEL_BROWSING,
    FONT_IMPORT_PANEL_FETCHING,
    FONT_IMPORT_PANEL_CANCELLING,
    FONT_IMPORT_PANEL_FAILED,
} Font_Import_Panel;

bool font_import_job_is_active(Font_Import_Job_State state);
bool font_import_job_is_finished(Font_Import_Job_State state);
double font_import_job_timeout(Font_Import_Job job);
bool font_import_job_deadline_expired(Font_Import_Job job,
                                      Font_Import_Job_State state,
                                      double started_at, double now);
double font_import_job_deadline_remaining(Font_Import_Job job,
                                          Font_Import_Job_State state,
                                          double started_at, double now);

// Whether a browse may begin. A catalogue request is a network call like any
// other, so it is gated by the same consent as a download.
Font_Import_Block font_import_browse_block(bool helper_available,
                                           bool network_allowed,
                                           Font_Import_Job_State state);
// Whether a download may begin. A track is required because the face has to
// belong to something: without one there is nothing for it to be bundled into.
Font_Import_Block font_import_fetch_block(bool helper_available,
                                          bool network_allowed,
                                          Font_Import_Job_State state,
                                          bool family_selected,
                                          bool track_present);
const char *font_import_block_reason(Font_Import_Block block);

Font_Import_Panel font_import_panel(bool network_allowed,
                                    bool catalogue_loaded,
                                    Font_Import_Job job,
                                    Font_Import_Job_State state);

// A job's result is only the result the user is waiting for when its nonce is
// the one that was issued last. Anything else is a job that was cancelled or
// superseded while it was still writing, and applying it would replace the
// caption face with one nobody asked for.
bool font_import_result_is_current(uint64_t result_nonce, uint64_t active_nonce);
uint64_t font_import_next_nonce(uint64_t nonce);

// A finished job's outcome should not sit on screen forever. Success returns
// to the list on its own; failure waits, because the reason is the point.
bool font_import_outcome_expired(Font_Import_Job_State state,
                                 double finished_at, double now);

#endif // MUSIALIZER_FONT_IMPORT_STATE_H_
