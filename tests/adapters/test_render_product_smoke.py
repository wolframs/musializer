import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/musializer"


@unittest.skipUnless(
    os.name == "posix"
    and APP.is_file()
    and os.access(APP, os.X_OK)
    and shutil.which("xvfb-run")
    and shutil.which("ffmpeg")
    and shutil.which("ffprobe"),
    "built Musializer, Xvfb, FFmpeg, and ffprobe are required",
)
class RenderProductSmokeTests(unittest.TestCase):
    def test_compressed_audio_uses_one_exact_transactional_av_timeline(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            audio = directory / "awkward duration.mp3"
            project = directory / "proof.musi"
            video = directory / "proof.mp4"
            subprocess.run(
                [
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i",
                    "sine=frequency=523.25:sample_rate=48000:duration=0.137",
                    "-ac", "2", "-codec:a", "libmp3lame", "-q:a", "2",
                    str(audio),
                ],
                check=True, capture_output=True, text=True, timeout=30,
            )
            source_hash = hashlib.sha256(audio.read_bytes()).hexdigest()
            completed = subprocess.run(
                [
                    "xvfb-run", "-a", str(APP), "--mute", str(audio),
                    "--scene", "constellation",
                    "--resolution", "640x360", "--fps", "24",
                    "--quality", "balanced",
                    "--save-project", str(project),
                    "--render", str(video),
                ],
                cwd=ROOT, capture_output=True, text=True, timeout=90,
            )
            if completed.returncode != 0 and (
                "Could not initialize audio device" in completed.stderr
                or "Could not initialize the rendering window" in completed.stderr
            ):
                self.skipTest("host has no usable graphical/audio runtime")
            self.assertEqual(
                completed.returncode, 0,
                msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
            )
            self.assertEqual(hashlib.sha256(audio.read_bytes()).hexdigest(),
                             source_hash)

            document = json.loads(project.read_text(encoding="utf-8"))
            bundled_audio = Path(document["audio"]["path"])
            self.assertEqual(document["audio"]["mode"], "imported")
            self.assertEqual(
                bundled_audio,
                Path("proof.assets") / "audio" / f"{source_hash}.mp3",
            )
            self.assertEqual(
                hashlib.sha256((directory / bundled_audio).read_bytes()).hexdigest(),
                source_hash,
            )
            self.assertIsNone(document["ascii_image"])
            expected_frames = math.ceil(document["audio"]["duration_seconds"]*24)
            probe = subprocess.run(
                [
                    "ffprobe", "-v", "error", "-show_streams",
                    "-show_format", "-of", "json", str(video),
                ],
                check=True, capture_output=True, text=True, timeout=30,
            )
            metadata = json.loads(probe.stdout)
            streams = {stream["codec_type"]: stream
                       for stream in metadata["streams"]}
            video_stream = streams["video"]
            audio_stream = streams["audio"]
            expected_duration = expected_frames/24.0
            self.assertEqual(int(video_stream["nb_frames"]), expected_frames)
            self.assertEqual(video_stream["codec_name"], "h264")
            self.assertEqual(video_stream["profile"], "High")
            self.assertEqual(video_stream["pix_fmt"], "yuv420p")
            self.assertEqual(video_stream["color_space"], "bt709")
            self.assertEqual(video_stream["color_transfer"], "bt709")
            self.assertEqual(video_stream["color_primaries"], "bt709")
            self.assertEqual(audio_stream["codec_name"], "aac")
            self.assertAlmostEqual(float(video_stream["duration"]),
                                   expected_duration, places=6)
            # MP4 audio/container time bases may quantize to milliseconds;
            # they must still agree with the exact video boundary within one
            # muxer tick, never a whole video frame.
            self.assertAlmostEqual(float(audio_stream["duration"]),
                                   expected_duration, delta=0.0015)
            self.assertAlmostEqual(float(metadata["format"]["duration"]),
                                   expected_duration, delta=0.0015)
            leftovers = [path.name for path in directory.iterdir()
                         if path.name.startswith(".musializer-")]
            self.assertEqual(leftovers, [])

    def test_route_arguments_apply_after_project_loading_in_either_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            audio = directory / "route order.mp3"
            base_project = directory / "base.musi"
            before_project = directory / "route-before-project.musi"
            after_project = directory / "route-after-project.musi"
            subprocess.run(
                [
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i",
                    "sine=frequency=440:sample_rate=48000:duration=0.137",
                    "-ac", "2", "-codec:a", "libmp3lame", "-q:a", "4",
                    str(audio),
                ],
                check=True, capture_output=True, text=True, timeout=30,
            )

            def run_app(arguments):
                completed = subprocess.run(
                    ["xvfb-run", "-a", str(APP), "--mute", *arguments],
                    cwd=ROOT, capture_output=True, text=True, timeout=60,
                )
                if completed.returncode != 0 and (
                    "Could not initialize audio device" in completed.stderr
                    or "Could not initialize the rendering window" in completed.stderr
                ):
                    self.skipTest("host has no usable graphical/audio runtime")
                self.assertEqual(
                    completed.returncode, 0,
                    msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
                )

            run_app([str(audio), "--save-project", str(base_project)])
            route = "pulse.weight:peak:0:0:1:1:8:smoothstep"
            run_app([
                "--route", route, "--project", str(base_project),
                "--save-project", str(before_project),
            ])
            run_app([
                "--project", str(base_project), "--route", route,
                "--save-project", str(after_project),
            ])

            def persisted_route(path):
                document = json.loads(path.read_text(encoding="utf-8"))
                return next(
                    mapping for mapping in document["scenes"][0]["mappings"]
                    if mapping["parameter"] == "settings.pulse.weight"
                )

            before = persisted_route(before_project)
            after = persisted_route(after_project)
            self.assertEqual(before, after)
            self.assertEqual(before["source"], "peak")
            self.assertEqual(before["output_min"], 1)
            self.assertEqual(before["output_max"], 8)
            self.assertEqual(before["interpolation"], "smoothstep")

    def test_render_window_matches_the_same_span_of_a_full_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            audio = directory / "window source.mp3"
            full = directory / "full.mp4"
            windowed = directory / "window.mp4"
            subprocess.run(
                [
                    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i",
                    "sine=frequency=311.13:sample_rate=48000:duration=2.0",
                    "-af", "apulsator=hz=1.5", "-ac", "2",
                    "-codec:a", "libmp3lame", "-q:a", "2", str(audio),
                ],
                check=True, capture_output=True, text=True, timeout=30,
            )
            base = [
                "xvfb-run", "-a", str(APP), "--mute", str(audio),
                "--scene", "constellation",
                "--resolution", "640x360", "--fps", "24",
                "--quality", "balanced",
            ]
            completed = subprocess.run(
                base + ["--render", str(full)],
                cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            if completed.returncode != 0 and (
                "Could not initialize audio device" in completed.stderr
                or "Could not initialize the rendering window" in completed.stderr
            ):
                self.skipTest("host has no usable graphical/audio runtime")
            self.assertEqual(
                completed.returncode, 0,
                msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
            )
            completed = subprocess.run(
                base + ["--render-window", "1.1", "0.5", "--render", str(windowed)],
                cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(
                completed.returncode, 0,
                msg=f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}",
            )

            probe = subprocess.run(
                [
                    "ffprobe", "-v", "error", "-show_streams", "-of", "json",
                    str(windowed),
                ],
                check=True, capture_output=True, text=True, timeout=30,
            )
            streams = {stream["codec_type"]: stream
                       for stream in json.loads(probe.stdout)["streams"]}
            # 1.1-1.6 s crosses fractional 24 fps boundaries. The enclosing
            # full-render span is [floor(26.4), ceil(38.4)) = [26, 39).
            enclosing_duration = 13/24
            self.assertEqual(int(streams["video"]["nb_frames"]), 13)
            self.assertAlmostEqual(float(streams["video"]["duration"]),
                                   enclosing_duration, places=6)
            self.assertAlmostEqual(float(streams["audio"]["duration"]),
                                   enclosing_duration, delta=0.0015)

            # The windowed frames must be the same source frames the full
            # export produced for that span. Two separate H.264 encodes can
            # never match byte-for-byte, so compare decoded content: pure
            # encoder noise sits far above 35 dB, while diverged analyzer,
            # beat, or scene state visibly restructures the frames and sinks
            # the minimum PSNR.
            psnr = subprocess.run(
                [
                    "ffmpeg", "-hide_banner", "-i", str(windowed),
                    "-i", str(full), "-filter_complex",
                    "[1:v]trim=start_frame=26:end_frame=39,"
                    "setpts=PTS-STARTPTS[ref];[0:v][ref]psnr",
                    "-f", "null", "-",
                ],
                check=True, capture_output=True, text=True, timeout=60,
                env={**os.environ, "LC_ALL": "C"},
            )
            summary = [line for line in psnr.stderr.splitlines()
                       if "Parsed_psnr" in line and "min:" in line]
            self.assertTrue(summary, msg=psnr.stderr)
            minimum = float(summary[-1].split("min:")[1].split()[0])
            self.assertGreater(minimum, 35.0)


if __name__ == "__main__":
    unittest.main()
