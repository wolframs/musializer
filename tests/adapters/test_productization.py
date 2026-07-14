import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DistributionManifestTests(unittest.TestCase):
    def test_support_manifest_covers_product_runtime_files(self):
        source = (ROOT / "src_build" / "nob_stage2.c").read_text(encoding="utf-8")
        match = re.search(
            r"static const char \*distribution_support_files\[\] = \{(.*?)\n\};",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        actual = set(re.findall(r'"([^"\\]+)"', match.group(1)))
        expected = {
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
        }
        expected.update(
            path.relative_to(ROOT).as_posix()
            for pattern in ("*.py", "*.md")
            for path in (ROOT / "tools").glob(pattern)
        )
        expected.update(
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "prompts").glob("*")
            if path.is_file()
        )
        expected.update(
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "schemas").glob("*.json")
        )
        self.assertEqual(actual, expected)

    def test_release_profiles_preserve_non_finite_validation(self):
        recipes = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (ROOT / "src_build").glob("nob_*.c")
        )
        for unsafe in ("-ffast-math", "-Ofast", "/fp:fast"):
            self.assertNotIn(unsafe, recipes)

    def test_scaled_windows_use_physical_framebuffer_viewport(self):
        host = (ROOT / "src" / "musializer.c").read_text(encoding="utf-8")
        glfw = (
            ROOT / "thirdparty/raylib-5.5/src/platforms/rcore_desktop_glfw.c"
        ).read_text(encoding="utf-8")

        self.assertIn("FLAG_WINDOW_HIGHDPI", host)
        self.assertIn("glfwSetFramebufferSizeCallback", glfw)
        build = (ROOT / "src_build/nob_stage2.c").read_text(encoding="utf-8")
        self.assertIn("raylib_module_dependencies", build)
        self.assertIn('platforms/rcore_desktop_glfw.c', build)
        callback = re.search(
            r"static void FramebufferSizeCallback\([^;]+?\)\s*\{.*?\n\}",
            glfw,
            re.DOTALL,
        )
        self.assertIsNotNone(callback)
        self.assertIn("SetupViewport(width, height)", callback.group(0))
        self.assertIn("rlSetFramebufferWidth(width)", callback.group(0))
        self.assertIn("rlSetFramebufferHeight(height)", callback.group(0))
        self.assertIn("CORE.Window.screenScale = MatrixScale", callback.group(0))

    def test_timeline_waveform_and_precise_seek_are_built_for_every_target(self):
        build = (ROOT / "src_build/nob_stage2.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertEqual(build.count('"./src/track_timeline.c"'), 2)
        self.assertIn("track_timeline_build_waveform", plug)
        self.assertIn("track_timeline_seek_from_x", plug)
        self.assertIn("track_timeline_path_is_seekable", plug)
        self.assertIn('"-0.1 s"', plug)
        self.assertIn('"+0.1 s"', plug)
        self.assertIn("2.0f, COLOR_TIMELINE_CURSOR", plug)
        seek = re.search(
            r"static void seek_track_to\([^;]+?\)\s*\{.*?\n\}",
            plug,
            re.DOTALL,
        )
        self.assertIsNotNone(seek)
        seek_body = seek.group(0)
        ordered = [
            "StopMusicStream(track->music)",
            "SeekMusicStream(track->music",
            "UpdateMusicStream(track->music)",
            "PlayMusicStream(track->music)",
            "fft_clean()",
        ]
        positions = [seek_body.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("if (!was_playing) PauseMusicStream(track->music)", seek_body)
        self.assertIn("p->timeline_scrubbing = true", plug)
        scrub_start = plug.index(
            "const uint64_t drag_id = UINT64_C(0x5345454B44524147)"
        )
        scrub_end = plug.index("// TODO: enable the user to render", scrub_start)
        scrub = plug[scrub_start:scrub_end]
        release = scrub.index("if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))")
        committed_seek = scrub.index("seek_track_to(track, target)", release)
        self.assertLess(release, committed_seek)

    def test_assist_content_and_provenance_commit_as_one_operation(self):
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")
        apply_start = plug.index("static bool apply_assist_candidate(void)\n{")
        apply_end = plug.index("static void discard_assist_candidate", apply_start)
        apply = plug[apply_start:apply_end]
        self.assertLess(
            apply.index("stage_candidate_analysis_lanes("),
            apply.index("apply_candidate_to_track("),
        )
        self.assertLess(
            apply.index("apply_candidate_to_track("),
            apply.index("memcpy(track->analysis_lanes, staged"),
        )
        self.assertNotIn("track_set_analysis_lane", plug)

    def test_project_save_bundles_audio_and_ascii_sources_before_publication(self):
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")
        save_start = plug.index("static bool save_project_to_path(")
        save_end = plug.index("static bool save_project_as(", save_start)
        save = plug[save_start:save_end]
        self.assertGreaterEqual(save.count("musi_project_bundle_asset("), 2)
        self.assertLess(
            save.index("musi_project_bundle_asset("),
            save.index("musi_project_atomic_write("),
        )
        self.assertNotIn("not project-portable yet", save)
        self.assertIn("MUSI_ASSET_IMPORTED", plug)
        self.assertIn("musi_project_resolve_bundled_asset_path", plug)

    def test_assist_state_policy_is_wired_into_every_product_target(self):
        build = (ROOT / "src_build/nob_stage2.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertEqual(build.count('"./src/assist_ui_state.c"'), 2)
        self.assertIn('#include "assist_ui_state.h"', plug)
        self.assertIn("assist_start_block(", plug)
        self.assertIn("disabled_text_button(", plug)
        self.assertIn("p->assist_confirmation_pending = true", plug)
        self.assertIn("ASSIST_JOB_CANCELLING", plug)
        self.assertIn("request_assist_job_cancel();", plug)
        self.assertIn("draw_fullscreen_assist_status(preview_boundary);", plug)
        self.assertIn("assist_timeline_height(", plug)
        render_start = plug.index("static bool start_rendering_track_to")
        render_body = plug[render_start:plug.index("static void start_rendering_track(",
                                                   render_start)]
        self.assertIn("assist_job_is_active(p->assist_job_state)", render_body)

    def test_preview_audio_has_refill_headroom_before_main_thread_work(self):
        host = (ROOT / "src/musializer.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        buffer_match = re.search(
            r"#define PREVIEW_AUDIO_BUFFER_FRAMES\s+(\d+)", host
        )
        self.assertIsNotNone(buffer_match)
        self.assertGreaterEqual(int(buffer_match.group(1)), 4096)
        self.assertLess(
            host.index("InitAudioDevice()"),
            host.index("SetAudioStreamBufferSizeDefault(PREVIEW_AUDIO_BUFFER_FRAMES)"),
        )
        self.assertLess(
            host.index("SetAudioStreamBufferSizeDefault(PREVIEW_AUDIO_BUFFER_FRAMES)"),
            host.index("plug_init()"),
        )
        preview = re.search(
            r"static void preview_screen\(void\)\s*\{.*?\n\}", plug, re.DOTALL
        )
        self.assertIsNotNone(preview)
        self.assertLess(
            preview.group(0).index("UpdateMusicStream(early_track->music)"),
            preview.group(0).index("poll_assist_job()"),
        )
        start = re.search(
            r"static void start_preview_track\([^;]+?\)\s*\{.*?\n\}",
            plug,
            re.DOTALL,
        )
        self.assertIsNotNone(start)
        self.assertLess(
            start.group(0).index("UpdateMusicStream(track->music)"),
            start.group(0).index("PlayMusicStream(track->music)"),
        )

        load_start = plug.index("MUSIALIZER_PLUG bool plug_load_track(")
        load_end = plug.index("MUSIALIZER_PLUG bool plug_load_ascii_image(",
                              load_start)
        body = plug[load_start:load_end]
        self.assertLess(body.index("PauseMusicStream(active_track->music)"),
                        body.index("sha256_file_hex(canonical_path"))
        self.assertLess(body.index("sha256_file_hex(canonical_path"),
                        body.index("load_timeline_waveform(new_track)"))
        self.assertLess(body.index("load_timeline_waveform(new_track)"),
                        body.index("ResumeMusicStream(active_track->music)"))

    def test_song_atlas_analysis_is_lazy_in_preview_and_reuses_export_pcm(self):
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")
        waveform_start = plug.index("static void load_timeline_waveform(")
        waveform_end = plug.index("static bool ensure_song_atlas_map(",
                                  waveform_start)
        self.assertNotIn("song_atlas_map_build(",
                         plug[waveform_start:waveform_end])

        lazy_start = waveform_end
        lazy_end = plug.index("static bool track_uses_song_atlas(", lazy_start)
        lazy = plug[lazy_start:lazy_end]
        self.assertIn("PauseMusicStream(track->music)", lazy)
        self.assertIn("song_atlas_map_build(", lazy)
        self.assertIn("UpdateMusicStream(track->music)", lazy)
        self.assertIn("ResumeMusicStream(track->music)", lazy)

        render_start = plug.index("static bool start_rendering_track_to(")
        render_end = plug.index("static void start_rendering_track(", render_start)
        render = plug[render_start:render_end]
        self.assertLess(render.index("LoadWaveSamples(wave)"),
                        render.index("song_atlas_map_build("))

    def test_ui_uses_bundled_readable_font_with_license(self):
        build = (ROOT / "src_build/nob_stage2.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn("resources/fonts/SpaceGrotesk-Regular.otf", build)
        self.assertIn("resources/fonts/SpaceGrotesk-OFL.txt", build)
        self.assertIn("SpaceGrotesk-Regular.otf", plug)
        self.assertIn("static Font ui_font(void)", plug)
        self.assertEqual(plug.count("GetFontDefault()"), 1)

    def test_export_keeps_exact_video_frames_and_color_contract_cross_platform(self):
        for filename in ("ffmpeg_posix.c", "ffmpeg_windows.c"):
            source = (ROOT / "src" / filename).read_text(encoding="utf-8")
            self.assertIn('\"-af\", \"apad\"', source)
            self.assertIn('\"-t\"', source)
            self.assertIn('\"-profile:v\", \"high\"', source)
            self.assertIn('\"-pix_fmt\", \"yuv420p\"', source)
            self.assertIn('\"-color_primaries\", \"bt709\"', source)

        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")
        failure = plug.index("if (!ffmpeg_send_frame_flipped")
        cleanup = plug.index("finish_rendering_track(track);", failure)
        self.assertIn("ffmpeg_end_rendering(p->ffmpeg, true)",
                      plug[failure:cleanup])

    def test_macos_bundle_executable_name_matches_packaged_file(self):
        info = plistlib.loads((ROOT / "src_build/Info.plist").read_bytes())
        executable = info["CFBundleExecutable"]
        recipe = (ROOT / "src_build/nob_macos.c").read_text(encoding="utf-8")
        self.assertIn(f"Contents/MacOS/{executable}", recipe)

    def test_macos_bundle_places_complete_assist_tree_beside_executable(self):
        recipe = (ROOT / "src_build/nob_macos.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        support_root = "./build/Musializer.app/Contents/MacOS"
        self.assertIn(f'copy_distribution_support("{support_root}")', recipe)
        self.assertIn('"%s/tools/external_analysis.py"', plug)

        # The helper derives its root from its own path, so copying the shared
        # manifest here also keeps prompts/, schemas/, and sibling tools at the
        # relative locations used by every Assist mode.
        manifest = (ROOT / "src_build/nob_stage2.c").read_text(encoding="utf-8")
        match = re.search(
            r"static const char \*distribution_support_files\[\] = \{(.*?)\n\};",
            manifest,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        support_files = set(re.findall(r'"([^"\\]+)"', match.group(1)))
        self.assertIn("tools/external_analysis.py", support_files)
        self.assertTrue(any(path.startswith("prompts/") for path in support_files))
        self.assertTrue(any(path.startswith("schemas/") for path in support_files))


@unittest.skipUnless(os.name == "posix" and shutil.which("sh"), "POSIX shell required")
class LinuxLauncherTests(unittest.TestCase):
    def test_installer_and_launcher_handle_shell_and_sed_metacharacters(self):
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            project = temp / "project & pipe|quote' space"
            for relative in (
                "tools",
                "packaging/linux",
                "resources/logo",
                "build",
            ):
                (project / relative).mkdir(parents=True, exist_ok=True)

            shutil.copy2(ROOT / "tools/install-linux-launcher.sh",
                         project / "tools/install-linux-launcher.sh")
            shutil.copy2(ROOT / "tools/musializer-launcher",
                         project / "tools/musializer-launcher")
            shutil.copy2(ROOT / "packaging/linux/io.github.tsoding.musializer.desktop.in",
                         project / "packaging/linux/io.github.tsoding.musializer.desktop.in")
            shutil.copy2(ROOT / "packaging/linux/io.github.tsoding.musializer.xml",
                         project / "packaging/linux/io.github.tsoding.musializer.xml")
            shutil.copy2(ROOT / "resources/logo/logo-256.png",
                         project / "resources/logo/logo-256.png")

            fake_app = project / "build/musializer"
            fake_app.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$PWD\" \"$@\" > invoked.txt\n",
                encoding="utf-8",
            )
            fake_app.chmod(fake_app.stat().st_mode | stat.S_IXUSR)

            env = os.environ.copy()
            env.update({
                "HOME": str(temp / "home"),
                "XDG_DATA_HOME": str(temp / "data"),
                "XDG_BIN_HOME": str(temp / "bin"),
                "XDG_STATE_HOME": str(temp / "state"),
            })
            installer = project / "tools/install-linux-launcher.sh"
            subprocess.run(
                ["sh", str(installer), "--skip-build", "--no-refresh"],
                check=True,
                env=env,
                capture_output=True,
                text=True,
            )

            launcher = temp / "bin/musializer"
            desktop = temp / "data/applications/io.github.tsoding.musializer.desktop"
            mime = temp / "data/mime/packages/io.github.tsoding.musializer.xml"
            self.assertTrue(os.access(launcher, os.X_OK))
            self.assertNotIn("@PROJECT_ROOT", launcher.read_text(encoding="utf-8"))
            self.assertNotIn("@APP_RELATIVE@", launcher.read_text(encoding="utf-8"))
            self.assertNotIn("@LAUNCHER@", desktop.read_text(encoding="utf-8"))
            self.assertTrue(mime.is_file())
            self.assertIn("application/x-musializer-project",
                          desktop.read_text(encoding="utf-8"))
            self.assertIn('glob pattern="*.musi"',
                          mime.read_text(encoding="utf-8"))

            subprocess.run(
                [str(launcher), "song one.mp3", "song-two.flac"],
                check=True,
                env=env,
                capture_output=True,
                text=True,
            )
            invocation = (project / "invoked.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(invocation, [str(project), "song one.mp3", "song-two.flac"])

            subprocess.run(
                ["sh", str(installer), "--uninstall", "--no-refresh"],
                check=True,
                env=env,
                capture_output=True,
                text=True,
            )
            self.assertFalse(launcher.exists())
            self.assertFalse(desktop.exists())
            self.assertFalse(mime.exists())

    def test_portable_archive_installs_without_build_tooling(self):
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            package = temp / "musializer package"
            for relative in ("tools", "packaging/linux", "resources/logo"):
                (package / relative).mkdir(parents=True, exist_ok=True)
            for relative in (
                "tools/install-linux-launcher.sh",
                "tools/musializer-launcher",
                "packaging/linux/io.github.tsoding.musializer.desktop.in",
                "packaging/linux/io.github.tsoding.musializer.xml",
                "resources/logo/logo-256.png",
            ):
                shutil.copy2(ROOT / relative, package / relative)

            fake_app = package / "musializer"
            fake_app.write_text(
                "#!/bin/sh\nprintf '%s\\n' \"$PWD\" \"$@\" > invoked.txt\n",
                encoding="utf-8",
            )
            fake_app.chmod(fake_app.stat().st_mode | stat.S_IXUSR)
            env = os.environ.copy()
            env.update({
                "HOME": str(temp / "home"),
                "XDG_DATA_HOME": str(temp / "data"),
                "XDG_BIN_HOME": str(temp / "bin"),
                "XDG_STATE_HOME": str(temp / "state"),
                "PATH": "/usr/bin:/bin",
            })
            subprocess.run(
                ["sh", str(package / "tools/install-linux-launcher.sh"),
                 "--no-refresh"],
                check=True, env=env, capture_output=True, text=True,
            )
            launcher = temp / "bin/musializer"
            self.assertIn('APP="$PROJECT_ROOT/musializer"',
                          launcher.read_text(encoding="utf-8"))
            subprocess.run(
                [str(launcher), "portable.musi"], check=True, env=env,
                capture_output=True, text=True,
            )
            self.assertEqual(
                (package / "invoked.txt").read_text(encoding="utf-8").splitlines(),
                [str(package), "portable.musi"],
            )


if __name__ == "__main__":
    unittest.main()
