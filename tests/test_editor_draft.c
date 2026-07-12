#include "editor_draft.h"
#include "test_support.h"

static Editor_Lyric_Draft_State selected(void)
{
    return (Editor_Lyric_Draft_State) {
        .selected_id = 42,
        .canonical_exists = true,
        .canonical_start_seconds = 1.25,
        .canonical_end_seconds = 3.5,
        .canonical_text = "same words",
        .draft_start_seconds = 1.25,
        .draft_end_seconds = 3.5,
        .draft_text = "same words",
    };
}

TEST(editor_draft_clean_states_allow_context_changes)
{
    Editor_Lyric_Draft_State none = {0};
    Editor_Lyric_Draft_State unchanged = selected();
    EXPECT_FALSE(editor_lyric_draft_is_dirty(NULL));
    EXPECT_FALSE(editor_lyric_draft_is_dirty(&none));
    EXPECT_FALSE(editor_lyric_draft_is_dirty(&unchanged));
}

TEST(editor_draft_new_cue_is_always_protected)
{
    Editor_Lyric_Draft_State draft = { .is_new = true };
    EXPECT_TRUE(editor_lyric_draft_is_dirty(&draft));
}

TEST(editor_draft_detects_each_authored_change)
{
    Editor_Lyric_Draft_State draft = selected();
    draft.draft_start_seconds += 0.1;
    EXPECT_TRUE(editor_lyric_draft_is_dirty(&draft));

    draft = selected();
    draft.draft_end_seconds -= 0.1;
    EXPECT_TRUE(editor_lyric_draft_is_dirty(&draft));

    draft = selected();
    draft.draft_text = "different words";
    EXPECT_TRUE(editor_lyric_draft_is_dirty(&draft));
}

TEST(editor_draft_fails_safe_when_selected_cue_disappears)
{
    Editor_Lyric_Draft_State draft = selected();
    draft.canonical_exists = false;
    EXPECT_TRUE(editor_lyric_draft_is_dirty(&draft));
    draft.canonical_exists = true;
    draft.canonical_text = NULL;
    EXPECT_TRUE(editor_lyric_draft_is_dirty(&draft));
}
