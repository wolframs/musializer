#include <string.h>

#include "test_support.h"
#include "track_identity.h"

#define EXPECT_NAME(title, path, expected)                                       \
    EXPECT_TRUE(strcmp(track_identity_display_name((title), (path)), (expected)) == 0)

// Regression: after a save the track rail, export summary, assist status, and
// applied-suggestion notices all showed the SHA-256 of the imported audio
// asset, because every one of them derived its label from the track's file
// path, which import rewrites to `<stem>.assets/audio/<sha256>.<ext>`.
TEST(track_identity_prefers_the_project_title_over_the_hashed_asset_path)
{
    const char *hashed =
        "demo.assets/audio/"
        "ec3646f6923d08996d02935214e43e458921a9dfe40fb5021cb02a5ad76abfeb.wav";
    EXPECT_NAME("demo", hashed, "demo");
    EXPECT_NAME("A Title With Spaces", hashed, "A Title With Spaces");
    // A title is used verbatim, never reinterpreted as a path.
    EXPECT_NAME("/not/a/path", "x.wav", "/not/a/path");
}

TEST(track_identity_falls_back_to_the_file_name_without_a_title)
{
    // Audio opened directly carries no project metadata yet.
    EXPECT_NAME(NULL, "/music/song.mp3", "song.mp3");
    EXPECT_NAME("", "/music/song.mp3", "song.mp3");
    EXPECT_NAME(NULL, "song.mp3", "song.mp3");
    // A project authored on Windows is expected to open on a POSIX host with
    // its stored asset path intact, and the reverse.
    EXPECT_NAME(NULL, "C:\\music\\song.mp3", "song.mp3");
    EXPECT_NAME(NULL, "a/b\\c/song.mp3", "song.mp3");
}

TEST(track_identity_never_returns_null_for_degenerate_input)
{
    EXPECT_NAME(NULL, NULL, "");
    EXPECT_NAME("", "", "");
    EXPECT_NAME(NULL, "", "");
    // A trailing separator has no file name component. The empty tail is
    // honest; the enclosing directory chain would overflow a label control.
    EXPECT_NAME(NULL, "/music/", "");
    EXPECT_NAME(NULL, "/", "");
}
