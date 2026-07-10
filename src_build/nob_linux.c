#define MUSIALIZER_TARGET_NAME "linux"

static void append_linux_profile_flags(Cmd *cmd, bool link)
{
    switch (build_profile) {
    case BUILD_PROFILE_RELEASE:
        cmd_append(cmd, "-O3", "-march=native", "-ffast-math", "-flto=auto");
        break;
    case BUILD_PROFILE_SANITIZE:
        cmd_append(cmd, "-O1", "-g3", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all",
                   "-fsanitize=address,undefined");
        break;
    case BUILD_PROFILE_DEBUG:
    case BUILD_PROFILE_HOTRELOAD:
        cmd_append(cmd, "-O0", "-g3", "-fno-omit-frame-pointer");
        break;
    }
    (void) link;
}

static const char *linux_raylib_build_path(void)
{
    if (build_profile == BUILD_PROFILE_RELEASE) {
        return temp_sprintf("./build/raylib/%s", MUSIALIZER_TARGET_NAME);
    }
    return temp_sprintf("./build/raylib/%s-%s", MUSIALIZER_TARGET_NAME,
                        build_profile_name(build_profile));
}

bool build_musializer(void)
{
    bool result = true;
    Cmd cmd = {0};
    Procs procs = {0};

    const bool hotreload = build_uses_hotreload();
    const char *raylib_path = linux_raylib_build_path();
    if (hotreload) {
    // TODO: add a way to replace `cc` with something else GCC compatible on POSIX
    // Like `clang` for instance
    cmd_append(&cmd, "cc",
        "-Wall", "-Wextra", "-ggdb",
        "-DMUSIALIZER_HOTRELOAD",
        "-I.", "-I"RAYLIB_SRC_FOLDER,
        "-fPIC", "-shared",
        "-o", "./build/libplug.so");
    append_posix_plug_sources(&cmd);
    cmd_append(&cmd, "./thirdparty/tinyfiledialogs.c",
        temp_sprintf("-L%s", raylib_path), "-l:libraylib.so");
    append_linux_profile_flags(&cmd, true);
    cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
    if (!cmd_run(&cmd)) return_defer(false);

    cmd_append(&cmd, "cc",
        "-Wall", "-Wextra", "-ggdb",
        "-DMUSIALIZER_HOTRELOAD",
        "-I.", "-I"RAYLIB_SRC_FOLDER,
        "-o", "./build/musializer",
        "./src/musializer.c", "./src/hotreload_posix.c",
        "-Wl,-rpath=./build/",
        "-Wl,-rpath=./",
        temp_sprintf("-Wl,-rpath=%s", raylib_path),
        // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
        temp_sprintf("-Wl,-rpath=.%s", raylib_path + strlen("./build")),
        temp_sprintf("-L%s", raylib_path));
    append_linux_profile_flags(&cmd, true);
    cmd_append(&cmd, "-l:libraylib.so", "-lm", "-ldl", "-lpthread");
    if (!cmd_run(&cmd)) return_defer(false);

    if (!procs_flush(&procs)) return_defer(false);
    } else {
    cmd_append(&cmd, "cc",
        "-Wall", "-Wextra", "-ggdb",
        "-I.",
        "-I"RAYLIB_SRC_FOLDER,
        "-o", "./build/musializer");
    append_posix_plug_sources(&cmd);
    cmd_append(&cmd, "./src/musializer.c", "./thirdparty/tinyfiledialogs.c",
        temp_sprintf("-L%s", raylib_path), "-l:libraylib.a");
    append_linux_profile_flags(&cmd, true);
    cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
    if (!cmd_run(&cmd)) return_defer(false);
    }

defer:
    cmd_free(cmd);
    da_free(procs);
    return result;
}

bool build_raylib(void)
{
    bool result = true;
    Cmd cmd = {0};
    File_Paths object_files = {0};

    if (!mkdir_if_not_exists("./build/raylib")) {
        return_defer(false);
    }

    Procs procs = {0};

    const char *build_path = linux_raylib_build_path();

    if (!mkdir_if_not_exists(build_path)) {
        return_defer(false);
    }

    for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = temp_sprintf(RAYLIB_SRC_FOLDER"%s.c", raylib_modules[i]);
        const char *output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        da_append(&object_files, output_path);

        if (needs_rebuild(output_path, &input_path, 1)) {
            cmd_append(&cmd, "cc",
                "-ggdb", "-DPLATFORM_DESKTOP", "-D_GLFW_X11", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1",
                "-I"RAYLIB_SRC_FOLDER"external/glfw/include",
                "-c", input_path,
                "-o", output_path);
            append_linux_profile_flags(&cmd, false);
            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
    }

    if (!procs_flush(&procs)) return_defer(false);

    if (!build_uses_hotreload()) {
    const char *libraylib_path = temp_sprintf("%s/libraylib.a", build_path);

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, "ar", "-crs", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        if (!cmd_run(&cmd)) return_defer(false);
    }
    } else {
    const char *libraylib_path = temp_sprintf("%s/libraylib.so", build_path);

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, "cc", "-shared", "-o", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        if (!cmd_run(&cmd)) return_defer(false);
    }
    }

defer:
    cmd_free(cmd);
    da_free(object_files);
    return result;
}

bool build_dist()
{
    if (build_uses_hotreload()) {
    nob_log(ERROR, "We do not ship with hotreload enabled");
    return false;
    } else {
    if (!mkdir_if_not_exists("./musializer-linux-x86_64/")) return false;
    if (!copy_file("./build/musializer", "./musializer-linux-x86_64/musializer")) return false;
    if (!copy_directory_recursively("./resources/", "./musializer-linux-x86_64/resources/")) return false;
    // TODO: should we pack ffmpeg with Linux build?
    // There are some static executables for Linux
    Cmd cmd = {0};
    cmd_append(&cmd, "tar", "fvcz", "./musializer-linux-x86_64.tar.gz", "./musializer-linux-x86_64");
    bool ok = cmd_run(&cmd);
    cmd_free(cmd);
    if (!ok) return false;

    return true;
    }
}
