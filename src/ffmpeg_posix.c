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
#include <unistd.h>

#include <raylib.h>

#include "ffmpeg.h"

#define READ_END 0
#define WRITE_END 1

struct FFMPEG {
    int pipe;
    pid_t pid;
};

FFMPEG *ffmpeg_start_rendering(const char *output_path, size_t width, size_t height, size_t fps, const char *sound_file_path)
{
    if (output_path == NULL || sound_file_path == NULL || width == 0 || height == 0 || fps == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: invalid rendering parameters");
        return NULL;
    }

    char resolution[64];
    char framerate[64];
    int resolution_length = snprintf(resolution, sizeof(resolution), "%zux%zu", width, height);
    int framerate_length = snprintf(framerate, sizeof(framerate), "%zu", fps);
    if (resolution_length < 0 || (size_t)resolution_length >= sizeof(resolution) ||
        framerate_length < 0 || (size_t)framerate_length >= sizeof(framerate)) {
        TraceLog(LOG_ERROR, "FFMPEG: could not format rendering parameters");
        return NULL;
    }

    FFMPEG *ffmpeg = malloc(sizeof(*ffmpeg));
    if (ffmpeg == NULL) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate process state");
        return NULL;
    }

    int pipefd[2];

    if (pipe(pipefd) < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create a pipe: %s", strerror(errno));
        free(ffmpeg);
        return NULL;
    }

    pid_t child = fork();
    if (child < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not fork a child: %s", strerror(errno));
        close(pipefd[READ_END]);
        close(pipefd[WRITE_END]);
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
            "-r", framerate,
            "-i", "-",
            "-i", sound_file_path,

            "-c:v", "libx264",
            "-vb", "2500k",
            "-c:a", "aac",
            "-ab", "200k",
            "-pix_fmt", "yuv420p",
            output_path,

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

    for (;;) {
        int wstatus = 0;
        if (waitpid(pid, &wstatus, 0) < 0) {
            if (errno == EINTR) continue;
            TraceLog(LOG_ERROR, "FFMPEG: could not wait for ffmpeg child process to finish: %s", strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (cancel && cancel_sent) return transport_ok;
            if (exit_status != 0) {
                TraceLog(LOG_ERROR, "FFMPEG: ffmpeg exited with code %d", exit_status);
                return false;
            }

            return transport_ok;
        }

        if (WIFSIGNALED(wstatus)) {
            if (cancel && cancel_sent && WTERMSIG(wstatus) == SIGKILL) return transport_ok;
            TraceLog(LOG_ERROR, "FFMPEG: ffmpeg got terminated by %s", strsignal(WTERMSIG(wstatus)));
            return false;
        }
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
