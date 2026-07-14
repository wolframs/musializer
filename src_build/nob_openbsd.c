#define MUSIALIZER_TARGET_NAME "OpenBSD"

static bool openbsd_profile_supported(void)
{
    if (build_profile == BUILD_PROFILE_SANITIZE) {
        nob_log(NOB_ERROR,
                "The sanitize profile is not supported by the OpenBSD recipe (the base toolchain does not provide a portable ASan/UBSan runtime)");
        return false;
    }
    return true;
}

static void append_openbsd_profile_flags(Nob_Cmd *cmd)
{
    switch (build_profile) {
    case BUILD_PROFILE_RELEASE:
        nob_cmd_append(cmd, "-O2", "-DNDEBUG");
        break;
    case BUILD_PROFILE_DEBUG:
    case BUILD_PROFILE_HOTRELOAD:
        nob_cmd_append(cmd, "-O0", "-g3", "-fno-omit-frame-pointer");
        break;
    case BUILD_PROFILE_SANITIZE:
        break; // Rejected by openbsd_profile_supported().
    }
}

static const char *openbsd_raylib_build_path(void)
{
    if (build_profile == BUILD_PROFILE_RELEASE && !build_uses_hotreload()) {
        return nob_temp_sprintf("./build/raylib/%s", MUSIALIZER_TARGET_NAME);
    }
    return nob_temp_sprintf("./build/raylib/%s-%s-%s", MUSIALIZER_TARGET_NAME,
                            build_profile_name(build_profile),
                            build_uses_hotreload() ? "shared" : "static");
}

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

    if (!openbsd_profile_supported()) nob_return_defer(false);
    const bool hotreload = build_uses_hotreload();
    const char *raylib_path = openbsd_raylib_build_path();

    if (hotreload) {
        procs.count = 0;
            cmd.count = 0;
                // TODO: add a way to replace `cc` with something else GCC compatible on POSIX
                // Like `clang` for instance
                nob_cmd_append(&cmd, "cc");
                nob_cmd_append(&cmd, "-Wall", "-Wextra", "-DMUSIALIZER_HOTRELOAD");
                nob_cmd_append(&cmd, "-I.");
                nob_cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
                nob_cmd_append(&cmd, "-fPIC", "-shared");
                nob_cmd_append(&cmd, "-o", "./build/libplug.so");
                nob_cmd_append(&cmd, "./thirdparty/tinyfiledialogs.c");
                append_posix_plug_sources(&cmd);
                nob_cmd_append(&cmd,
                    nob_temp_sprintf("-L%s", raylib_path),
                    "-l:libraylib.so");
                append_openbsd_profile_flags(&cmd);
                nob_cmd_append(&cmd, "-lm", "-lpthread");
            nob_da_append(&procs, nob_cmd_run_async(cmd));

            cmd.count = 0;
                nob_cmd_append(&cmd, "cc");
                nob_cmd_append(&cmd, "-Wall", "-Wextra", "-DMUSIALIZER_HOTRELOAD");
                nob_cmd_append(&cmd, "-I.");
                nob_cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
                nob_cmd_append(&cmd, "-o", "./build/musializer");
                nob_cmd_append(&cmd,
                    "./src/musializer.c",
                    "./src/hotreload_posix.c");
                nob_cmd_append(&cmd,
                    "-Wl,-rpath=./build/",
                    "-Wl,-rpath=./",
                    nob_temp_sprintf("-Wl,-rpath=%s", raylib_path),
                    // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
                    nob_temp_sprintf("-Wl,-rpath=.%s", raylib_path + strlen("./build")));
                nob_cmd_append(&cmd,
                    nob_temp_sprintf("-L%s", raylib_path),
                    "-l:libraylib.so");
                append_openbsd_profile_flags(&cmd);
                nob_cmd_append(&cmd, "-lm", "-lpthread");
            nob_da_append(&procs, nob_cmd_run_async(cmd));
        if (!nob_procs_wait(procs)) nob_return_defer(false);
    } else {
        cmd.count = 0;
            nob_cmd_append(&cmd, "cc");
            nob_cmd_append(&cmd, "-Wall", "-Wextra");
            nob_cmd_append(&cmd, "-I.");
            nob_cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
            nob_cmd_append(&cmd, "-o", "./build/musializer");
            nob_cmd_append(&cmd, "./thirdparty/tinyfiledialogs.c");
            append_posix_plug_sources(&cmd);
            nob_cmd_append(&cmd, "./src/musializer.c");
            nob_cmd_append(&cmd,
                nob_temp_sprintf("-L%s", raylib_path),
                "-l:libraylib.a");
            append_openbsd_profile_flags(&cmd);
            nob_cmd_append(&cmd, "-lm", "-lpthread");
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}

bool build_raylib(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_File_Paths object_files = {0};

    if (!openbsd_profile_supported()) nob_return_defer(false);

    if (!nob_mkdir_if_not_exists("./build/raylib")) {
        nob_return_defer(false);
    }

    Nob_Procs procs = {0};

    const char *build_path = openbsd_raylib_build_path();

    if (!nob_mkdir_if_not_exists(build_path)) {
        nob_return_defer(false);
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = nob_temp_sprintf(RAYLIB_SRC_FOLDER"%s.c", raylib_modules[i]);
        const char *output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        const char *dependencies[2];
        size_t dependency_count = raylib_module_dependencies(
            raylib_modules[i], input_path, dependencies);

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, dependencies, dependency_count)) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "cc");
            nob_cmd_append(&cmd, "-w");
            nob_cmd_append(&cmd, "-ggdb", "-DPLATFORM_DESKTOP", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1");
            nob_cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER"external/glfw/include");
            nob_cmd_append(&cmd, "-c", input_path);
            nob_cmd_append(&cmd, "-o", output_path);
            nob_cmd_append(&cmd, "-I/usr/X11R6/include");
            append_openbsd_profile_flags(&cmd);
            Nob_Proc proc = nob_cmd_run_async(cmd);
            nob_da_append(&procs, proc);
        }
    }
    cmd.count = 0;

    if (!nob_procs_wait(procs)) nob_return_defer(false);

    if (!build_uses_hotreload()) {
    const char *libraylib_path = nob_temp_sprintf("%s/libraylib.a", build_path);

    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "ar", "-crs", libraylib_path);
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }
    } else {
    const char *libraylib_path = nob_temp_sprintf("%s/libraylib.so", build_path);

    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "cc");
        nob_cmd_append(&cmd, "-shared");
        nob_cmd_append(&cmd, "-o", libraylib_path);
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(object_files);
    return result;
}

bool build_dist()
{
    if (build_uses_hotreload()) {
    nob_log(NOB_ERROR, "We do not ship with hotreload enabled");
    return false;
    } else {
    if (!nob_mkdir_if_not_exists("./musializer-openbsd-x86_64/")) return false;
    if (!nob_copy_file("./build/musializer", "./musializer-openbsd-x86_64/musializer")) return false;
    if (!copy_distribution_support("./musializer-openbsd-x86_64")) return false;
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "chmod", "-R", "u=rwX,go=rX", "./musializer-openbsd-x86_64");
    if (!nob_cmd_run_sync(cmd)) return false;
    cmd.count = 0;
    nob_cmd_append(&cmd, "tar", "-czf", "./musializer-openbsd-x86_64.tar.gz",
                   "./musializer-openbsd-x86_64/musializer");
    append_distribution_support_paths(&cmd, "./musializer-openbsd-x86_64");
    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    if (!ok) return false;

    return true;
    }
}
