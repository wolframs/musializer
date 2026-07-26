#include <stdbool.h>

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
// #define NOB_WARN_DEPRECATED
#include "../thirdparty/nob.h"
#include "../build/config.h"
#include "./configurer.c"

static const char *raylib_modules[] = {
    "rcore",
    "raudio",
    "rglfw",
    "rmodels",
    "rshapes",
    "rtext",
    "rtextures",
    "utils",
};

static size_t raylib_module_dependencies(const char *module,
                                          const char *primary_source,
                                          const char **dependencies)
{
    size_t count = 0;
    dependencies[count++] = primary_source;
    if (strcmp(module, "rcore") == 0) {
        // rcore.c textually includes the GLFW platform backend. Track it
        // explicitly so window-system fixes cannot reuse a stale rcore object.
        dependencies[count++] = RAYLIB_SRC_FOLDER"platforms/rcore_desktop_glfw.c";
    }
    return count;
}

typedef enum {
    BUILD_PROFILE_RELEASE,
    BUILD_PROFILE_DEBUG,
    BUILD_PROFILE_SANITIZE,
    BUILD_PROFILE_HOTRELOAD,
} Build_Profile;

// The no-argument build intentionally remains equivalent to the historical
// optimized build. Named profiles are invocation-local; they do not rewrite
// build/config.h.
static Build_Profile build_profile = BUILD_PROFILE_RELEASE;
// Distribution builds must never reuse workstation-specific release objects.
static bool build_for_distribution = false;

static const char *build_profile_name(Build_Profile profile)
{
    switch (profile) {
    case BUILD_PROFILE_RELEASE:   return "release";
    case BUILD_PROFILE_DEBUG:     return "debug";
    case BUILD_PROFILE_SANITIZE:  return "sanitize";
    case BUILD_PROFILE_HOTRELOAD: return "hotreload";
    }
    return "unknown";
}

static bool parse_build_profile(const char *name, Build_Profile *profile)
{
    if (strcmp(name, "release") == 0) {
        *profile = BUILD_PROFILE_RELEASE;
    } else if (strcmp(name, "debug") == 0) {
        *profile = BUILD_PROFILE_DEBUG;
    } else if (strcmp(name, "sanitize") == 0) {
        *profile = BUILD_PROFILE_SANITIZE;
    } else if (strcmp(name, "hotreload") == 0) {
        *profile = BUILD_PROFILE_HOTRELOAD;
    } else {
        return false;
    }
    return true;
}

static bool build_uses_hotreload(void)
{
    if (build_profile == BUILD_PROFILE_HOTRELOAD) return true;
#ifdef MUSIALIZER_HOTRELOAD
    return true;
#else
    return false;
#endif
}

// Centralized engine sources give every platform one clean insertion point.
// Platform recipes append their own FFmpeg and host implementations.
static void append_engine_sources(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd,
        "./src/plug.c",
        "./src/audio_analyzer.c",
        "./src/beat_tracker.c",
        "./src/sample_ring.c",
        "./src/ascii_art.c",
        "./src/analysis_bridge.c",
        "./src/analysis_candidate.c",
        "./src/assist_ui_state.c",
        "./src/editor_draft.c",
        "./src/event_timeline.c",
        "./src/track_timeline.c",
        "./src/track_identity.c",
        "./src/song_atlas_map.c",
        "./src/scene_event_merge.c",
        "./src/scene_settings.c",
        "./src/scene_routes.c",
        "./src/route_editor_state.c",
        "./src/preset_store.c",
        "./src/semantic_lane.c",
        "./src/lyrics.c",
        "./src/lyrics_editor_ui.c",
        "./src/lyrics_editor_layout.c",
        "./src/caption_layout.c",
        "./src/ui_notice.c",
        "./src/ui_row_typography.c",
        "./src/ui_widgets.c",
        "./src/workspace_layout.c",
        "./src/timeline_layout.c",
        "./src/project.c",
        "./src/project_io.c",
        "./src/render_export.c",
        "./src/sha256.c",
        "./src/scene_switch.c",
        "./src/scene.c",
        "./src/scene_spectrum.c",
        "./src/scene_pulse_field.c",
        "./src/scene_orbital_lattice_motion.c",
        "./src/scene_orbital_lattice.c",
        "./src/scene_ascii_field.c",
        "./src/scene_song_atlas.c",
        "./src/scene_spectral_terrarium.c",
        "./src/scene_constellation_motion.c",
        "./src/scene_constellation.c",
        "./src/scene_cadence.c",
        "./src/scene_loom_weave.c",
        "./src/scene_loom.c",
        "./src/scene_pentagram.c");
}

static void append_posix_plug_sources(Nob_Cmd *cmd)
{
    append_engine_sources(cmd);
    nob_cmd_append(cmd, "./src/ffmpeg_posix.c");
}

static void append_windows_plug_sources(Nob_Cmd *cmd)
{
    append_engine_sources(cmd);
    nob_cmd_append(cmd, "./src/ffmpeg_windows.c");
}

static void append_tested_core_sources(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd,
        "./src/audio_analyzer.c",
        "./src/beat_tracker.c",
        "./src/sample_ring.c",
        "./src/ascii_art.c",
        "./src/analysis_bridge.c",
        "./src/analysis_candidate.c",
        "./src/assist_ui_state.c",
        "./src/editor_draft.c",
        "./src/event_timeline.c",
        "./src/track_timeline.c",
        "./src/track_identity.c",
        "./src/song_atlas_map.c",
        "./src/scene_event_merge.c",
        "./src/scene_settings.c",
        "./src/scene_orbital_lattice_motion.c",
        "./src/scene_constellation_motion.c",
        "./src/scene_loom_weave.c",
        "./src/scene_routes.c",
        "./src/route_editor_state.c",
        "./src/preset_store.c",
        "./src/semantic_lane.c",
        "./src/lyrics.c",
        "./src/lyrics_editor_layout.c",
        "./src/caption_layout.c",
        "./src/ui_notice.c",
        "./src/ui_row_typography.c",
        "./src/workspace_layout.c",
        "./src/timeline_layout.c",
        "./src/render_export.c",
        "./src/sha256.c",
        "./src/scene_switch.c",
        "./src/project.c",
        "./src/project_io.c");
}

static const char *distribution_support_files[] = {
    "README.md",
    "LICENSE",
    "CHANGELOG.txt",
    ".env.example",
    "packaging/PRODUCT_READINESS.md",
    "packaging/linux/io.github.tsoding.musializer.desktop.in",
    "packaging/linux/io.github.tsoding.musializer.xml",
    "tools/install-linux-launcher.sh",
    "tools/musializer-launcher",
    "resources/logo/logo-256.png",
    "resources/fonts/OFL.txt",
    "resources/fonts/SpaceGrotesk-OFL.txt",
    "tools/ANALYSIS_ADAPTERS.md",
    "tools/MEASURED_ANALYSIS.md",
    "tools/UI_REVIEW.md",
    "tools/analysis_io.py",
    "tools/analyze_audio.py",
    "tools/external_analysis.py",
    "tools/import_whisper.py",
    "tools/lyric_align.py",
    "tools/mimo_openrouter.py",
    "tools/musializer_doctor.py",
    "prompts/lyrics_cleanup_system.md",
    "schemas/analysis-cache-v1.schema.json",
    "schemas/analysis-provenance-v1.schema.json",
    "schemas/codex-lyric-review-output-v1.schema.json",
    "schemas/lyric-review-v1.schema.json",
    "schemas/lyric-sync-v1.schema.json",
    "schemas/lyric-timing-v1.schema.json",
    "schemas/measured-analysis-v1.schema.json",
    "schemas/project-v1.schema.json",
    "schemas/scene-plan-v1.schema.json",
    "schemas/semantic-notes-v1.schema.json",
    "schemas/semantic-score-v1.schema.json",
};

static bool copy_distribution_support(const char *root)
{
    const char *directories[] = {
        "packaging", "packaging/linux", "tools", "prompts", "schemas",
        "resources", "resources/logo", "resources/fonts"
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(directories); ++i) {
        if (!nob_mkdir_if_not_exists(
                nob_temp_sprintf("%s/%s", root, directories[i]))) return false;
    }
    for (size_t i = 0; i < NOB_ARRAY_LEN(distribution_support_files); ++i) {
        const char *relative = distribution_support_files[i];
        if (!nob_copy_file(relative, nob_temp_sprintf("%s/%s", root, relative))) {
            return false;
        }
    }
    return true;
}

static void append_distribution_support_paths(Nob_Cmd *cmd, const char *root)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(distribution_support_files); ++i) {
        nob_cmd_append(cmd,
            nob_temp_sprintf("%s/%s", root, distribution_support_files[i]));
    }
}

// @backcomp
#if defined(MUSIALIZER_TARGET)
#error "We recently replaced a single MUSIALIZER_TARGET macro with a bunch of MUSIALIZER_TARGET_<TARGET> macros instead. Since MUSIALIZER_TARGET is still defined your ./build/ is probably old. Please remove it so ./build/config.h gets regenerated."
#endif // MUSIALIZER_TARGET

#if defined(MUSIALIZER_TARGET_LINUX)
#include "nob_linux.c"
#elif defined(MUSIALIZER_TARGET_MACOS)
#include "nob_macos.c"
#elif defined(MUSIALIZER_TARGET_WIN64_MINGW)
#include "nob_win64_mingw.c"
#elif defined(MUSIALIZER_TARGET_WIN64_MSVC)
#include "nob_win64_msvc.c"
#elif defined(MUSIALIZER_TARGET_OPENBSD)
#include "nob_openbsd.c"
#else
#error "No Musializer Target is defined. Check your ./build/config.h."
#endif // MUSIALIZER_TARGET

#include "../build/config_logger.c"

void log_available_subcommands(const char *program, Nob_Log_Level level)
{
    nob_log(level, "Usage: %s [subcommand] [profile]", program);
    nob_log(level, "Subcommands:");
    nob_log(level, "    build [release|debug|sanitize|hotreload] (default: release)");
    nob_log(level, "    test  [release|debug|sanitize] (default: debug)");
    nob_log(level, "    dist");
    nob_log(level, "    svg");
    nob_log(level, "    help");
}

static bool build_and_run_tests(Build_Profile profile)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_File_Paths children = {0};

    if (!nob_mkdir_if_not_exists("./build/tests")) nob_return_defer(false);
    if (!nob_read_entire_dir("./tests", &children)) nob_return_defer(false);

    nob_cmd_append(&cmd, "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-I.", "-Isrc", "-Itests");
    if (profile == BUILD_PROFILE_RELEASE) {
        nob_cmd_append(&cmd, "-O3", "-DNDEBUG");
    } else if (profile == BUILD_PROFILE_SANITIZE) {
        nob_cmd_append(&cmd, "-O1", "-g3", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all",
                       "-fsanitize=address,undefined");
    } else {
        nob_cmd_append(&cmd, "-O0", "-g3", "-fno-omit-frame-pointer");
    }

    nob_cmd_append(&cmd, "./tests/test_support.c", "./tests/audio_fixtures.c");
    for (size_t i = 0; i < children.count; ++i) {
        Nob_String_View name = nob_sv_from_cstr(children.items[i]);
        if (name.count > 5 && strcmp(children.items[i], "test_support.c") != 0 &&
            strncmp(name.data, "test_", 5) == 0 && nob_sv_end_with(name, ".c")) {
            nob_cmd_append(&cmd, nob_temp_sprintf("./tests/%s", children.items[i]));
        }
    }
    append_tested_core_sources(&cmd);
    nob_cmd_append(&cmd, "-o", "./build/tests/musializer_tests", "-lm");
    if (!nob_cmd_run(&cmd)) nob_return_defer(false);

    if (profile == BUILD_PROFILE_SANITIZE) {
        // LeakSanitizer cannot operate under ptrace-based sandboxes. ASan's
        // bounds/use-after-free checks and UBSan remain fully enabled; use a
        // separate leak checker on hosts where ptrace is available.
        nob_cmd_append(&cmd, "env", "ASAN_OPTIONS=detect_leaks=0");
    }
    nob_cmd_append(&cmd, "./build/tests/musializer_tests");
    if (!nob_cmd_run(&cmd)) nob_return_defer(false);

defer:
    nob_cmd_free(cmd);
    nob_da_free(children);
    return result;
}

typedef struct {
    const char *file_path;
    size_t offset;
    size_t size;
} Resource;

Resource resources[] = {
    { .file_path = "./resources/logo/logo-256.png" },
    { .file_path = "./resources/shaders/glsl330/circle.fs" },
    { .file_path = "./resources/shaders/glsl120/circle.fs" },
    { .file_path = "./resources/icons/volume.png" },
    { .file_path = "./resources/icons/play.png" },
    { .file_path = "./resources/icons/render.png" },
    { .file_path = "./resources/icons/fullscreen.png" },
    { .file_path = "./resources/icons/microphone.png" },
    { .file_path = "./resources/fonts/SpaceGrotesk-Regular.otf" },
    { .file_path = "./resources/fonts/Alegreya-Regular.ttf" },
};

bool generate_resource_bundle(void)
{
    bool result = true;
    Nob_String_Builder bundle = {0};
    Nob_String_Builder content = {0};
    FILE *out = NULL;

    // bundle  = [aaaaaaaaabbbbb]
    //            ^        ^
    // content = []
    // 0, 9

    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        content.count = 0;
        if (!nob_read_entire_file(resources[i].file_path, &content)) nob_return_defer(false);
        resources[i].offset = bundle.count;
        resources[i].size = content.count;
        nob_da_append_many(&bundle, content.items, content.count);
        nob_da_append(&bundle, 0);
    }

    const char *bundle_h_path = "./build/bundle.h";
    out = fopen(bundle_h_path, "wb");
    if (out == NULL) {
        nob_log(NOB_ERROR, "Could not open file %s for writing: %s", bundle_h_path, strerror(errno));
        nob_return_defer(false);
    }

    genf(out, "#ifndef BUNDLE_H_");
    genf(out, "#define BUNDLE_H_");
    genf(out, "typedef struct {");
    genf(out, "    const char *file_path;");
    genf(out, "    size_t offset;");
    genf(out, "    size_t size;");
    genf(out, "} Resource;");
    genf(out, "size_t resources_count = %zu;", NOB_ARRAY_LEN(resources));
    genf(out, "Resource resources[] = {");
    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        genf(out, "    {.file_path = \"%s\", .offset = %zu, .size = %zu},",
             resources[i].file_path, resources[i].offset, resources[i].size);
    }
    genf(out, "};");

    genf(out, "unsigned char bundle[] = {");
    size_t row_size = 20;
    for (size_t i = 0; i < bundle.count; ) {
        fprintf(out, "     ");
        for (size_t col = 0; col < row_size && i < bundle.count; ++col, ++i) {
            fprintf(out, "0x%02X, ", (unsigned char)bundle.items[i]);
        }
        fputc('\n', out);
    }
    genf(out, "};");
    genf(out, "#endif // BUNDLE_H_");

    nob_log(NOB_INFO, "Generated %s", bundle_h_path);

defer:
    if (out) fclose(out);
    free(content.items);
    free(bundle.items);
    return result;
}

int main(int argc, char **argv)
{
    nob_log(NOB_INFO, "--- STAGE 2 ---");
    log_config(NOB_INFO);
    nob_log(NOB_INFO, "---");

    const char *program = nob_shift_args(&argc, &argv);

    const char *subcommand = NULL;
    if (argc <= 0) {
        subcommand = "build";
    } else {
        subcommand = nob_shift_args(&argc, &argv);
    }

    if (strcmp(subcommand, "build") == 0) {
        if (argc > 0) {
            const char *profile_name = nob_shift_args(&argc, &argv);
            if (!parse_build_profile(profile_name, &build_profile)) {
                nob_log(NOB_ERROR, "Unknown build profile `%s`", profile_name);
                return 1;
            }
        }
        if (argc > 0) {
            nob_log(NOB_ERROR, "Unexpected argument `%s`", argv[0]);
            return 1;
        }
        nob_log(NOB_INFO, "Build profile: %s", build_profile_name(build_profile));
        if (!build_raylib()) return 1;
#ifndef MUSIALIZER_UNBUNDLE
        if (!generate_resource_bundle()) return 1;
#endif // MUSIALIZER_UNBUNDLE
        if (!build_musializer()) return 1;
    } else if (strcmp(subcommand, "test") == 0) {
        build_profile = BUILD_PROFILE_DEBUG;
        if (argc > 0) {
            const char *profile_name = nob_shift_args(&argc, &argv);
            if (!parse_build_profile(profile_name, &build_profile) || build_profile == BUILD_PROFILE_HOTRELOAD) {
                nob_log(NOB_ERROR, "Unknown or unsupported test profile `%s`", profile_name);
                return 1;
            }
        }
        if (argc > 0) {
            nob_log(NOB_ERROR, "Unexpected argument `%s`", argv[0]);
            return 1;
        }
        nob_log(NOB_INFO, "Test profile: %s", build_profile_name(build_profile));
        if (!build_and_run_tests(build_profile)) return 1;
    } else if (strcmp(subcommand, "dist") == 0) {
        if (argc > 0) {
            nob_log(NOB_ERROR, "Unexpected argument `%s`", argv[0]);
            return 1;
        }
        if (build_uses_hotreload()) {
            nob_log(NOB_ERROR, "Distribution builds do not support hot reload");
            return 1;
        }
#ifdef MUSIALIZER_UNBUNDLE
        nob_log(NOB_ERROR, "Distribution builds require bundled resources");
        return 1;
#endif
        build_profile = BUILD_PROFILE_RELEASE;
        build_for_distribution = true;
        nob_log(NOB_INFO, "Building portable release artifacts for distribution");
        if (!build_raylib()) return 1;
        if (!generate_resource_bundle()) return 1;
        if (!build_musializer()) return 1;
        if (!build_dist()) return 1;
    } else if (strcmp(subcommand, "config") == 0) {
        nob_log(NOB_ERROR, "The `config` command does not exist anymore!");
        nob_log(NOB_ERROR, "Edit %s to configure the build!", CONFIG_PATH);
        return 1;
    } else if (strcmp(subcommand, "svg") == 0) {
        Nob_Procs procs = {0};

        Nob_Cmd cmd = {0};

        typedef struct {
            const char *in_path;
            const char *out_path;
            int resize;
        } Svg;

        Svg svgs[] = {
            {.out_path = "./resources/logo/logo-256.ico",    .in_path = "./resources/logo/logo.svg", .resize = 256, },
            {.out_path = "./resources/logo/logo-256.png",    .in_path = "./resources/logo/logo.svg", .resize = 256, },
            {.out_path = "./resources/icons/fullscreen.png", .in_path = "./resources/icons/fullscreen.svg"          },
            {.out_path = "./resources/icons/volume.png",     .in_path = "./resources/icons/volume.svg"              },
            {.out_path = "./resources/icons/play.png",       .in_path = "./resources/icons/play.svg"                },
            {.out_path = "./resources/icons/render.png",     .in_path = "./resources/icons/render.svg"              },
            {.out_path = "./resources/icons/microphone.png", .in_path = "./resources/icons/microphone.svg"          },
        };

        for (size_t i = 0; i < NOB_ARRAY_LEN(svgs); ++i) {
            if (nob_needs_rebuild1(svgs[i].out_path, svgs[i].in_path)) {
                cmd.count = 0;
                nob_cmd_append(&cmd, "convert");
                nob_cmd_append(&cmd, "-background", "None");
                nob_cmd_append(&cmd, svgs[i].in_path);
                if (svgs[i].resize) {
                    nob_cmd_append(&cmd, "-resize", nob_temp_sprintf("%d", svgs[i].resize));
                }
                nob_cmd_append(&cmd, svgs[i].out_path);
                if (!nob_cmd_run(&cmd, .async = &procs)) return 1;
            } else {
                nob_log(NOB_INFO, "%s is up to date", svgs[i].out_path);
            }
        }

        if (!nob_procs_wait(procs)) return 1;
    } else if (strcmp(subcommand, "help") == 0) {
        log_available_subcommands(program, NOB_INFO);
    } else {
        nob_log(NOB_ERROR, "Unknown subcommand %s", subcommand);
        log_available_subcommands(program, NOB_ERROR);
        return 1;
    }
    // TODO: it would be nice to check for situations like building TARGET_WIN64_MSVC on Linux and report that it's not possible.
    return 0;
}
