#ifndef MUSIALIZER_TRACK_IDENTITY_H_
#define MUSIALIZER_TRACK_IDENTITY_H_

// Human identity of a track, for anything a user reads.
//
// Saving imports the source audio into the content-addressed
// `<stem>.assets/audio/<sha256>.<ext>` bundle, so from that point the audio
// path's file name is a SHA-256 and is useless as a label. The project title
// is the identity that survives open and save, so it wins whenever it exists.
//
// `project_title` is NULL or empty for audio that was never saved as a
// project; `audio_path` may be NULL. The result aliases one of the arguments
// (or a literal) and is never NULL, so it is valid for as long as the argument
// it aliases. Nothing is copied and nothing is allocated.
const char *track_identity_display_name(const char *project_title,
                                        const char *audio_path);

#endif // MUSIALIZER_TRACK_IDENTITY_H_
