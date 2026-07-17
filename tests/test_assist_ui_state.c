#include "assist_ui_state.h"
#include "test_support.h"

#include <string.h>

TEST(assist_start_guards_report_one_truthful_reason)
{
    EXPECT_TRUE(assist_start_block(false, ASSIST_JOB_IDLE, false) ==
                ASSIST_START_HELPER_UNAVAILABLE);
    EXPECT_TRUE(assist_start_block(true, ASSIST_JOB_RUNNING, false) ==
                ASSIST_START_JOB_ACTIVE);
    EXPECT_TRUE(assist_start_block(true, ASSIST_JOB_CANCELLING, false) ==
                ASSIST_START_JOB_ACTIVE);
    EXPECT_TRUE(assist_start_block(true, ASSIST_JOB_TIMING_OUT, false) ==
                ASSIST_START_JOB_ACTIVE);
    EXPECT_TRUE(assist_start_block(true, ASSIST_JOB_FAILING, false) ==
                ASSIST_START_JOB_ACTIVE);
    EXPECT_TRUE(assist_start_block(false, ASSIST_JOB_RUNNING, false) ==
                ASSIST_START_JOB_ACTIVE);
    EXPECT_TRUE(assist_start_block(true, ASSIST_JOB_FAILED, true) ==
                ASSIST_START_RESULT_PENDING);
    EXPECT_TRUE(assist_start_block(false, ASSIST_JOB_FAILED, true) ==
                ASSIST_START_RESULT_PENDING);
    EXPECT_TRUE(assist_start_block(true, ASSIST_JOB_CANCELLED, false) ==
                ASSIST_START_ALLOWED);
    EXPECT_TRUE(strlen(assist_start_block_reason(ASSIST_START_JOB_ACTIVE)) > 0);
}

TEST(assist_panel_content_precedence_matches_the_job_lifecycle)
{
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_IDLE, false, false) ==
                ASSIST_PANEL_READY);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_FAILED, true, false) ==
                ASSIST_PANEL_CONFIRMATION);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_RUNNING, true, false) ==
                ASSIST_PANEL_RUNNING);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_CANCELLING, true, false) ==
                ASSIST_PANEL_CANCELLING);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_TIMING_OUT, true, false) ==
                ASSIST_PANEL_CANCELLING);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_RUNNING, true, true) ==
                ASSIST_PANEL_CANDIDATE);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_SUCCEEDED, false, false) ==
                ASSIST_PANEL_EMPTY);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_FAILED, false, false) ==
                ASSIST_PANEL_EMPTY);
    EXPECT_TRUE(assist_panel_content(ASSIST_JOB_TIMED_OUT, false, false) ==
                ASSIST_PANEL_EMPTY);
}

TEST(assist_deadline_is_job_wide_monotonic_and_exact)
{
    EXPECT_FALSE(assist_job_deadline_expired(
        ASSIST_JOB_RUNNING, 10.0, 10.0 + ASSIST_JOB_TIMEOUT_SECONDS - 0.001));
    EXPECT_TRUE(assist_job_deadline_expired(
        ASSIST_JOB_RUNNING, 10.0, 10.0 + ASSIST_JOB_TIMEOUT_SECONDS));
    EXPECT_FALSE(assist_job_deadline_expired(
        ASSIST_JOB_CANCELLING, 10.0, 10.0 + ASSIST_JOB_TIMEOUT_SECONDS));
    EXPECT_FALSE(assist_job_deadline_expired(ASSIST_JOB_RUNNING, 10.0, 9.0));
    EXPECT_NEAR(assist_job_deadline_remaining(
                    ASSIST_JOB_RUNNING, 10.0, 610.0),
                1800.0, 0.0);
    EXPECT_NEAR(assist_job_deadline_remaining(
                    ASSIST_JOB_RUNNING, 10.0,
                    10.0 + ASSIST_JOB_TIMEOUT_SECONDS + 1.0),
                0.0, 0.0);
}

TEST(assist_modes_expose_real_workflow_and_data_boundary_copy)
{
    for (Assist_Mode mode = ASSIST_MODE_LYRICS;
         mode < ASSIST_MODE_COUNT; mode = (Assist_Mode)(mode + 1)) {
        EXPECT_TRUE(strlen(assist_mode_display_name(mode)) > 0);
        EXPECT_TRUE(strlen(assist_mode_argument(mode)) > 0);
        EXPECT_TRUE(strlen(assist_mode_badge(mode)) > 0);
        EXPECT_TRUE(strlen(assist_mode_workflow(mode)) > 0);
        EXPECT_TRUE(strlen(assist_mode_data_boundary(mode)) > 0);
    }
    EXPECT_TRUE(strstr(assist_mode_data_boundary(ASSIST_MODE_SECTIONS),
                       "locally") != NULL);
    EXPECT_TRUE(strstr(assist_mode_data_boundary(ASSIST_MODE_MIMO),
                       "OpenRouter") != NULL);
    EXPECT_TRUE(strstr(assist_mode_data_boundary(ASSIST_MODE_LYRICS),
                       "Codex") != NULL);
    EXPECT_TRUE(strstr(assist_mode_empty_result(ASSIST_MODE_LYRICS),
                       "no validated lyric cues") != NULL);
    EXPECT_TRUE(strstr(assist_mode_empty_result(ASSIST_MODE_ALL),
                       "no validated editor changes") != NULL);
}

TEST(assist_layout_fits_supported_small_and_large_windows)
{
    Assist_Ui_Layout small = assist_ui_layout(948.0f, ASSIST_PANEL_CANDIDATE);
    Assist_Ui_Layout large = assist_ui_layout(1908.0f, ASSIST_PANEL_CANDIDATE);
    EXPECT_EQ_SIZE(small.mode_columns, 4);
    EXPECT_EQ_SIZE(small.mode_rows, 1);
    EXPECT_EQ_SIZE(large.mode_columns, 4);
    EXPECT_TRUE(small.required_height <= 282.0f);
    EXPECT_NEAR(small.required_height, large.required_height, 0.0f);

    // The supported minimum is 960x640 with a 50 px toolbar. The Assist panel
    // still receives enough room for its longest staged-result state.
    float timeline = assist_timeline_height(640.0f, 50.0f,
                                            small.required_height);
    EXPECT_TRUE(timeline <= 440.0f);
    EXPECT_TRUE(timeline - 158.0f >= small.required_height - 0.01f);
}

TEST(assist_layout_degrades_to_two_columns_when_embedded_narrowly)
{
    Assist_Ui_Layout narrow = assist_ui_layout(620.0f,
                                               ASSIST_PANEL_CONFIRMATION);
    EXPECT_EQ_SIZE(narrow.mode_columns, 2);
    EXPECT_EQ_SIZE(narrow.mode_rows, 2);
    EXPECT_TRUE(narrow.status_y > 175.0f);
    EXPECT_TRUE(narrow.required_height >
                assist_ui_layout(948.0f, ASSIST_PANEL_CONFIRMATION).required_height);
}

TEST(assist_never_replaces_an_active_authored_lyric_draft)
{
    EXPECT_TRUE(assist_candidate_conflicts_with_lyric_draft(true, true, true));
    EXPECT_FALSE(assist_candidate_conflicts_with_lyric_draft(false, true, true));
    EXPECT_FALSE(assist_candidate_conflicts_with_lyric_draft(true, false, true));
    EXPECT_FALSE(assist_candidate_conflicts_with_lyric_draft(true, true, false));
}

TEST(assist_empty_results_are_not_applyable_candidates)
{
    EXPECT_FALSE(assist_result_has_changes(1u, 0u));
    EXPECT_FALSE(assist_result_has_changes(1u, 2u));
    EXPECT_TRUE(assist_result_has_changes(1u, 1u));
    EXPECT_TRUE(assist_result_has_changes(7u, 4u));
}
