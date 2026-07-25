"""Command-line flags configure a session; they must not edit the project.

Every state-setting flag routes through mark_project_dirty, and the workspace
autosaves a dirty project about 1.5 seconds after the change. Together those made
`--project show.musi --scene loom` rewrite the opened file with no interaction at
all, replacing its base scene and disabling its saved automatic scene plan.
Persisting startup state is opt-in through --save-project.
"""

import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/musializer"

# The autosave poll commits 1.5 s after a project is marked dirty. Hold the
# window open comfortably past that so a regression cannot hide behind timing.
AUTOSAVE_OBSERVATION_SECONDS = 5.0

RUNTIME_UNAVAILABLE = (
    "Could not initialize audio device",
    "Could not initialize the rendering window",
)


@unittest.skipUnless(
    os.name == "posix"
    and APP.is_file()
    and os.access(APP, os.X_OK)
    and shutil.which("xvfb-run")
    and shutil.which("ffmpeg"),
    "built Musializer, Xvfb, and FFmpeg are required",
)
class CommandLineSessionTests(unittest.TestCase):
    def build_project(self, directory):
        audio = directory / "session.wav"
        project = directory / "session.musi"
        subprocess.run(
            [
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-f", "lavfi", "-i",
                "sine=frequency=440:sample_rate=48000:duration=2",
                "-ac", "2", str(audio),
            ],
            check=True, capture_output=True, text=True, timeout=30,
        )
        completed = subprocess.run(
            [
                "xvfb-run", "-a", str(APP), "--mute", str(audio),
                "--scene", "spectrum", "--resolution", "640x360", "--fps", "24",
                "--quality", "balanced", "--save-project", str(project),
            ],
            cwd=ROOT, capture_output=True, text=True, timeout=90,
        )
        if completed.returncode != 0 and any(
            reason in completed.stderr for reason in RUNTIME_UNAVAILABLE
        ):
            self.skipTest("host has no usable graphical/audio runtime")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(project.is_file())
        return project

    def sweep(self, marker):
        """Kill any surviving app process launched for this test.

        A leaked instance is not harmless: headless under Xvfb there is no vsync,
        so it spins a full core forever. Six of them once put this workstation at
        load 33. The marker is the test's unique temporary directory, so this
        cannot match an application the operator is running themselves.
        """
        killed = []
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                cmdline = (entry / "cmdline").read_bytes().decode("utf-8", "replace")
            except OSError:
                continue  # Exited while we looked, or not ours to read.
            if marker in cmdline and "musializer" in cmdline:
                try:
                    os.kill(int(entry.name), signal.SIGKILL)
                    killed.append(entry.name)
                except (OSError, ValueError):
                    pass
        return killed

    def hold_open(self, *arguments):
        """Run the app long enough for an autosave to fire, then stop it."""
        # xvfb-run is a wrapper script, so signalling it alone orphans the
        # application it launched. Own the whole process group instead.
        process = subprocess.Popen(
            ["xvfb-run", "-a", str(APP), "--mute", *arguments],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            start_new_session=True,
        )
        try:
            output = process.communicate(timeout=AUTOSAVE_OBSERVATION_SECONDS)[0]
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                output = process.communicate(timeout=20)[0] or ""
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                output = process.communicate(timeout=20)[0] or ""
        else:
            # Exiting early means the arguments were rejected, so the window the
            # autosave needs never existed and the assertion would prove nothing.
            if any(reason in output for reason in RUNTIME_UNAVAILABLE):
                self.skipTest("host has no usable graphical/audio runtime")
            self.fail(f"application exited before the autosave window: {output}")
        return output

    def test_startup_flags_do_not_rewrite_the_opened_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.addCleanup(self.sweep, str(directory))
            project = self.build_project(directory)
            original = hashlib.sha256(project.read_bytes()).hexdigest()

            # Each of these used to be committed to the opened file unprompted.
            for arguments in (
                ["--scene", "loom"],
                ["--resolution", "2560x1440"],
                ["--fps", "60"],
                ["--quality", "master"],
                ["--event", "lyric:1.25:42:0.9"],
            ):
                with self.subTest(flag=arguments[0]):
                    self.hold_open("--project", str(project), *arguments,
                                   "--ui-probe", "panel=none,size=640x480")
                    self.assertEqual(
                        hashlib.sha256(project.read_bytes()).hexdigest(), original,
                        f"{arguments[0]} rewrote the opened project",
                    )

    def test_opening_a_project_alone_leaves_it_untouched(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.addCleanup(self.sweep, str(directory))
            project = self.build_project(directory)
            original = hashlib.sha256(project.read_bytes()).hexdigest()
            self.hold_open("--project", str(project),
                           "--ui-probe", "panel=none,size=640x480")
            self.assertEqual(
                hashlib.sha256(project.read_bytes()).hexdigest(), original)

    def test_save_project_still_persists_startup_configuration(self):
        """The opt-in path has to keep working, or the fix above is a regression."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.addCleanup(self.sweep, str(directory))
            project = self.build_project(directory)
            derived = directory / "derived.musi"
            completed = subprocess.run(
                [
                    "xvfb-run", "-a", str(APP), "--mute",
                    "--project", str(project),
                    "--scene", "loom", "--quality", "master",
                    "--save-project", str(derived),
                ],
                cwd=ROOT, capture_output=True, text=True, timeout=90,
            )
            if completed.returncode != 0 and any(
                reason in completed.stderr for reason in RUNTIME_UNAVAILABLE
            ):
                self.skipTest("host has no usable graphical/audio runtime")
            self.assertEqual(completed.returncode, 0, completed.stderr)

            saved = json.loads(derived.read_text(encoding="utf-8"))
            self.assertIn("loom", [s.get("scene_type") for s in saved["scenes"]])
            self.assertEqual(saved["output"]["quality"], "master")
            # The source project is still the one the operator last saved.
            source = json.loads(project.read_text(encoding="utf-8"))
            self.assertNotIn("loom", [s.get("scene_type") for s in source["scenes"]])


if __name__ == "__main__":
    unittest.main()
