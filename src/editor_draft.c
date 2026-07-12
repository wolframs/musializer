#include "editor_draft.h"

#include <string.h>

bool editor_lyric_draft_is_dirty(const Editor_Lyric_Draft_State *state)
{
    if (state == NULL) return false;
    if (state->is_new) return true;
    if (state->selected_id == 0) return false;
    if (!state->canonical_exists || state->canonical_text == NULL ||
        state->draft_text == NULL) return true;
    return state->canonical_start_seconds != state->draft_start_seconds ||
           state->canonical_end_seconds != state->draft_end_seconds ||
           strcmp(state->canonical_text, state->draft_text) != 0;
}
