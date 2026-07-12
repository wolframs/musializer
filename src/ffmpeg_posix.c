#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <raylib.h>

#include "ffmpeg.h"
#include "render_export.h"

#define READ_END 0
#define WRITE_END 1

struct FFMPEG {
    int pipe;
    pid_t pid;
    char *output_path;
    char *temporary_path;
};

static void ffmpeg_free_paths(FFMPEG *ffmpeg)
{
    free(ffmpeg->output_path);
    free(ffmpeg->temporary_path);
}

static bool remove_temporary_output(const char *path)
{
    if (remove(path) == 0 || errno == ENOENT) return true;
    TraceLog(LOG_ERROR, "FFMPEG: could not remove temporary output %s: %s",
             path, strerror(errno));
    return false;
}

// 1 means reaped, 0 means deadline expired, -1 means the wait failed.
static int waitpid_bounded(pid_t pid, bool cancellation, int *wstatus)
{
    struct timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) return -1;
    for (;;) {
        pid_t waited = waitpid(pid, wstatus, WNOHANG);
        if (waited == pid) return 1;
        if (waited < 0 && errno != EINTR) return -1;

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
        uint64_t elapsed_ms = (uint64_t)(now.tv_sec - started.tv_sec)*UINT64_C(1000);
        long nanoseconds = now.tv_nsec - started.tv_nsec;
        if (nanoseconds < 0) {
            elapsed_ms -= 1000;
            nanoseconds += 1000*1000*1000L;
        }
        elapsed_ms += (uint64_t)nanoseconds/UINT64_C(1000000);
        if (render_export_wait_action(false, elapsed_ms, cancellation) ==
            RENDER_EXPORT_WAIT_TIMEOUT) return 0;

        struct timespec pause = {.tv_sec = 0, .tv_nsec = 20*1000*1000L};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
    }
}

FFMPEG *ffmpeg_start_rendering(const char *output_path,
                               const Render_Export_Config *config,
                               const char *sound_file_path,
                               uint64_t total_frames,
                               uint64_t job_nonce)
{
    if (output_path == NULL || sound_file_path == NULL ||
        render_export_config_validate(config) != RENDER_EXPORT_OK ||
        total_frames == 0 || job_nonce == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: invalid rendering parameters");
        return NULL;
    }

    char resolution[64];
    char framerate[64];
    char duration[64];
    int resolution_length = snprintf(resolution, sizeof(resolution), "%ux%u",
                                     config->width, config->height);
    int framerate_length = snprintf(framerate, sizeof(framerate), "%u", config->fps);
    if (resolution_length < 0 || (size_t)resolution_length >= sizeof(resolution) ||
        framerate_length < 0 || (size_t)framerate_length >= sizeof(framerate) ||
        render_export_transport_duration_text(total_frames, config->fps,
                                              duration, sizeof(duration)) !=
            RENDER_EXPORT_OK) {
        TraceLog(LOG_ERROR, "FFMPEG: could not format rendering parameters");
        return NULL;
    }

    FFMPEG *ffmpeg = malloc(sizeof(*ffmpeg));
    if (ffmpeg == NULL) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate process state");
        return NULL;
    }
    memset(ffmpeg, 0, sizeof(*ffmpeg));
    ffmpeg->output_path = strdup(output_path);
    if (ffmpeg->output_path == NULL ||
        !render_export_temporary_path(output_path, (uint64_t)getpid(), job_nonce,
                                      &ffmpeg->temporary_path)) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate transactional output paths");
        ffmpeg_free_paths(ffmpeg);
        free(ffmpeg);
        return NULL;
    }

    int pipefd[2];

    if (pipe(pipefd) < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create a pipe: %s", strerror(errno));
        ffmpeg_free_paths(ffmpeg);
        free(ffmpeg);
        return NULL;
    }

    const char *preset = config->quality == RENDER_QUALITY_BALANCED ? "fast" : "slow";
    const char *crf = config->quality == RENDER_QUALITY_BALANCED ? "20" :
                      config->quality == RENDER_QUALITY_HIGH ? "16" : "12";
    const char *audio_bitrate = config->quality == RENDER_QUALITY_MASTER ? "320k" : "256k";

    pid_t child = fork();
    if (child < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not fork a child: %s", strerror(errno));
        close(pipefd[READ_END]);
        close(pipefd[WRITE_END]);
        ffmpeg_free_paths(ffmpeg);
        free(ffmpeg);
        return NULL;
    }

    if (child == 0) {
        if (pipefd[READ_END] != STDIN_FILENO) {
            while (dup2(pipefd[READ_END], STDIN_FILENO) < 0) {
                if (errno == EINTR) continue;
                _exit(126);
            }
            close(pipefd[READ_END]);
        }
        close(pipefd[WRITE_END]);

        execlp("ffmpeg",
            "ffmpeg",
            "-loglevel", "warning",
            "-y",

            "-f", "rawvideo",
            "-pix_fmt", "rgba",
            "-s", resolution,
            "-framerate", framerate,
            "-i", "-",
            "-i", sound_file_path,

            "-map", "0:v:0",
            "-map", "1:a:0",
            "-c:v", "libx264",
            "-preset", preset,
            "-crf", crf,
            "-profile:v", "high",
            "-x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709",
            "-c:a", "aac",
            "-b:a", audio_bitrate,
            // Keep every deterministic video frame. The decoded PCM may end
            // within the last frame, so pad only that sub-frame tail before
            // letting the video stream define the final container duration.
            "-af", "apad",
            "-pix_fmt", "yuv420p",
            "-color_primaries", "bt709",
            "-color_trc", "bt709",
            "-colorspace", "bt709",
            "-movflags", "+faststart",
            "-t", duration,
            ffmpeg->temporary_path,

            NULL
        );
        _exit(127);
    }

    if (close(pipefd[READ_END]) < 0) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close read end of the pipe on the parent's end: %s", strerror(errno));
    }

    ffmpeg->pid = child;
    ffmpeg->pipe = pipefd[WRITE_END];
    return ffmpeg;
}

bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel)
{
    if (ffmpeg == NULL) return false;

    int pipe = ffmpeg->pipe;
    pid_t pid = ffmpeg->pid;
    char *output_path = ffmpeg->output_path;
    char *temporary_path = ffmpeg->temporary_path;
    bool transport_ok = true;
    bool cancel_sent = false;

    free(ffmpeg);

    if (close(pipe) < 0) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close write end of the pipe on the parent's end: %s", strerror(errno));
        transport_ok = false;
    }

    if (cancel) {
        if (kill(pid, SIGKILL) == 0) {
            cancel_sent = true;
        } else if (errno != ESRCH) {
            TraceLog(LOG_ERROR, "FFMPEG: could not terminate ffmpeg child process: %s", strerror(errno));
            transport_ok = false;
        }
    }

    int wstatus = 0;
    int waited = waitpid_bounded(pid, cancel, &wstatus);
    if (waited != 1) {
        if (waited == 0) {
            TraceLog(LOG_ERROR,
                     "FFMPEG: encoder did not %s within the %u ms grace period; terminating it",
                     cancel ? "cancel" : "finalize", render_export_finalize_grace_ms(cancel));
        } else {
            TraceLog(LOG_ERROR, "FFMPEG: could not wait for encoder: %s; terminating it",
                     strerror(errno));
        }
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            TraceLog(LOG_ERROR, "FFMPEG: could not force-terminate encoder: %s", strerror(errno));
        }
        int forced = waitpid_bounded(pid, true, &wstatus);
        if (forced != 1) {
            TraceLog(LOG_ERROR,
                     "FFMPEG: encoder could not be reaped; prior output is preserved and in-progress file remains at %s",
                     temporary_path);
            free(output_path);
            free(temporary_path);
            return false;
        }
        (void)remove_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }

    for (;;) {
        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (cancel) {
                bool cleaned = remove_temporary_output(temporary_path);
                free(output_path);
                free(temporary_path);
                return transport_ok && cleaned &&
                       (cancel_sent || exit_status == 0);
            }
            if (exit_status != 0) {
                TraceLog(LOG_ERROR, "FFMPEG: ffmpeg exited with code %d", exit_status);
                (void)remove_temporary_output(temporary_path);
                free(output_path);
                free(temporary_path);
                return false;
            }
            if (!transport_ok) {
                (void)remove_temporary_output(temporary_path);
                free(output_path);
                free(temporary_path);
                return false;
            }
            if (rename(temporary_path, output_path) != 0) {
                TraceLog(LOG_ERROR,
                         "FFMPEG: could not publish completed output %s: %s (encoded file remains at %s)",
                         output_path, strerror(errno), temporary_path);
                free(output_path);
                free(temporary_path);
                return false;
            }
            free(output_path);
            free(temporary_path);
            return true;
        }

        if (WIFSIGNALED(wstatus)) {
            if (cancel && cancel_sent && WTERMSIG(wstatus) == SIGKILL) {
                bool cleaned = remove_temporary_output(temporary_path);
                free(output_path);
                free(temporary_path);
                return transport_ok && cleaned;
            }
            TraceLog(LOG_ERROR, "FFMPEG: ffmpeg got terminated by %s", strsignal(WTERMSIG(wstatus)));
            (void)remove_temporary_output(temporary_path);
            free(output_path);
            free(temporary_path);
            return false;
        }
        TraceLog(LOG_ERROR, "FFMPEG: encoder ended in an unexpected process state");
        (void)remove_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }

    assert(0 && "unreachable");
}

bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height)
{
    if (ffmpeg == NULL || data == NULL || width == 0 ||
        width > SIZE_MAX/sizeof(uint32_t) || height > SIZE_MAX/width) {
        TraceLog(LOG_ERROR, "FFMPEG: invalid frame parameters");
        return false;
    }

    const size_t row_size = sizeof(uint32_t)*width;
    for (size_t y = height; y > 0; --y) {
        const uint8_t *row = (const uint8_t *)data + (y - 1)*row_size;
        size_t remaining = row_size;
        while (remaining > 0) {
            size_t chunk = remaining;
#ifdef SSIZE_MAX
            if (chunk > (size_t)SSIZE_MAX) chunk = (size_t)SSIZE_MAX;
#endif
            ssize_t written = write(ffmpeg->pipe, row, chunk);
            if (written < 0) {
                if (errno == EINTR) continue;
                TraceLog(LOG_ERROR, "FFMPEG: failed to write into ffmpeg pipe: %s", strerror(errno));
                return false;
            }
            if (written == 0) {
                TraceLog(LOG_ERROR, "FFMPEG: write into ffmpeg pipe made no progress");
                return false;
            }
            row += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    return true;
}
