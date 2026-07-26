#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <errno.h>
#include <math.h>

#include <raylib.h>

#define PREVIEW_AUDIO_BUFFER_FRAMES 8192
enum { COMMAND_LINE_ROUTE_CAPACITY = 256 };

#ifndef _WIN32
#include <signal.h> // needed for sigaction()
#endif // _WIN32

#include "./hotreload.h"

static bool parse_command_line_event(const char *spec, Event_Record *event)
{
    if (spec == NULL || event == NULL) return false;
    const char *separator = strchr(spec, ':');
    if (separator == NULL || separator == spec || (size_t)(separator - spec) >= 16) return false;
    char type[16] = {0};
    memcpy(type, spec, (size_t)(separator - spec));

    char *end = NULL;
    errno = 0;
    double timestamp = strtod(separator + 1, &end);
    if (errno == ERANGE || end == separator + 1 || *end != ':') return false;

    const char *id_text = end + 1;
    const char *id_end = strchr(id_text, ':');
    if (id_end == NULL || id_end == id_text) return false;
    for (const char *digit = id_text; digit < id_end; ++digit) {
        if (*digit < '0' || *digit > '9') return false;
    }
    errno = 0;
    unsigned long long parsed_id = strtoull(id_text, &end, 10);
    if (errno == ERANGE || end != id_end) return false;

    const char *value_text = end + 1;
    errno = 0;
    float value = strtof(value_text, &end);
    if (errno == ERANGE || end == value_text || *end != '\0') return false;
    uint32_t event_type = 0;
    if (strcmp(type, "lyric") == 0) event_type = EVENT_TYPE_LYRIC;
    else if (strcmp(type, "semantic") == 0) event_type = EVENT_TYPE_SEMANTIC;
    else if (strcmp(type, "cue") == 0) event_type = EVENT_TYPE_CUE;
    else if (strcmp(type, "custom") == 0) event_type = EVENT_TYPE_CUSTOM;
    else return false;

    *event = (Event_Record) {
        .timestamp_seconds = timestamp,
        .id = (uint64_t)parsed_id,
        .type = event_type,
        .value_count = 1,
        .values = {value},
    };
    return true; // The plug owns canonical validation and insertion.
}

static bool parse_positive_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') return false;
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed == 0 ||
        parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_seconds(const char *text, double *seconds)
{
    if (text == NULL || seconds == NULL || text[0] == '\0') return false;
    char *end = NULL;
    errno = 0;
    double parsed = strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' ||
        !isfinite(parsed) || parsed < 0.0) return false;
    *seconds = parsed;
    return true;
}

static bool parse_resolution(const char *text, uint32_t *width, uint32_t *height)
{
    if (text == NULL || width == NULL || height == NULL) return false;
    const char *separator = strchr(text, 'x');
    if (separator == NULL || separator == text || separator[1] == '\0' ||
        strchr(separator + 1, 'x') != NULL) return false;
    char width_text[16];
    size_t width_length = (size_t)(separator - text);
    if (width_length >= sizeof(width_text)) return false;
    memcpy(width_text, text, width_length);
    width_text[width_length] = '\0';
    return parse_positive_u32(width_text, width) &&
           parse_positive_u32(separator + 1, height);
}

// `--ui-probe` request: the workspace state handed to the plug plus the window
// geometry, which is the host's to set. See Plug_Ui_Probe in plug.h.
typedef struct {
    Plug_Ui_Probe probe;
    uint32_t width;
    uint32_t height;
    bool size_requested;
} Command_Line_Ui_Probe;

static bool parse_ui_probe_panel(const char *text, Plug_Ui_Panel *panel)
{
    if (strcmp(text, "none")   == 0) { *panel = PLUG_UI_PANEL_NONE;   return true; }
    if (strcmp(text, "tune")   == 0) { *panel = PLUG_UI_PANEL_TUNE;   return true; }
    if (strcmp(text, "export") == 0) { *panel = PLUG_UI_PANEL_EXPORT; return true; }
    if (strcmp(text, "lyrics") == 0) { *panel = PLUG_UI_PANEL_LYRICS; return true; }
    if (strcmp(text, "assist") == 0) { *panel = PLUG_UI_PANEL_ASSIST; return true; }
    return false;
}

static bool parse_ui_probe_flag(const char *text, bool *value)
{
    if (strcmp(text, "1") == 0) { *value = true;  return true; }
    if (strcmp(text, "0") == 0) { *value = false; return true; }
    return false;
}

// Parses "key=value[,key=value...]". A repeated key, an unknown key, or an
// unparsable value is an error rather than a silent default, so a typo in a
// capture script cannot quietly photograph the wrong UI state.
static bool parse_ui_probe(const char *spec, Command_Line_Ui_Probe *request)
{
    if (spec == NULL || request == NULL) return false;
    *request = (Command_Line_Ui_Probe) {0};

    char buffer[256];
    size_t length = strlen(spec);
    if (length == 0 || length >= sizeof(buffer)) return false;
    memcpy(buffer, spec, length + 1);

    bool seen_panel = false;
    bool seen_fullscreen = false;
    bool seen_time = false;
    bool seen_play = false;
    bool seen_assist = false;
    bool seen_lyric = false;
    bool seen_zoom = false;
    char *cursor = buffer;
    while (cursor != NULL && *cursor != '\0') {
        char *comma = strchr(cursor, ',');
        if (comma != NULL) *comma++ = '\0';
        char *equals = strchr(cursor, '=');
        if (equals == NULL || equals == cursor || equals[1] == '\0') return false;
        *equals = '\0';
        const char *key = cursor;
        const char *value = equals + 1;

        if (strcmp(key, "panel") == 0) {
            if (seen_panel || !parse_ui_probe_panel(value, &request->probe.panel)) {
                return false;
            }
            seen_panel = true;
        } else if (strcmp(key, "fullscreen") == 0) {
            if (seen_fullscreen ||
                !parse_ui_probe_flag(value, &request->probe.fullscreen)) {
                return false;
            }
            seen_fullscreen = true;
        } else if (strcmp(key, "play") == 0) {
            if (seen_play || !parse_ui_probe_flag(value, &request->probe.playing)) {
                return false;
            }
            seen_play = true;
        } else if (strcmp(key, "lyric") == 0) {
            unsigned long index = 0;
            char *end = NULL;
            errno = 0;
            index = strtoul(value, &end, 10);
            if (seen_lyric || end == value || *end != '\0' || errno != 0 ||
                index == 0 || index > 4096) {
                return false;
            }
            request->probe.lyric_selection = (unsigned)index;
            seen_lyric = true;
        } else if (strcmp(key, "zoom") == 0) {
            char *end = NULL;
            errno = 0;
            double factor = strtod(value, &end);
            if (seen_zoom || end == value || *end != '\0' || errno != 0 ||
                !isfinite(factor) || factor < 1.0 || factor > 100000.0) {
                return false;
            }
            request->probe.timeline_zoom = factor;
            seen_zoom = true;
        } else if (strcmp(key, "assist") == 0) {
            if (seen_assist || strcmp(value, "confirm") != 0) return false;
            request->probe.assist_confirmation = true;
            seen_assist = true;
        } else if (strcmp(key, "time") == 0) {
            if (seen_time || !parse_seconds(value, &request->probe.seek_seconds)) {
                return false;
            }
            request->probe.seek_requested = true;
            seen_time = true;
        } else if (strcmp(key, "size") == 0) {
            if (request->size_requested ||
                !parse_resolution(value, &request->width, &request->height)) {
                return false;
            }
            request->size_requested = true;
        } else {
            return false;
        }
        cursor = comma;
    }
    return true;
}

static void print_command_line_help(FILE *stream, const char *program)
{
    fprintf(stream,
        "Musializer 2026.07 - deterministic music visualization workspace\n"
        "\n"
        "Usage: %s [options] [audio-file | project.musi]\n"
        "\n"
        "Workspace:\n"
        "  --project FILE          Open a .musi project\n"
        "  --save-project FILE     Atomically save the current workspace\n"
        "  --scene NAME            spectrum, pulse, orbital, ascii, atlas,\n"
        "                          terrarium, constellation, cadence, loom,\n"
        "                          or pentagram\n"
        "  --ascii-image FILE      Import an image and select ASCII Field\n"
        "  --event SPEC            Add type:seconds:id:value to the manual lane\n"
        "  --route SPEC            Drive a scene setting from live audio:\n"
        "                          parameter:source:band:in_min:in_max:\n"
        "                          out_min:out_max[:curve][:noclamp], e.g.\n"
        "                          loom.weight:band:2:0:1:0.4:2.2:smoothstep\n"
        "                          Sources: rms, peak, spectral_flux,\n"
        "                          beat_phase, band. Curves: step, linear,\n"
        "                          smoothstep, ease_in, ease_out\n"
        "  --analysis-bridge FILE  Import a verified analysis bridge\n"
        "  --auto-scenes           Enable imported scene suggestions\n"
        "\n"
        "Export:\n"
        "  --render FILE           Render MP4 and exit\n"
        "  --render-window S D     Render only D seconds starting at S.\n"
        "                          Frames match the same span of a full render\n"
        "  --resolution WIDTHxHEIGHT\n"
        "  --fps N\n"
        "  --quality NAME          balanced, high, or master\n"
        "\n"
        "Diagnostics:\n"
        "  --mute                  Start with the output volume at zero\n"
        "  --reload-once           Exercise one hot-reload handoff\n"
        "  --ui-probe SPEC         Open a workspace panel and park the transport\n"
        "                          for reproducible headless UI capture. SPEC is\n"
        "                          comma-separated key=value pairs:\n"
        "                          panel=none|tune|export|lyrics|assist,\n"
        "                          fullscreen=0|1, time=SECONDS, size=WIDTHxHEIGHT,\n"
        "                          assist=confirm arms the Assist confirmation\n"
        "                          prompt, lyric=N selects the nth lyric cue\n"
        "                          prompt (needs panel=assist),\n"
        "                          zoom=FACTOR zooms the timeline strip about the\n"
        "                          playhead (1 = whole track),\n"
        "                          play=0|1. The transport is parked unless\n"
        "                          play=1; audio-reactive scenes need play=1 but\n"
        "                          then capture a frame that is not reproducible.\n"
        "                          Every panel except none needs a loaded track\n"
        "  -h, --help              Show this help without opening a window\n"
        "  --version               Show the version\n",
        program != NULL && program[0] != '\0' ? program : "musializer");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_line_help(stdout, argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            puts("musializer 2026.07");
            return 0;
        }
    }
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

    // The default framebuffer is the preview path. Offline rendering uses a
    // deterministic supersampling resolve in plug.c, so both paths smooth the
    // same scene geometry without temporal jitter.
    // KDE/Wayland commonly exposes a logical window size and a larger physical
    // framebuffer through XWayland. The HiDPI flag keeps UI coordinates
    // logical while the framebuffer callback sizes the OpenGL viewport in
    // physical pixels.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_ALWAYS_RUN |
                   FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    size_t factor = 80;
    InitWindow(factor*16, factor*9, "Musializer");
    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "Could not initialize the rendering window");
        return 1;
    }
    SetWindowMinSize(960, 640);
    {
        const char *file_path = "./resources/logo/logo-256.png";
        size_t data_size;
        void *data = plug_load_resource(file_path, &data_size);
        Image logo = LoadImageFromMemory(GetFileExtension(file_path), data, data_size);
        SetWindowIcon(logo);
        UnloadImage(logo);
        plug_free_resource(data);
    }
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_ERROR, "Could not initialize the audio device");
        CloseWindow();
        return 1;
    }
    // raylib's default music half-buffer is sampleRate/30 (about 33 ms). The
    // editor occasionally performs durable saves or accepts completed analysis
    // jobs on the main thread, so retain roughly 170-186 ms of refill headroom
    // at the common 48/44.1 kHz source rates. This is decode-ahead, not output
    // device latency, and applies only to streams created after this call.
    SetAudioStreamBufferSizeDefault(PREVIEW_AUDIO_BUFFER_FRAMES);
    TraceLog(LOG_INFO, "AUDIO: Music stream half-buffer: %u frames",
             (unsigned)PREVIEW_AUDIO_BUFFER_FRAMES);

    plug_init();
    const char *render_output = NULL;
    const char *project_output = NULL;
    const char *analysis_bridge = NULL;
    bool command_line_error = false;
    bool reload_once = false;
    bool auto_scenes = false;
    bool ui_probe_requested = false;
    Command_Line_Ui_Probe ui_probe = {0};
    uint32_t render_width = 0;
    uint32_t render_height = 0;
    uint32_t render_fps = 0;
    const char *render_quality = NULL;
    bool render_window_set = false;
    double render_window_start = 0.0;
    double render_window_duration = 0.0;
    const char *route_specs[COMMAND_LINE_ROUTE_CAPACITY] = {0};
    size_t route_spec_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mute") == 0) {
            // Silent startup for scripted and test runs: kills the output
            // device volume only, so playback, analysis, and export behave
            // exactly as an audible session.
            SetMasterVolume(0.0f);
            continue;
        }
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
        if (strcmp(argv[i], "--event") == 0) {
            Event_Record event;
            if (i + 1 >= argc ||
                !parse_command_line_event(argv[++i], &event) ||
                !plug_record_event(event)) {
                TraceLog(LOG_WARNING, "Invalid command-line event; expected type:seconds:id:value");
                command_line_error = true;
            }
            continue;
        }
        if (strcmp(argv[i], "--route") == 0) {
            if (i + 1 >= argc) {
                TraceLog(LOG_WARNING,
                         "Missing command-line route; expected "
                         "parameter:source:band:in_min:in_max:out_min:out_max"
                         "[:curve][:noclamp]");
                command_line_error = true;
            } else if (route_spec_count >= COMMAND_LINE_ROUTE_CAPACITY) {
                TraceLog(LOG_WARNING, "Too many command-line routes (maximum %u)",
                         (unsigned)COMMAND_LINE_ROUTE_CAPACITY);
                command_line_error = true;
                i += 1;
            } else {
                // Defer routes until every positional/--project input has been
                // loaded. Otherwise a project hydration can overwrite a route
                // that happened to appear earlier in argv.
                route_specs[route_spec_count++] = argv[++i];
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
        if (strcmp(argv[i], "--render-window") == 0) {
            if (i + 2 >= argc ||
                !parse_seconds(argv[i + 1], &render_window_start) ||
                !parse_seconds(argv[i + 2], &render_window_duration) ||
                render_window_duration <= 0.0) {
                TraceLog(LOG_WARNING,
                         "Invalid render window; expected START_SECONDS DURATION_SECONDS");
                command_line_error = true;
            } else {
                render_window_set = true;
            }
            i += i + 2 < argc ? 2 : (argc - 1 - i);
            continue;
        }
        if (strcmp(argv[i], "--resolution") == 0) {
            if (i + 1 >= argc ||
                !parse_resolution(argv[++i], &render_width, &render_height)) {
                TraceLog(LOG_WARNING, "Invalid resolution; expected WIDTHxHEIGHT");
                command_line_error = true;
            }
            continue;
        }
        if (strcmp(argv[i], "--fps") == 0) {
            if (i + 1 >= argc || !parse_positive_u32(argv[++i], &render_fps)) {
                TraceLog(LOG_WARNING, "Invalid render frame rate");
                command_line_error = true;
            }
            continue;
        }
        if (strcmp(argv[i], "--quality") == 0) {
            if (i + 1 >= argc) {
                TraceLog(LOG_WARNING, "Missing render quality");
                command_line_error = true;
            } else {
                render_quality = argv[++i];
            }
            continue;
        }
        if (strcmp(argv[i], "--project") == 0) {
            if (i + 1 >= argc || !plug_load_project(argv[++i])) {
                TraceLog(LOG_WARNING, "Could not load command-line project");
                command_line_error = true;
            }
            continue;
        }
        if (strcmp(argv[i], "--save-project") == 0) {
            if (i + 1 >= argc) {
                TraceLog(LOG_WARNING, "Missing project output path");
                command_line_error = true;
            } else {
                project_output = argv[++i];
            }
            continue;
        }
        if (strcmp(argv[i], "--analysis-bridge") == 0) {
            if (i + 1 >= argc) {
                TraceLog(LOG_WARNING, "Missing command-line analysis bridge path");
                command_line_error = true;
            } else {
                analysis_bridge = argv[++i];
            }
            continue;
        }
        if (strcmp(argv[i], "--auto-scenes") == 0) {
            auto_scenes = true;
            continue;
        }
        if (strcmp(argv[i], "--reload-once") == 0) {
            reload_once = true;
            continue;
        }
        if (strcmp(argv[i], "--ui-probe") == 0) {
            if (i + 1 >= argc || !parse_ui_probe(argv[++i], &ui_probe)) {
                TraceLog(LOG_WARNING,
                         "Invalid --ui-probe spec; expected comma-separated "
                         "panel=, fullscreen=, time=, or size= pairs");
                command_line_error = true;
            } else {
                // Deferred until every input is loaded: a panel probe is only
                // meaningful once its track exists.
                ui_probe_requested = true;
            }
            continue;
        }
        if (IsFileExtension(argv[i], ".musi") ?
            !plug_load_project(argv[i]) : !plug_load_track(argv[i])) {
            TraceLog(LOG_WARNING, "Could not load command-line track: %s", argv[i]);
            command_line_error = true;
        }
    }

    for (size_t i = 0; i < route_spec_count; ++i) {
        if (!plug_add_scene_route(route_specs[i])) {
            TraceLog(LOG_WARNING,
                     "Invalid or duplicate command-line route; expected "
                     "parameter:source:band:in_min:in_max:out_min:out_max"
                     "[:curve][:noclamp]");
            command_line_error = true;
        }
    }

    if (!command_line_error &&
        (render_width != 0 || render_fps != 0 || render_quality != NULL) &&
        !plug_configure_render(render_width, render_height, render_fps, render_quality)) {
        TraceLog(LOG_WARNING,
                 "Invalid render configuration; quality is balanced, high, or master");
        command_line_error = true;
    }

    if (!command_line_error && render_window_set &&
        !plug_configure_render_window(render_window_start,
                                      render_window_duration)) {
        TraceLog(LOG_WARNING,
                 "Invalid render window; start and duration must be finite seconds");
        command_line_error = true;
    }

    if (analysis_bridge != NULL &&
        (command_line_error || !plug_load_analysis_bridge(analysis_bridge))) {
        TraceLog(LOG_WARNING, "Could not load command-line analysis bridge: %s",
                 analysis_bridge);
        command_line_error = true;
    }
    if (auto_scenes && (command_line_error || !plug_set_auto_scenes(true))) {
        TraceLog(LOG_WARNING, "Could not enable automatic scene switching");
        command_line_error = true;
    }
    if (project_output != NULL &&
        (command_line_error || !plug_save_project(project_output))) {
        TraceLog(LOG_WARNING, "Could not save command-line project: %s", project_output);
        command_line_error = true;
    }
    // Startup configuration is not an edit. Autosave would otherwise write every
    // flag above straight into a project opened with --project, so a one-off
    // `--resolution 2560x1440` preview permanently changed the saved output size.
    plug_mark_command_line_state_clean();
    if (ui_probe_requested && !command_line_error) {
        // Geometry is the host's; the plug owns workspace state. GLFW clamps to
        // the minimum window size, so a deliberately tiny probe still captures
        // the smallest layout the application actually permits.
        if (ui_probe.size_requested) {
            SetWindowSize((int)ui_probe.width, (int)ui_probe.height);
        }
        // Park the window at the origin so a capture of a display sized to the
        // window needs no guesswork about where the compositor placed it.
        SetWindowPosition(0, 0);
        if (!plug_apply_ui_probe(ui_probe.probe)) {
            TraceLog(LOG_WARNING,
                     "Could not apply --ui-probe state; a panel or seek probe "
                     "needs a loaded, seekable track");
            command_line_error = true;
        }
    }

    bool exit_after_render = false;
    bool exit_after_save = project_output != NULL && render_output == NULL;
    int exit_status = command_line_error ? 1 : 0;
    if (reload_once && exit_status == 0) {
        void *state = plug_pre_reload();
        if (state == NULL) {
            TraceLog(LOG_ERROR, "Hot reload was vetoed while resources remain owned");
            exit_status = 1;
        } else if (!reload_libplug()) {
            exit_status = 1;
        } else {
            plug_post_reload(state);
        }
    }
    if (render_output != NULL && exit_status == 0) {
        exit_after_render = plug_start_render(render_output);
        if (!exit_after_render) {
            TraceLog(LOG_ERROR, "Could not start command-line render");
            exit_status = 1;
        }
    }
    while (exit_status == 0 && !exit_after_save) {
        if (WindowShouldClose() && plug_confirm_close()) break;
        if (IsKeyPressed(KEY_H)) {
            void *state = plug_pre_reload();
            if (state == NULL) {
                TraceLog(LOG_WARNING,
                         "Hot reload was vetoed while resources remain owned");
            } else {
                if (!reload_libplug()) return 1;
                plug_post_reload(state);
            }
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
