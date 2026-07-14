#define MUSIALIZER_TARGET_NAME "win64-mingw"

// On windows, mingw doesn't have the `x86_64-w64-mingw32-` prefix for tools such as `windres` or `ar`.
// For gcc, you can use both `x86_64-w64-mingw32-gcc` and just `gcc`
#ifdef _WIN32
#define MAYBE_PREFIXED(x) x
#else
#define MAYBE_PREFIXED(x) "x86_64-w64-mingw32-"x
#endif // _WIN32

static bool mingw_profile_supported(void)
{
    if (build_profile == BUILD_PROFILE_SANITIZE) {
        nob_log(NOB_ERROR,
                "The sanitize profile is not supported by the MinGW recipe (its ASan/UBSan runtime is incompatible with the static Windows build)");
        return false;
    }
    return true;
}

static void append_mingw_profile_flags(Cmd *cmd)
{
    switch (build_profile) {
    case BUILD_PROFILE_RELEASE:
        cmd_append(cmd, "-O3", "-DNDEBUG", "-flto");
        break;
    case BUILD_PROFILE_DEBUG:
    case BUILD_PROFILE_HOTRELOAD:
        cmd_append(cmd, "-O0", "-g3", "-fno-omit-frame-pointer");
        break;
    case BUILD_PROFILE_SANITIZE:
        break; // Rejected by mingw_profile_supported().
    }
}

static const char *mingw_raylib_build_path(void)
{
    if (build_profile == BUILD_PROFILE_RELEASE && !build_uses_hotreload()) {
        return temp_sprintf("./build/raylib/%s", MUSIALIZER_TARGET_NAME);
    }
    return temp_sprintf("./build/raylib/%s-%s-%s", MUSIALIZER_TARGET_NAME,
                        build_profile_name(build_profile),
                        build_uses_hotreload() ? "shared" : "static");
}

bool build_musializer(void)
{
    bool result = true;
    Cmd cmd = {0};
    Procs procs = {0};

    if (!mingw_profile_supported()) return_defer(false);
    const bool hotreload = build_uses_hotreload();
    const char *raylib_path = mingw_raylib_build_path();

    cmd_append(&cmd, MAYBE_PREFIXED("windres"));
    cmd_append(&cmd, "./src/musializer.rc");
    cmd_append(&cmd, "-O", "coff");
    cmd_append(&cmd, "-o", "./build/musializer.res");
    if (!cmd_run(&cmd)) return_defer(false);

    if (hotreload) {
    cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
    cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra", "-DMUSIALIZER_HOTRELOAD");
    cmd_append(&cmd, "-I.");
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-fPIC", "-shared");
    cmd_append(&cmd, "-static-libgcc");
    cmd_append(&cmd, "-o", "./build/libplug.dll");
    append_windows_plug_sources(&cmd);
    cmd_append(&cmd, "./thirdparty/tinyfiledialogs.c");
    cmd_append(&cmd,
        "-L./build",
        "-l:raylib.dll");
    append_mingw_profile_flags(&cmd);
    cmd_append(&cmd, "-lwinmm", "-lgdi32", "-lole32");
    if (!cmd_run(&cmd, .async = &procs)) return_defer(false);

    cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
    cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra", "-DMUSIALIZER_HOTRELOAD");
    cmd_append(&cmd, "-I.");
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-o", "./build/musializer");
    cmd_append(&cmd,
        "./src/musializer.c",
        "./src/hotreload_windows.c");
    cmd_append(&cmd,
        "-Wl,-rpath=./build/",
        "-Wl,-rpath=./",
        temp_sprintf("-Wl,-rpath=%s", raylib_path),
        // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
        temp_sprintf("-Wl,-rpath=.%s", raylib_path + strlen("./build")));
    cmd_append(&cmd,
        "-L./build",
        "-l:raylib.dll");
    append_mingw_profile_flags(&cmd);
    cmd_append(&cmd, "-lwinmm", "-lgdi32");
    if (!cmd_run(&cmd, .async = &procs)) return_defer(false);

    if (!procs_flush(&procs)) return_defer(false);
    } else {
    cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
    cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra");
    cmd_append(&cmd, "-I.");
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-o", "./build/musializer");
    append_windows_plug_sources(&cmd);
    cmd_append(&cmd,
        "./src/musializer.c",
        "./thirdparty/tinyfiledialogs.c",
        "./build/musializer.res"
        );
    cmd_append(&cmd,
        temp_sprintf("-L%s", raylib_path),
        "-l:libraylib.a");
    append_mingw_profile_flags(&cmd);
    cmd_append(&cmd, "-lwinmm", "-lgdi32", "-lole32");
    cmd_append(&cmd, "-static");
    if (!cmd_run(&cmd)) return_defer(false);
    }

defer:
    cmd_free(cmd);
    da_free(procs);
    return result;
}

bool build_raylib()
{
    bool result = true;
    Cmd cmd = {0};
    File_Paths object_files = {0};

    if (!mingw_profile_supported()) return_defer(false);

    if (!mkdir_if_not_exists("./build/raylib")) {
        return_defer(false);
    }

    Procs procs = {0};

    const char *build_path = mingw_raylib_build_path();

    if (!mkdir_if_not_exists(build_path)) {
        return_defer(false);
    }

    for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = temp_sprintf(RAYLIB_SRC_FOLDER"%s.c", raylib_modules[i]);
        const char *output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        const char *dependencies[2];
        size_t dependency_count = raylib_module_dependencies(
            raylib_modules[i], input_path, dependencies);

        da_append(&object_files, output_path);

        if (needs_rebuild(output_path, dependencies, dependency_count)) {
            cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
            cmd_append(&cmd, "-ggdb", "-DPLATFORM_DESKTOP", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1");
            cmd_append(&cmd, "-DPLATFORM_DESKTOP");
            cmd_append(&cmd, "-fPIC");
            cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER"external/glfw/include");
            cmd_append(&cmd, "-c", input_path);
            cmd_append(&cmd, "-o", output_path);
            append_mingw_profile_flags(&cmd);

            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
    }

    if (!procs_flush(&procs)) return_defer(false);

    if (!build_uses_hotreload()) {
    const char *libraylib_path = temp_sprintf("%s/libraylib.a", build_path);

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, MAYBE_PREFIXED("ar"));
        cmd_append(&cmd, "-crs", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        if (!cmd_run(&cmd)) return_defer(false);
    }
    } else {
    // it cannot load the raylib dll if it not in the same folder as the executable
    const char *libraylib_path = "./build/raylib.dll";

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
        cmd_append(&cmd, "-shared");
        cmd_append(&cmd, "-o", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        cmd_append(&cmd, "-lwinmm", "-lgdi32");
        if (!cmd_run(&cmd)) return_defer(false);
    }
    }

defer:
    cmd_free(cmd);
    da_free(object_files);
    return result;
}

bool build_dist(void)
{
    if (build_uses_hotreload()) {
    nob_log(ERROR, "We do not ship with hotreload enabled");
    return false;
    } else {
    if (!mkdir_if_not_exists("./musializer-win64-mingw/")) return false;
    if (!copy_file("./build/musializer.exe", "./musializer-win64-mingw/musializer.exe")) return false;
    if (!copy_distribution_support("./musializer-win64-mingw")) return false;
    if (!copy_file("musializer-logged.bat", "./musializer-win64-mingw/musializer-logged.bat")) return false;
    // TODO: pack ffmpeg.exe with windows build
    //if (!copy_file("ffmpeg.exe", "./musializer-win64-mingw/ffmpeg.exe")) return false;
    Cmd cmd = {0};
    const char *dist_path = "./musializer-win64-mingw.zip";
    int dist_exists = file_exists(dist_path);
    if (dist_exists < 0) return false;
    if (dist_exists > 0 && !delete_file(dist_path)) return false;
    cmd_append(&cmd, "zip", dist_path,
               "./musializer-win64-mingw/musializer.exe",
               "./musializer-win64-mingw/musializer-logged.bat");
    append_distribution_support_paths(&cmd, "./musializer-win64-mingw");
    bool ok = cmd_run(&cmd);
    cmd_free(cmd);
    if (!ok) return false;
    nob_log(INFO, "Created %s", dist_path);
    return true;
    }
}
