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

struct FFMPEG {
    HANDLE hProcess;
    HANDLE hPipeWrite;
};

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

FFMPEG *ffmpeg_start_rendering(const char *output_path, size_t width, size_t height, size_t fps, const char *sound_file_path)
{
    if (output_path == NULL || sound_file_path == NULL || width == 0 || height == 0 || fps == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: invalid rendering parameters");
        return NULL;
    }

    FFMPEG *ffmpeg = malloc(sizeof(*ffmpeg));
    if (ffmpeg == NULL) {
        TraceLog(LOG_ERROR, "FFMPEG: could not allocate process state");
        return NULL;
    }

    HANDLE pipe_read = NULL;
    HANDLE pipe_write = NULL;

    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&pipe_read, &pipe_write, &saAttr, 0)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create pipe. System Error Code: %d", GetLastError());
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
    int resolution_length = snprintf(resolution, sizeof(resolution), "%zux%zu", width, height);
    int framerate_length = snprintf(framerate, sizeof(framerate), "%zu", fps);
    if (resolution_length < 0 || (size_t)resolution_length >= sizeof(resolution) ||
        framerate_length < 0 || (size_t)framerate_length >= sizeof(framerate)) {
        TraceLog(LOG_ERROR, "FFMPEG: could not format rendering parameters");
        goto fail;
    }

    const char *args[] = {
        "ffmpeg.exe", "-loglevel", "warning", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgba", "-s", resolution,
        "-r", framerate, "-i", "-", "-i", sound_file_path,
        "-c:v", "libx264", "-vb", "2500k", "-c:a", "aac",
        "-ab", "200k", "-pix_fmt", "yuv420p", output_path,
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
    bool transport_ok = true;
    bool cancel_sent = false;
    free(ffmpeg);

    if (!cancel && !FlushFileBuffers(hPipeWrite)) {
        TraceLog(LOG_WARNING, "FFMPEG: could not flush frame pipe. System Error Code: %d", GetLastError());
        transport_ok = false;
    }
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

    DWORD wait_result = WaitForSingleObject(hProcess, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not wait on child process. System Error Code: %d", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    DWORD exit_status;
    if (GetExitCodeProcess(hProcess, &exit_status) == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not get process exit code. System Error Code: %d", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    if (cancel && cancel_sent && exit_status == 69) {
        CloseHandle(hProcess);
        return transport_ok;
    }

    if (exit_status != 0) {
        TraceLog(LOG_ERROR, "FFMPEG: command exited with exit code %lu", exit_status);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hProcess);

    return transport_ok;
}

// TODO: where can we find this symbol for the Windows build?
void __imp__wassert() {}
