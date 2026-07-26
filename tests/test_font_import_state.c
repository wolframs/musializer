#include "font_import_state.h"
#include "test_support.h"

#include <string.h>

TEST(font_import_consent_gates_every_request_including_the_catalogue)
{
    // Fetching the family list is a network call like any other. Letting the
    // browse happen "because it is only a list" would contact Google before
    // the user had agreed to contact anyone.
    EXPECT_TRUE(font_import_browse_block(true, false, FONT_IMPORT_IDLE) ==
                FONT_IMPORT_BLOCK_CONSENT_REQUIRED);
    EXPECT_TRUE(font_import_browse_block(true, true, FONT_IMPORT_IDLE) ==
                FONT_IMPORT_ALLOWED);
    EXPECT_TRUE(font_import_fetch_block(true, false, FONT_IMPORT_IDLE, true, true) ==
                FONT_IMPORT_BLOCK_CONSENT_REQUIRED);

    // A missing helper is reported ahead of consent: asking someone to approve
    // a network call this installation cannot make would be a pointless
    // question with a misleading answer.
    EXPECT_TRUE(font_import_browse_block(false, false, FONT_IMPORT_IDLE) ==
                FONT_IMPORT_BLOCK_HELPER_UNAVAILABLE);
    EXPECT_TRUE(font_import_browse_block(false, true, FONT_IMPORT_IDLE) ==
                FONT_IMPORT_BLOCK_HELPER_UNAVAILABLE);

    for (int block = FONT_IMPORT_ALLOWED; block <= FONT_IMPORT_BLOCK_NO_TRACK; ++block) {
        const char *reason = font_import_block_reason((Font_Import_Block)block);
        EXPECT_TRUE(reason != NULL);
        EXPECT_TRUE((block == FONT_IMPORT_ALLOWED) == (reason[0] == '\0'));
    }
}

TEST(font_import_refuses_to_start_a_second_request_or_one_with_nowhere_to_go)
{
    EXPECT_TRUE(font_import_browse_block(true, true, FONT_IMPORT_RUNNING) ==
                FONT_IMPORT_BLOCK_JOB_ACTIVE);
    EXPECT_TRUE(font_import_browse_block(true, true, FONT_IMPORT_CANCELLING) ==
                FONT_IMPORT_BLOCK_JOB_ACTIVE);
    // A finished job is not an active one: the panel must be usable again the
    // moment a request stops, not once its outcome has been dismissed.
    EXPECT_TRUE(font_import_browse_block(true, true, FONT_IMPORT_SUCCEEDED) ==
                FONT_IMPORT_ALLOWED);
    EXPECT_TRUE(font_import_browse_block(true, true, FONT_IMPORT_FAILED) ==
                FONT_IMPORT_ALLOWED);
    EXPECT_TRUE(font_import_browse_block(true, true, FONT_IMPORT_TIMED_OUT) ==
                FONT_IMPORT_ALLOWED);

    // A face has to belong to something. Without a track there is nothing for
    // it to be bundled into, and the download would be discarded on exit.
    EXPECT_TRUE(font_import_fetch_block(true, true, FONT_IMPORT_IDLE, true, false) ==
                FONT_IMPORT_BLOCK_NO_TRACK);
    EXPECT_TRUE(font_import_fetch_block(true, true, FONT_IMPORT_IDLE, false, true) ==
                FONT_IMPORT_BLOCK_NO_FAMILY);
    EXPECT_TRUE(font_import_fetch_block(true, true, FONT_IMPORT_IDLE, true, true) ==
                FONT_IMPORT_ALLOWED);
}

TEST(font_import_deadlines_are_per_job_and_survive_a_clock_that_moves_backwards)
{
    EXPECT_TRUE(font_import_job_timeout(FONT_IMPORT_JOB_CATALOGUE) ==
                FONT_CATALOGUE_JOB_TIMEOUT_SECONDS);
    EXPECT_TRUE(font_import_job_timeout(FONT_IMPORT_JOB_FETCH) ==
                FONT_FETCH_JOB_TIMEOUT_SECONDS);
    EXPECT_TRUE(font_import_job_timeout(FONT_IMPORT_JOB_NONE) == 0.0);

    // A download is allowed longer than a list, and neither borrows the
    // other's budget.
    EXPECT_TRUE(font_import_job_deadline_expired(
        FONT_IMPORT_JOB_CATALOGUE, FONT_IMPORT_RUNNING, 100.0,
        100.0 + FONT_CATALOGUE_JOB_TIMEOUT_SECONDS));
    EXPECT_TRUE(!font_import_job_deadline_expired(
        FONT_IMPORT_JOB_FETCH, FONT_IMPORT_RUNNING, 100.0,
        100.0 + FONT_CATALOGUE_JOB_TIMEOUT_SECONDS));
    EXPECT_TRUE(!font_import_job_deadline_expired(
        FONT_IMPORT_JOB_CATALOGUE, FONT_IMPORT_RUNNING, 100.0,
        100.0 + FONT_CATALOGUE_JOB_TIMEOUT_SECONDS - 0.001));

    // A job being cancelled still has a deadline; otherwise a worker that
    // ignores the signal keeps the panel locked forever.
    EXPECT_TRUE(font_import_job_deadline_expired(
        FONT_IMPORT_JOB_FETCH, FONT_IMPORT_CANCELLING, 0.0,
        FONT_FETCH_JOB_TIMEOUT_SECONDS + 1.0));
    // A finished job has none.
    EXPECT_TRUE(!font_import_job_deadline_expired(
        FONT_IMPORT_JOB_FETCH, FONT_IMPORT_SUCCEEDED, 0.0, 1e9));

    // Suspend/resume can hand back a smaller "now". That is not a job overrun.
    EXPECT_TRUE(!font_import_job_deadline_expired(
        FONT_IMPORT_JOB_FETCH, FONT_IMPORT_RUNNING, 500.0, 100.0));
    EXPECT_TRUE(font_import_job_deadline_remaining(
        FONT_IMPORT_JOB_FETCH, FONT_IMPORT_RUNNING, 500.0, 100.0) ==
        FONT_FETCH_JOB_TIMEOUT_SECONDS);

    EXPECT_TRUE(font_import_job_deadline_remaining(
        FONT_IMPORT_JOB_CATALOGUE, FONT_IMPORT_RUNNING, 10.0, 10.0) ==
        FONT_CATALOGUE_JOB_TIMEOUT_SECONDS);
    EXPECT_TRUE(font_import_job_deadline_remaining(
        FONT_IMPORT_JOB_CATALOGUE, FONT_IMPORT_RUNNING, 10.0, 1e9) == 0.0);
    EXPECT_TRUE(font_import_job_deadline_remaining(
        FONT_IMPORT_JOB_CATALOGUE, FONT_IMPORT_IDLE, 10.0, 10.0) == 0.0);
}

TEST(font_import_panel_puts_consent_ahead_of_work_already_in_flight)
{
    // Withdrawing consent mid-request must show the consent panel. Continuing
    // to draw a progress bar would tell the user their refusal did nothing.
    EXPECT_TRUE(font_import_panel(false, true, FONT_IMPORT_JOB_FETCH,
                                  FONT_IMPORT_RUNNING) ==
                FONT_IMPORT_PANEL_CONSENT);
    EXPECT_TRUE(font_import_panel(false, false, FONT_IMPORT_JOB_NONE,
                                  FONT_IMPORT_IDLE) == FONT_IMPORT_PANEL_CONSENT);

    EXPECT_TRUE(font_import_panel(true, false, FONT_IMPORT_JOB_CATALOGUE,
                                  FONT_IMPORT_RUNNING) ==
                FONT_IMPORT_PANEL_LOADING);
    EXPECT_TRUE(font_import_panel(true, true, FONT_IMPORT_JOB_FETCH,
                                  FONT_IMPORT_RUNNING) ==
                FONT_IMPORT_PANEL_FETCHING);
    EXPECT_TRUE(font_import_panel(true, true, FONT_IMPORT_JOB_FETCH,
                                  FONT_IMPORT_CANCELLING) ==
                FONT_IMPORT_PANEL_CANCELLING);
    EXPECT_TRUE(font_import_panel(true, true, FONT_IMPORT_JOB_FETCH,
                                  FONT_IMPORT_FAILED) == FONT_IMPORT_PANEL_FAILED);
    EXPECT_TRUE(font_import_panel(true, true, FONT_IMPORT_JOB_CATALOGUE,
                                  FONT_IMPORT_TIMED_OUT) ==
                FONT_IMPORT_PANEL_FAILED);

    // Cancelling a download keeps the list that was already loaded. The user
    // stopped one request; they did not ask to start over.
    EXPECT_TRUE(font_import_panel(true, true, FONT_IMPORT_JOB_FETCH,
                                  FONT_IMPORT_CANCELLED) ==
                FONT_IMPORT_PANEL_BROWSING);
    EXPECT_TRUE(font_import_panel(true, false, FONT_IMPORT_JOB_NONE,
                                  FONT_IMPORT_IDLE) == FONT_IMPORT_PANEL_LOADING);
}

TEST(font_import_only_accepts_the_result_of_the_job_being_waited_on)
{
    uint64_t nonce = font_import_next_nonce(0);
    EXPECT_EQ_U64(nonce, 1);
    EXPECT_TRUE(font_import_result_is_current(nonce, nonce));

    // The user cancels one download and starts another. The first worker was
    // already writing; its artifact must not become the caption face.
    uint64_t superseded = nonce;
    nonce = font_import_next_nonce(nonce);
    EXPECT_TRUE(!font_import_result_is_current(superseded, nonce));
    EXPECT_TRUE(font_import_result_is_current(nonce, nonce));

    // Zero is never a live job, so a zeroed structure cannot accept anything.
    EXPECT_TRUE(!font_import_result_is_current(0, 0));
    EXPECT_TRUE(!font_import_result_is_current(0, nonce));
    EXPECT_TRUE(!font_import_result_is_current(nonce, 0));

    // Wrapping skips zero rather than reusing it as a live nonce.
    EXPECT_EQ_U64(font_import_next_nonce(UINT64_MAX), 1);
}

TEST(font_import_clears_a_good_outcome_on_its_own_but_never_a_failure)
{
    double finished = 1000.0;
    EXPECT_TRUE(!font_import_outcome_expired(FONT_IMPORT_SUCCEEDED, finished,
                                             finished));
    EXPECT_TRUE(font_import_outcome_expired(
        FONT_IMPORT_SUCCEEDED, finished,
        finished + FONT_IMPORT_SUCCESS_LINGER_SECONDS));
    EXPECT_TRUE(font_import_outcome_expired(
        FONT_IMPORT_CANCELLED, finished,
        finished + FONT_IMPORT_SUCCESS_LINGER_SECONDS));

    // A failure is the one thing worth reading. It waits to be dismissed.
    EXPECT_TRUE(!font_import_outcome_expired(FONT_IMPORT_FAILED, finished, 1e9));
    EXPECT_TRUE(!font_import_outcome_expired(FONT_IMPORT_TIMED_OUT, finished, 1e9));
    EXPECT_TRUE(!font_import_outcome_expired(FONT_IMPORT_RUNNING, finished, 1e9));
    EXPECT_TRUE(!font_import_outcome_expired(FONT_IMPORT_SUCCEEDED, finished, 0.0));
}

TEST(font_import_job_activity_and_completion_partition_every_state)
{
    static const Font_Import_Job_State states[] = {
        FONT_IMPORT_IDLE, FONT_IMPORT_RUNNING, FONT_IMPORT_CANCELLING,
        FONT_IMPORT_SUCCEEDED, FONT_IMPORT_FAILED, FONT_IMPORT_CANCELLED,
        FONT_IMPORT_TIMED_OUT,
    };
    size_t active = 0;
    size_t finished = 0;
    for (size_t i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
        bool is_active = font_import_job_is_active(states[i]);
        bool is_finished = font_import_job_is_finished(states[i]);
        // Nothing may be both, or a poll would try to cancel a job that has
        // already published its result.
        EXPECT_TRUE(!(is_active && is_finished));
        active += is_active ? 1u : 0u;
        finished += is_finished ? 1u : 0u;
    }
    EXPECT_EQ_SIZE(active, 2);
    EXPECT_EQ_SIZE(finished, 4);
    EXPECT_TRUE(!font_import_job_is_active(FONT_IMPORT_IDLE));
    EXPECT_TRUE(!font_import_job_is_finished(FONT_IMPORT_IDLE));
}
