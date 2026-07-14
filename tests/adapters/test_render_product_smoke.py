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
                    "xvfb-run", "-a", str(APP), str(audio),
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


if __name__ == "__main__":
    unittest.main()
