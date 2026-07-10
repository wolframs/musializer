#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>

#include <raylib.h>

#ifndef _WIN32
#include <signal.h> // needed for sigaction()
#endif // _WIN32

#include "./hotreload.h"

int main(int argc, char **argv)
{
#ifndef _WIN32
    // NOTE: This is needed because if the pipe between Musializer and FFmpeg breaks
    // Musializer will receive SIGPIPE on trying to write into it. While such behavior
    // makes sense for command line utilities, Musializer is a relatively friendly GUI
    // application that is trying to recover from such situations.
    struct sigaction act = {0};
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
#endif // _WIN32

    if (!reload_libplug()) return 1;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_ALWAYS_RUN);
    size_t factor = 80;
    InitWindow(factor*16, factor*9, "Musializer");
    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "Could not initialize the rendering window");
        return 1;
    }
    {
        const char *file_path = "./resources/logo/logo-256.png";
        size_t data_size;
        void *data = plug_load_resource(file_path, &data_size);
        Image logo = LoadImageFromMemory(GetFileExtension(file_path), data, data_size);
        SetWindowIcon(logo);
        plug_free_resource(data);
    }
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_ERROR, "Could not initialize the audio device");
        CloseWindow();
        return 1;
    }

    plug_init();
    const char *render_output = NULL;
    bool command_line_error = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--scene") == 0) {
            if (i + 1 >= argc || !plug_select_scene(argv[++i])) {
                TraceLog(LOG_WARNING, "Unknown or missing command-line scene");
                command_line_error = true;
            }
            continue;
        }
        if (strcmp(argv[i], "--ascii-image") == 0) {
            if (i + 1 >= argc || !plug_load_ascii_image(argv[++i])) {
                TraceLog(LOG_WARNING, "Could not load command-line ASCII image");
                command_line_error = true;
            } else if (!plug_select_scene("ascii")) {
                TraceLog(LOG_WARNING, "Could not select ASCII scene");
                command_line_error = true;
            }
            continue;
        }
        if (strcmp(argv[i], "--render") == 0) {
            if (i + 1 >= argc) {
                TraceLog(LOG_WARNING, "Missing command-line render output path");
                command_line_error = true;
            } else {
                render_output = argv[++i];
            }
            continue;
        }
        if (!plug_load_track(argv[i])) {
            TraceLog(LOG_WARNING, "Could not load command-line track: %s", argv[i]);
            command_line_error = true;
        }
    }

    bool exit_after_render = false;
    int exit_status = command_line_error ? 1 : 0;
    if (render_output != NULL && exit_status == 0) {
        exit_after_render = plug_start_render(render_output);
        if (!exit_after_render) {
            TraceLog(LOG_ERROR, "Could not start command-line render");
            exit_status = 1;
        }
    }
    while (exit_status == 0 && !WindowShouldClose()) {
        if (IsKeyPressed(KEY_H)) {
            void *state = plug_pre_reload();
            if (!reload_libplug()) return 1;
            plug_post_reload(state);
        }
        plug_update();
        if (exit_after_render && !plug_render_active()) break;
    }

    if (exit_after_render && (plug_render_active() || plug_render_failed())) {
        exit_status = 1;
    }

    plug_shutdown();
    CloseAudioDevice();
    CloseWindow();

    return exit_status;
}
