#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#define _WINUSER_
#define _WINGDI_
#define _IMM_
#define _WINCON_
#include <windows.h>

#include <raylib.h>

#include "ffmpeg.h"
#include "render_export.h"

struct FFMPEG {
    HANDLE hProcess;
    HANDLE hPipeWrite;
    char *output_path;
    char *temporary_path;
};

static char *duplicate_string(const char *value)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, value, length + 1);
    return copy;
}

static void ffmpeg_free_paths(FFMPEG *ffmpeg)
{
    free(ffmpeg->output_path);
    free(ffmpeg->temporary_path);
}

static bool delete_temporary_output(const char *path)
{
    if (DeleteFileA(path)) return true;
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    TraceLog(LOG_ERROR,
             "FFMPEG: could not remove temporary output %s. System Error Code: %lu",
             path, (unsigned long)error);
    return false;
}

static bool append_command_char(char *buffer, size_t capacity, size_t *length, char value)
{
    if (*length + 1 >= capacity) return false;
    buffer[(*length)++] = value;
    buffer[*length] = '\0';
    return true;
}

// Quote one argument according to the parsing rules used by the Microsoft C runtime.
static bool append_command_arg(char *buffer, size_t capacity, size_t *length, const char *arg)
{
    if (!append_command_char(buffer, capacity, length, '"')) return false;

    const char *cursor = arg;
    for (;;) {
        size_t backslashes = 0;
        while (*cursor == '\\') {
            ++backslashes;
            ++cursor;
        }

        if (*cursor == '"' || *cursor == '\0') {
            size_t count = backslashes*2 + (*cursor == '"');
            for (size_t i = 0; i < count; ++i) {
                if (!append_command_char(buffer, capacity, length, '\\')) return false;
            }
            if (*cursor == '\0') break;
            if (!append_command_char(buffer, capacity, length, '"')) return false;
            ++cursor;
        } else {
            for (size_t i = 0; i < backslashes; ++i) {
                if (!append_command_char(buffer, capacity, length, '\\')) return false;
            }
            if (!append_command_char(buffer, capacity, length, *cursor++)) return false;
        }
    }

    return append_command_char(buffer, capacity, length, '"') &&
           append_command_char(buffer, capacity, length, ' ');
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

    FFMPEG *ffmpeg = malloc(sizeof(*ffmpeg));
    if (ffmpeg == NULL) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate process state");
        return NULL;
    }
    memset(ffmpeg, 0, sizeof(*ffmpeg));
    ffmpeg->output_path = duplicate_string(output_path);
    if (ffmpeg->output_path == NULL ||
        !render_export_temporary_path(output_path,
                                      (uint64_t)GetCurrentProcessId(), job_nonce,
                                      &ffmpeg->temporary_path)) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate transactional output paths");
        ffmpeg_free_paths(ffmpeg);
        free(ffmpeg);
        return NULL;
    }

    HANDLE pipe_read = NULL;
    HANDLE pipe_write = NULL;

    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&pipe_read, &pipe_write, &saAttr, 0)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create pipe. System Error Code: %d", GetLastError());
        ffmpeg_free_paths(ffmpeg);
        free(ffmpeg);
        return NULL;
    }

    if (!SetHandleInformation(pipe_write, HANDLE_FLAG_INHERIT, 0)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not mark write pipe as non-inheritable. System Error Code: %d", GetLastError());
        goto fail;
    }

    // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output

    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);
    // NOTE: theoretically setting NULL to std handles should not be a problem
    // https://docs.microsoft.com/en-us/windows/console/getstdhandle?redirectedfrom=MSDN#attachdetach-behavior
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (siStartInfo.hStdError == INVALID_HANDLE_VALUE) {
        TraceLog(LOG_ERROR, "FFMPEG: Could get standard error handle for the child. System Error Code: %d", GetLastError());
        goto fail;
    }
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (siStartInfo.hStdOutput == INVALID_HANDLE_VALUE) {
        TraceLog(LOG_ERROR, "FFMPEG: Could get standard output handle for the child. System Error Code: %d", GetLastError());
        goto fail;
    }
    siStartInfo.hStdInput = pipe_read;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

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
        goto fail;
    }

    const char *preset = config->quality == RENDER_QUALITY_BALANCED ? "fast" : "slow";
    const char *crf = config->quality == RENDER_QUALITY_BALANCED ? "20" :
                      config->quality == RENDER_QUALITY_HIGH ? "16" : "12";
    const char *audio_bitrate = config->quality == RENDER_QUALITY_MASTER ? "320k" : "256k";
    const char *args[] = {
        "ffmpeg.exe", "-loglevel", "warning", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgba", "-s", resolution,
        "-framerate", framerate, "-i", "-", "-i", sound_file_path,
        "-map", "0:v:0", "-map", "1:a:0",
        "-c:v", "libx264", "-preset", preset, "-crf", crf,
        "-profile:v", "high",
        "-x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709",
        "-c:a", "aac", "-b:a", audio_bitrate, "-pix_fmt", "yuv420p",
        "-af", "apad",
        "-color_primaries", "bt709", "-color_trc", "bt709",
        "-colorspace", "bt709", "-movflags", "+faststart", "-t", duration,
        ffmpeg->temporary_path,
    };
    size_t command_capacity = 1;
    for (size_t i = 0; i < sizeof(args)/sizeof(args[0]); ++i) {
        size_t arg_length = strlen(args[i]);
        if (command_capacity > SIZE_MAX - 4 ||
            arg_length > (SIZE_MAX - command_capacity - 4)/2) {
            TraceLog(LOG_ERROR, "FFMPEG: command line is too long");
            goto fail;
        }
        command_capacity += arg_length*2 + 4;
    }
    char *cmd_buffer = malloc(command_capacity);
    if (cmd_buffer == NULL) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate command line");
        goto fail;
    }
    size_t command_length = 0;
    cmd_buffer[0] = '\0';
    for (size_t i = 0; i < sizeof(args)/sizeof(args[0]); ++i) {
        if (!append_command_arg(cmd_buffer, command_capacity, &command_length, args[i])) {
            TraceLog(LOG_ERROR, "FFMPEG: could not construct command line");
            free(cmd_buffer);
            goto fail;
        }
    }
    if (command_length > 0) cmd_buffer[command_length - 1] = '\0';

    if (!CreateProcessA(NULL, cmd_buffer, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create child process. System Error Code: %d", GetLastError());
        free(cmd_buffer);
        goto fail;
    }
    free(cmd_buffer);

    CloseHandle(pipe_read);
    CloseHandle(piProcInfo.hThread);

    ffmpeg->hProcess = piProcInfo.hProcess;
    ffmpeg->hPipeWrite = pipe_write;
    return ffmpeg;

fail:
    if (pipe_write != NULL) CloseHandle(pipe_write);
    if (pipe_read != NULL) CloseHandle(pipe_read);
    ffmpeg_free_paths(ffmpeg);
    free(ffmpeg);
    return NULL;
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
            DWORD chunk = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
            DWORD written = 0;
            if (!WriteFile(ffmpeg->hPipeWrite, row, chunk, &written, NULL)) {
                TraceLog(LOG_ERROR, "FFMPEG: failed to write into ffmpeg pipe. System Error Code: %d", GetLastError());
                return false;
            }
            if (written == 0) {
                TraceLog(LOG_ERROR, "FFMPEG: write into ffmpeg pipe made no progress");
                return false;
            }
            row += written;
            remaining -= written;
        }
    }
    return true;
}

bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel)
{
    if (ffmpeg == NULL) return false;

    HANDLE hPipeWrite = ffmpeg->hPipeWrite;
    HANDLE hProcess = ffmpeg->hProcess;
    char *output_path = ffmpeg->output_path;
    char *temporary_path = ffmpeg->temporary_path;
    bool transport_ok = true;
    bool cancel_sent = false;
    free(ffmpeg);

    // Closing the anonymous pipe is the EOF/finalize signal. FlushFileBuffers
    // can itself wait forever for a stalled reader, defeating bounded process
    // finalization.
    if (!CloseHandle(hPipeWrite)) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close frame pipe. System Error Code: %d", GetLastError());
        transport_ok = false;
    }

    if (cancel) {
        if (TerminateProcess(hProcess, 69)) {
            cancel_sent = true;
        } else if (GetLastError() != ERROR_ACCESS_DENIED) {
            TraceLog(LOG_ERROR, "FFMPEG: could not terminate child process. System Error Code: %d", GetLastError());
            transport_ok = false;
        }
    }

    DWORD grace_ms = render_export_finalize_grace_ms(cancel);
    DWORD wait_result = WaitForSingleObject(hProcess, grace_ms);
    if (wait_result != WAIT_OBJECT_0) {
        if (wait_result == WAIT_TIMEOUT) {
            TraceLog(LOG_ERROR,
                     "FFMPEG: encoder did not %s within the %lu ms grace period; terminating it",
                     cancel ? "cancel" : "finalize", (unsigned long)grace_ms);
        } else {
            TraceLog(LOG_ERROR,
                     "FFMPEG: could not wait on encoder. System Error Code: %d; terminating it",
                     GetLastError());
        }
        if (!TerminateProcess(hProcess, 70) && GetLastError() != ERROR_ACCESS_DENIED) {
            TraceLog(LOG_ERROR, "FFMPEG: could not force-terminate encoder. System Error Code: %d",
                     GetLastError());
        }
        DWORD forced = WaitForSingleObject(
            hProcess, render_export_finalize_grace_ms(true));
        if (forced != WAIT_OBJECT_0) {
            TraceLog(LOG_ERROR,
                     "FFMPEG: encoder could not be reaped; prior output is preserved and in-progress file remains at %s",
                     temporary_path);
            CloseHandle(hProcess);
            free(output_path);
            free(temporary_path);
            return false;
        }
        CloseHandle(hProcess);
        (void)delete_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }

    DWORD exit_status;
    if (GetExitCodeProcess(hProcess, &exit_status) == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not get process exit code. System Error Code: %d", GetLastError());
        CloseHandle(hProcess);
        (void)delete_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }

    if (cancel) {
        CloseHandle(hProcess);
        bool cleaned = delete_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return transport_ok && cleaned && (cancel_sent || exit_status == 0);
    }

    if (exit_status != 0) {
        TraceLog(LOG_ERROR, "FFMPEG: command exited with exit code %lu", exit_status);
        CloseHandle(hProcess);
        (void)delete_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }

    CloseHandle(hProcess);
    if (!transport_ok) {
        (void)delete_temporary_output(temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }
    if (!MoveFileExA(temporary_path, output_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        TraceLog(LOG_ERROR,
                 "FFMPEG: could not publish completed output. System Error Code: %d (encoded file remains at %s)",
                 GetLastError(), temporary_path);
        free(output_path);
        free(temporary_path);
        return false;
    }
    free(output_path);
    free(temporary_path);
    return true;
}

// TODO: where can we find this symbol for the Windows build?
void __imp__wassert() {}
