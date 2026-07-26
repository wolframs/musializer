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
    Assist_Ui_Layout small = assist_ui_layout(948.0f, ASSIST_PANEL_CANDIDATE, false);
    Assist_Ui_Layout large = assist_ui_layout(1908.0f, ASSIST_PANEL_CANDIDATE, false);
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
                                               ASSIST_PANEL_CONFIRMATION, false);
    EXPECT_EQ_SIZE(narrow.mode_columns, 2);
    EXPECT_EQ_SIZE(narrow.mode_rows, 2);
    EXPECT_TRUE(narrow.status_y > 175.0f);
    EXPECT_TRUE(narrow.required_height >
                assist_ui_layout(948.0f, ASSIST_PANEL_CONFIRMATION, false).required_height);
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

TEST(assist_lyric_reference_row_only_grows_the_step_that_shows_it)
{
    // The row belongs to the review step, where the user decides. Every other
    // panel state must be exactly the height it was.
    static const Assist_Panel_Content others[] = {
        ASSIST_PANEL_READY, ASSIST_PANEL_RUNNING, ASSIST_PANEL_CANCELLING,
        ASSIST_PANEL_CANDIDATE, ASSIST_PANEL_EMPTY,
    };
    for (size_t i = 0; i < sizeof(others)/sizeof(others[0]); ++i) {
        EXPECT_NEAR(assist_ui_layout(948.0f, others[i], true).required_height,
                    assist_ui_layout(948.0f, others[i], false).required_height,
                    0.0f);
        EXPECT_NEAR(assist_ui_layout(948.0f, others[i], true).reference_y,
                    0.0f, 0.0f);
    }

    Assist_Ui_Layout without = assist_ui_layout(948.0f, ASSIST_PANEL_CONFIRMATION,
                                                false);
    Assist_Ui_Layout with = assist_ui_layout(948.0f, ASSIST_PANEL_CONFIRMATION,
                                             true);
    EXPECT_NEAR(without.reference_y, 0.0f, 0.0f);
    EXPECT_TRUE(with.reference_y > with.content_y);
    EXPECT_TRUE(with.required_height > without.required_height);
    // The row must sit inside the panel it grew, not past the bottom of it.
    EXPECT_TRUE(with.reference_y + 34.0f <= with.required_height);

    // And the taller panel must still fit the supported minimum window.
    float timeline = assist_timeline_height(640.0f, 50.0f, with.required_height);
    EXPECT_TRUE(timeline - 158.0f >= with.required_height - 0.01f);
}

TEST(assist_lyric_reference_is_offered_only_where_it_is_used)
{
    EXPECT_TRUE(assist_mode_uses_lyric_reference(ASSIST_MODE_LYRICS));
    EXPECT_TRUE(assist_mode_uses_lyric_reference(ASSIST_MODE_ALL));
    // Scene changes and MiMo never read authored lyrics, so offering the
    // control there would promise something the run does not do.
    EXPECT_FALSE(assist_mode_uses_lyric_reference(ASSIST_MODE_SECTIONS));
    EXPECT_FALSE(assist_mode_uses_lyric_reference(ASSIST_MODE_MIMO));

    for (int r = ASSIST_LYRIC_REFERENCE_NONE; r <= ASSIST_LYRIC_REFERENCE_CHOSEN; ++r) {
        const char *summary = assist_lyric_reference_summary((Assist_Lyric_Reference)r);
        EXPECT_TRUE(summary != NULL && summary[0] != '\0');
    }
    // Whether an embedded lyrics tag exists needs ffprobe, so the "none" case
    // must not claim transcription outright.
    EXPECT_TRUE(strstr(assist_lyric_reference_summary(ASSIST_LYRIC_REFERENCE_NONE),
                       "unless") != NULL);
}

TEST(assist_lyric_sibling_path_matches_the_helpers_stem_rule)
{
    char path[256];
    static const struct { const char *audio; const char *sibling; } cases[] = {
        {"kitty.mp3",              "kitty.lyrics.txt"},
        {"/music/kitty.mp3",       "/music/kitty.lyrics.txt"},
        {"/music/a.b.mp3",         "/music/a.b.lyrics.txt"},
        // No extension: pathlib's stem is the whole name.
        {"kitty",                  "kitty.lyrics.txt"},
        // A leading dot is part of the name, not an extension, so ".mp3"
        // becomes ".mp3.lyrics.txt" rather than ".lyrics.txt".
        {".mp3",                   ".mp3.lyrics.txt"},
        {"/music/.mp3",            "/music/.mp3.lyrics.txt"},
        // A dot in a parent directory must not be mistaken for the extension.
        {"/my.music/kitty",        "/my.music/kitty.lyrics.txt"},
        {"C:\\my.music\\kitty",    "C:\\my.music\\kitty.lyrics.txt"},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        memset(path, 'x', sizeof(path));
        EXPECT_TRUE(assist_lyric_sibling_path(cases[i].audio, path, sizeof(path)));
        EXPECT_TRUE(strcmp(path, cases[i].sibling) == 0);
    }

    EXPECT_FALSE(assist_lyric_sibling_path(NULL, path, sizeof(path)));
    EXPECT_FALSE(assist_lyric_sibling_path("", path, sizeof(path)));
    EXPECT_FALSE(assist_lyric_sibling_path("kitty.mp3", NULL, sizeof(path)));
    EXPECT_FALSE(assist_lyric_sibling_path("kitty.mp3", path, 0));

    // Exactly enough room, and one byte short of it.
    char exact[sizeof("kitty.lyrics.txt")];
    EXPECT_TRUE(assist_lyric_sibling_path("kitty.mp3", exact, sizeof(exact)));
    EXPECT_TRUE(strcmp(exact, "kitty.lyrics.txt") == 0);
    char tight[sizeof("kitty.lyrics.txt") - 1];
    memset(tight, 'x', sizeof(tight));
    EXPECT_FALSE(assist_lyric_sibling_path("kitty.mp3", tight, sizeof(tight)));
    for (size_t i = 0; i < sizeof(tight); ++i) EXPECT_TRUE(tight[i] == 'x');
}
