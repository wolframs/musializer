#include "font_import_state.h"

bool font_import_job_is_active(Font_Import_Job_State state)
{
    return state == FONT_IMPORT_RUNNING || state == FONT_IMPORT_CANCELLING;
}

bool font_import_job_is_finished(Font_Import_Job_State state)
{
    return state == FONT_IMPORT_SUCCEEDED || state == FONT_IMPORT_FAILED ||
           state == FONT_IMPORT_CANCELLED || state == FONT_IMPORT_TIMED_OUT;
}

double font_import_job_timeout(Font_Import_Job job)
{
    switch (job) {
    case FONT_IMPORT_JOB_CATALOGUE: return FONT_CATALOGUE_JOB_TIMEOUT_SECONDS;
    case FONT_IMPORT_JOB_FETCH:     return FONT_FETCH_JOB_TIMEOUT_SECONDS;
    case FONT_IMPORT_JOB_NONE:      break;
    }
    return 0.0;
}

bool font_import_job_deadline_expired(Font_Import_Job job,
                                      Font_Import_Job_State state,
                                      double started_at, double now)
{
    if (!font_import_job_is_active(state)) return false;
    double timeout = font_import_job_timeout(job);
    if (timeout <= 0.0) return false;
    // A clock that went backwards is not evidence that the job overran. This
    // is monotonic in practice, but a suspend/resume is not worth a spurious
    // "the download timed out" on a job that is running perfectly well.
    if (now < started_at) return false;
    return now - started_at >= timeout;
}

double font_import_job_deadline_remaining(Font_Import_Job job,
                                          Font_Import_Job_State state,
                                          double started_at, double now)
{
    if (!font_import_job_is_active(state)) return 0.0;
    double timeout = font_import_job_timeout(job);
    if (timeout <= 0.0) return 0.0;
    if (now < started_at) return timeout;
    double remaining = timeout - (now - started_at);
    return remaining > 0.0 ? remaining : 0.0;
}

Font_Import_Block font_import_browse_block(bool helper_available,
                                           bool network_allowed,
                                           Font_Import_Job_State state)
{
    if (!helper_available) return FONT_IMPORT_BLOCK_HELPER_UNAVAILABLE;
    if (!network_allowed) return FONT_IMPORT_BLOCK_CONSENT_REQUIRED;
    if (font_import_job_is_active(state)) return FONT_IMPORT_BLOCK_JOB_ACTIVE;
    return FONT_IMPORT_ALLOWED;
}

Font_Import_Block font_import_fetch_block(bool helper_available,
                                          bool network_allowed,
                                          Font_Import_Job_State state,
                                          bool family_selected,
                                          bool track_present)
{
    Font_Import_Block block = font_import_browse_block(helper_available,
                                                       network_allowed, state);
    if (block != FONT_IMPORT_ALLOWED) return block;
    if (!track_present) return FONT_IMPORT_BLOCK_NO_TRACK;
    if (!family_selected) return FONT_IMPORT_BLOCK_NO_FAMILY;
    return FONT_IMPORT_ALLOWED;
}

const char *font_import_block_reason(Font_Import_Block block)
{
    switch (block) {
    case FONT_IMPORT_ALLOWED:
        return "";
    case FONT_IMPORT_BLOCK_HELPER_UNAVAILABLE:
        return "The font helper is missing from this installation.";
    case FONT_IMPORT_BLOCK_CONSENT_REQUIRED:
        return "Allow contacting Google Fonts first.";
    case FONT_IMPORT_BLOCK_JOB_ACTIVE:
        return "A font request is already running.";
    case FONT_IMPORT_BLOCK_NO_FAMILY:
        return "Choose a family first.";
    case FONT_IMPORT_BLOCK_NO_TRACK:
        return "Open a track before importing a caption face.";
    }
    return "";
}

Font_Import_Panel font_import_panel(bool network_allowed,
                                    bool catalogue_loaded,
                                    Font_Import_Job job,
                                    Font_Import_Job_State state)
{
    // Consent outranks everything. Withdrawing it while a request is in flight
    // must show the consent panel, not a progress bar for work the user has
    // just said they did not want.
    if (!network_allowed) return FONT_IMPORT_PANEL_CONSENT;
    if (state == FONT_IMPORT_CANCELLING) return FONT_IMPORT_PANEL_CANCELLING;
    if (state == FONT_IMPORT_RUNNING) {
        return job == FONT_IMPORT_JOB_FETCH ? FONT_IMPORT_PANEL_FETCHING
                                            : FONT_IMPORT_PANEL_LOADING;
    }
    if (state == FONT_IMPORT_FAILED || state == FONT_IMPORT_TIMED_OUT) {
        return FONT_IMPORT_PANEL_FAILED;
    }
    // A cancelled job leaves whatever was already loaded on screen: the user
    // stopped a request, they did not ask to lose the list.
    return catalogue_loaded ? FONT_IMPORT_PANEL_BROWSING : FONT_IMPORT_PANEL_LOADING;
}

bool font_import_result_is_current(uint64_t result_nonce, uint64_t active_nonce)
{
    // Zero is the nonce no job ever carries, so it can never match and a
    // zeroed structure cannot accidentally accept a stale artifact.
    return result_nonce != 0 && result_nonce == active_nonce;
}

uint64_t font_import_next_nonce(uint64_t nonce)
{
    uint64_t next = nonce + 1u;
    return next == 0 ? 1u : next;
}

bool font_import_outcome_expired(Font_Import_Job_State state,
                                 double finished_at, double now)
{
    if (state != FONT_IMPORT_SUCCEEDED && state != FONT_IMPORT_CANCELLED) {
        return false;
    }
    if (now < finished_at) return false;
    return now - finished_at >= FONT_IMPORT_SUCCESS_LINGER_SECONDS;
}
