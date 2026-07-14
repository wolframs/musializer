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
        self.assertIn('"-0.1 s"', plug)
        self.assertIn('"+0.1 s"', plug)
        self.assertIn("2.0f, COLOR_TIMELINE_CURSOR", plug)

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
