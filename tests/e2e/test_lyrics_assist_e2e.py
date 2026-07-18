"""Manual end-to-end verification of the Lyrics Assist pipeline.

This suite is intentionally NOT part of the regular automated test suites:
it launches the real whisper.cpp binary, makes a real Codex model request
over the network, and drives the built desktop binary under Xvfb. Run it
deliberately:

    python3 -m unittest discover -s tests/e2e -v

It exercises the exact production seams, with no shortcuts:

1. Synthesizes spoken audio with a known script (ffmpeg's flite filter),
   so the expected transcript is known in advance.
2. Runs tools/external_analysis.py with the same argv the in-app Assist
   button spawns (mode "lyrics": measured analysis -> Whisper -> Codex
   lyric review -> bridge TSV).
3. Validates the emitted analysis.bridge TSV against its documented v1
   grammar and checks the transcription against the known script.
4. Feeds the bridge to the real application via --analysis-bridge, which
   shares the strict parse -> candidate -> apply path with the UI Apply
   button, and persists the result with --save-project.
5. Asserts the saved .musi contains the applied lyric cues and a
   lyric_timing analysis lane, then round-trips the project through a
   second open/save to prove the persisted form is editor-supported.

Prerequisites (each missing one skips with an explicit reason):
built build/musializer (debug or release; the sanitize binary false-fails
under Xvfb due to one-time GL driver leaks), ffmpeg with the flite filter,
ffprobe, xvfb-run, whisper.cpp (MUSIALIZER_WHISPER_BIN/MODEL or the
default /tmp install), and an authenticated `codex` CLI.

Reruns are full cost: the assist workspace is a fresh temporary directory
every time, so Whisper and Codex always run live.
"""

import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/musializer"
HELPER = ROOT / "tools/external_analysis.py"

# Original spoken script; distinctive everyday words the medium.en model
# transcribes reliably from clean synthesized speech.
SCRIPT_LINES = (
    "the silver river carries every echo home",
    "morning light is written on the water",
    "we are dancing in the electric rain",
)

# Fraction of script words that must survive Whisper plus Codex review.
# Synthesized speech is clean, but review may merge or normalize words.
TRANSCRIPT_RECALL_FLOOR = 0.5


def _whisper_paths():
    install = Path("/tmp/music-visualizations-whisper-1.8.6")
    binary = os.environ.get("MUSIALIZER_WHISPER_BIN")
    model = os.environ.get("MUSIALIZER_WHISPER_MODEL")
    return (
        Path(binary) if binary else install / "build/bin/whisper-cli",
        Path(model) if model else install / "ggml-medium.en.bin",
    )


def _words(text):
    return re.findall(r"[a-z']+", text.lower())


class LyricsAssistEndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "posix":
            raise unittest.SkipTest("POSIX host required")
        if not (APP.is_file() and os.access(APP, os.X_OK)):
            raise unittest.SkipTest(
                "build/musializer is missing; run ./nob build debug first"
            )
        for tool in ("ffmpeg", "ffprobe", "xvfb-run"):
            if not shutil.which(tool):
                raise unittest.SkipTest(f"{tool} is required on PATH")
        if not shutil.which("codex"):
            raise unittest.SkipTest(
                "codex CLI is required on PATH (lyrics mode runs Codex review)"
            )
        whisper_bin, whisper_model = _whisper_paths()
        if not whisper_bin.is_file():
            raise unittest.SkipTest(
                f"whisper.cpp binary not found: {whisper_bin} "
                "(set MUSIALIZER_WHISPER_BIN)"
            )
        if not whisper_model.is_file():
            raise unittest.SkipTest(
                f"whisper model not found: {whisper_model} "
                "(set MUSIALIZER_WHISPER_MODEL)"
            )
        probe = subprocess.run(
            [
                "ffmpeg", "-hide_banner", "-loglevel", "error",
                "-f", "lavfi", "-i", "flite=text='probe':voice=slt",
                "-t", "1", "-f", "null", "-",
            ],
            capture_output=True, text=True, timeout=30,
        )
        if probe.returncode != 0:
            raise unittest.SkipTest(
                "this ffmpeg build lacks the flite speech-synthesis filter"
            )

    def _synthesize_script(self, audio):
        """Spoken script as 48 kHz stereo MP3 with silence around each line."""
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
        for line in SCRIPT_LINES:
            command += ["-f", "lavfi", "-i", f"flite=text='{line}':voice=slt"]
        stages = []
        graph = []
        for index in range(len(SCRIPT_LINES)):
            graph.append(
                f"[{index}:a]aformat=sample_rates=48000:channel_layouts=stereo,"
                f"apad=pad_dur=0.9[s{index}]"
            )
            stages.append(f"[s{index}]")
        graph.append(
            "anullsrc=r=48000:cl=stereo,atrim=duration=0.6[lead]"
        )
        graph.append(
            "[lead]" + "".join(stages) +
            f"concat=n={len(SCRIPT_LINES) + 1}:v=0:a=1[mix]"
        )
        command += [
            "-filter_complex", ";".join(graph), "-map", "[mix]",
            "-codec:a", "libmp3lame", "-q:a", "2", str(audio),
        ]
        subprocess.run(command, check=True, capture_output=True, text=True,
                       timeout=120)

    def _probe_duration(self, audio):
        completed = subprocess.run(
            [
                "ffprobe", "-hide_banner", "-loglevel", "error",
                "-show_entries", "format=duration",
                "-of", "default=noprint_wrappers=1:nokey=1", str(audio),
            ],
            check=True, capture_output=True, text=True, timeout=30,
        )
        return float(completed.stdout.strip())

    def _parse_bridge(self, bridge):
        """Minimal reader for the documented ASCII TSV bridge v1 grammar."""
        text = bridge.read_text(encoding="ascii")
        lines = text.split("\n")
        self.assertEqual(lines[0], "MUSIALIZER_BRIDGE\t1")
        self.assertEqual(lines[-1], "", msg="bridge must end with a final LF")
        audio_fields = lines[1].split("\t")
        self.assertEqual(audio_fields[0], "AUDIO")
        self.assertEqual(len(audio_fields), 3)
        cues = []
        for line in lines[2:-1]:
            fields = line.split("\t")
            if fields[0] != "LYRIC":
                continue
            self.assertEqual(len(fields), 7, msg=f"malformed LYRIC row: {line}")
            cues.append({
                "id": int(fields[1]),
                "start_ms": int(fields[2]),
                "end_ms": int(fields[3]),
                "text": base64.b64decode(fields[6], validate=True).decode("utf-8"),
            })
        return {
            "audio_sha256": audio_fields[1],
            "duration_ms": int(audio_fields[2]),
            "cues": cues,
        }

    def _run_app(self, arguments, timeout):
        completed = subprocess.run(
            ["xvfb-run", "-a", str(APP), "--mute", *arguments],
            cwd=ROOT, capture_output=True, text=True, timeout=timeout,
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
        return completed

    def _assert_script_recall(self, transcript, label):
        expected = set()
        for line in SCRIPT_LINES:
            expected.update(_words(line))
        heard = set(_words(transcript))
        recall = len(expected & heard) / len(expected)
        self.assertGreaterEqual(
            recall, TRANSCRIPT_RECALL_FLOOR,
            msg=f"{label} recall {recall:.2f} too low; heard: {transcript!r}",
        )

    def test_assist_transcribes_applies_and_persists_lyrics(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            audio = directory / "spoken script.mp3"
            workspace = directory / "assist-workspace"
            bridge = workspace / "lyrics-e2e.bridge.tsv"
            log = workspace / "lyrics-e2e.log"
            project = directory / "assisted.musi"
            roundtrip = directory / "roundtrip.musi"

            self._synthesize_script(audio)
            duration = self._probe_duration(audio)
            self.assertGreater(duration, 5.0)
            audio_sha256 = hashlib.sha256(audio.read_bytes()).hexdigest()

            # Stage 1: the helper, spawned exactly as start_assist_job does
            # (argv shape and stderr capture), on a fresh workspace so both
            # Whisper and Codex run live rather than replaying caches.
            workspace.mkdir()
            with log.open("w", encoding="utf-8") as sink:
                completed = subprocess.run(
                    [
                        sys.executable, str(HELPER), "assist",
                        str(audio), str(workspace),
                        "--duration", f"{duration:.9f}",
                        "--mode", "lyrics",
                        "--bridge", str(bridge),
                        "--timeout", "2400", "--new-process-group",
                    ],
                    cwd=ROOT, stdout=subprocess.PIPE, stderr=sink,
                    text=True, timeout=2700,
                )
            self.assertEqual(
                completed.returncode, 0,
                msg="assist helper failed; job log:\n"
                    + log.read_text(encoding="utf-8", errors="replace")[-4000:],
            )

            # Stage 2: the bridge is well-formed, identifies this exact
            # audio, and heard the words we actually spoke.
            self.assertTrue(bridge.is_file(), msg="bridge TSV was not written")
            parsed = self._parse_bridge(bridge)
            self.assertEqual(parsed["audio_sha256"], audio_sha256)
            self.assertLessEqual(
                abs(parsed["duration_ms"] - duration * 1000.0), 250.0
            )
            self.assertGreater(len(parsed["cues"]), 0,
                               msg="assist produced no lyric cues")
            for cue in parsed["cues"]:
                self.assertLess(cue["start_ms"], cue["end_ms"])
                self.assertLessEqual(cue["end_ms"],
                                     parsed["duration_ms"] + 250)
            self._assert_script_recall(
                " ".join(cue["text"] for cue in parsed["cues"]),
                "bridge transcript",
            )

            # Stage 3: the real application imports and applies the bridge
            # (the same strict parse -> candidate -> apply path as the UI
            # Apply button, including SHA-256 identity and duration checks)
            # and persists the project. A rejected bridge exits non-zero.
            self._run_app(
                [
                    str(audio),
                    "--analysis-bridge", str(bridge),
                    "--save-project", str(project),
                ],
                timeout=120,
            )
            document = json.loads(project.read_text(encoding="utf-8"))
            saved = document["lyrics"]["cues"]
            self.assertEqual(len(saved), len(parsed["cues"]))
            for saved_cue, bridge_cue in zip(saved, parsed["cues"]):
                self.assertEqual(saved_cue["text"], bridge_cue["text"])
                self.assertEqual(round(saved_cue["start_seconds"] * 1000.0),
                                 bridge_cue["start_ms"])
                self.assertEqual(round(saved_cue["end_seconds"] * 1000.0),
                                 bridge_cue["end_ms"])
            self._assert_script_recall(
                " ".join(cue["text"] for cue in saved), "saved project",
            )
            lanes = [lane for lane in document["analysis_lanes"]
                     if lane["kind"] == "lyric_timing"]
            self.assertEqual(len(lanes), 1,
                             msg="applied bridge must record a lyric_timing lane")
            self.assertEqual(lanes[0]["audio_sha256"], audio_sha256)
            self.assertEqual(Path(lanes[0]["path"]).name, bridge.name)

            # Stage 4: the persisted project must be editor-supported; a
            # second open/save round-trip preserves the applied lyrics.
            self._run_app(
                [
                    "--project", str(project),
                    "--save-project", str(roundtrip),
                ],
                timeout=120,
            )
            reopened = json.loads(roundtrip.read_text(encoding="utf-8"))
            self.assertEqual(
                [(c["text"], round(c["start_seconds"] * 1000.0),
                  round(c["end_seconds"] * 1000.0))
                 for c in reopened["lyrics"]["cues"]],
                [(c["text"], round(c["start_seconds"] * 1000.0),
                  round(c["end_seconds"] * 1000.0))
                 for c in saved],
            )


if __name__ == "__main__":
    unittest.main()
