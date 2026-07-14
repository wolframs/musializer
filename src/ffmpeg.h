#ifndef FFMPEG_H_
#define FFMPEG_H_

#include <stddef.h>
#include <stdbool.h>

#include "render_export.h"

typedef struct FFMPEG FFMPEG;

// Cheap preflight used before showing the destination picker. Encoding still
// validates startup independently to avoid time-of-check/time-of-use assumptions.
bool ffmpeg_available(void);
FFMPEG *ffmpeg_start_rendering(const char *output_path,
                               const Render_Export_Config *config,
                               const char *sound_file_path,
                               uint64_t total_frames,
                               uint64_t job_nonce);
bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height);
// Always consumes ffmpeg. Intentional termination during cancellation is not an error.
bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel);

#endif // FFMPEG_H_
