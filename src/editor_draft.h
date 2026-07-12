#ifndef MUSIALIZER_EDITOR_DRAFT_H_
#define MUSIALIZER_EDITOR_DRAFT_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool is_new;
    uint64_t selected_id;

    bool canonical_exists;
    double canonical_start_seconds;
    double canonical_end_seconds;
    const char *canonical_text;

    double draft_start_seconds;
    double draft_end_seconds;
    const char *draft_text;
} Editor_Lyric_Draft_State;

// Reports whether changing workspace context would discard an authored lyric
// change. A missing selected cue is considered dirty so callers fail safe if
// the underlying document changed while the editor was open.
bool editor_lyric_draft_is_dirty(const Editor_Lyric_Draft_State *state);

#endif // MUSIALIZER_EDITOR_DRAFT_H_
