#include "track_identity.h"

#include <stddef.h>

const char *track_identity_display_name(const char *project_title,
                                        const char *audio_path)
{
    if (project_title != NULL && project_title[0] != '\0') return project_title;
    if (audio_path == NULL || audio_path[0] == '\0') return "";

    // Both separators are checked on every platform: a project written on one
    // host is expected to open on another, and the stored asset path travels
    // with it.
    const char *name = audio_path;
    for (const char *cursor = audio_path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    }
    // A path ending in a separator has no file name component. Returning the
    // empty tail is honest; falling back to the whole path would print a
    // directory chain into a control sized for a label.
    return name;
}
