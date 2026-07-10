from __future__ import annotations

import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import analysis_io  # noqa: E402
import analyze_audio as measured  # noqa: E402


def click_track(sample_rate: int, seconds: float, bpm: float = 120.0) -> np.ndarray:
    samples = np.zeros(round(sample_rate * seconds), dtype=np.float64)
    width = max(8, round(sample_rate * 0.0125))
    for beat in np.arange(0.0, seconds, 60.0 / bpm):
        start = round(beat * sample_rate)
        end = min(samples.size, start + width)
        samples[start:end] += np.hanning((end - start) * 2)[: end - start]
    return samples


def write_stereo_wav(path: Path, samples: np.ndarray, sample_rate: int) -> None:
    samples = np.asarray(samples)
    left = np.clip(samples, -1.0, 1.0)
    right = np.clip(-0.5 * samples, -1.0, 1.0)
    interleaved = np.column_stack((left, right)).reshape(-1)
    pcm = b"".join(struct.pack("<h", round(float(value) * 32767)) for value in interleaved)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(pcm)


class MeasuredAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_sine_features_are_finite_normalized_and_frequency_selective(self):
        sample_rate = 8000
        time = np.arange(sample_rate * 2) / sample_rate
        pcm = 0.5 * np.sin(2 * np.pi * 1000 * time)
        result = measured.analyze_pcm(
            pcm,
            audio_sha256="a" * 64,
            sample_rate=sample_rate,
            window_size=512,
            hop_size=128,
        )

        measured.validate_document(result)
        self.assertEqual(result["schema_version"], measured.SCHEMA_VERSION)
        self.assertEqual(result["lane"], "measured_audio")
        self.assertGreater(result["summary"]["global"]["bands_mean"]["mid"], 0.9)
        self.assertAlmostEqual(result["summary"]["global"]["centroid_mean"], 0.25, delta=0.02)
        rendered = json.dumps(result, allow_nan=False)
        self.assertNotIn("NaN", rendered)
        self.assertNotIn("Infinity", rendered)
        for frame in result["frames"]:
            values = [
                frame["rms"], frame["peak"], frame["spectral_centroid"],
                frame["spectral_flux"], frame["onset_strength"], frame["pulse"],
                *frame["bands"].values(),
            ]
            self.assertTrue(all(math.isfinite(value) and 0 <= value <= 1 for value in values))

    def test_click_track_produces_conservative_pulse_and_complete_summaries(self):
        sample_rate = 8000
        result = measured.analyze_pcm(
            click_track(sample_rate, 12),
            audio_sha256="b" * 64,
            sample_rate=sample_rate,
            window_size=512,
            hop_size=128,
        )

        pulse = result["pulse_estimate"]
        self.assertIsNotNone(pulse["bpm"])
        self.assertAlmostEqual(pulse["bpm"], 120, delta=3)
        self.assertGreater(pulse["confidence"], 0.25)
        self.assertTrue(any(frame["onset"] for frame in result["frames"]))
        duration = result["audio"]["duration_seconds"]
        sections = result["summary"]["sections"]
        self.assertEqual(sections[0]["start_seconds"], 0)
        self.assertEqual(sections[-1]["end_seconds"], duration)
        for left, right in zip(sections, sections[1:]):
            self.assertEqual(left["end_seconds"], right["start_seconds"])
        for resolution in result["summary"]["resolutions"]:
            self.assertEqual(resolution["bins"][0]["start_seconds"], 0)
            self.assertEqual(resolution["bins"][-1]["end_seconds"], duration)

    def test_silence_does_not_invent_a_beat(self):
        result = measured.analyze_pcm(
            np.zeros(8000 * 5),
            audio_sha256="c" * 64,
            sample_rate=8000,
            window_size=512,
            hop_size=128,
        )
        self.assertIsNone(result["pulse_estimate"]["bpm"])
        self.assertEqual(result["pulse_estimate"]["confidence"], 0)
        self.assertTrue(all(frame["spectral_flux"] == 0 for frame in result["frames"]))

    @unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg is not installed")
    def test_ffmpeg_decodes_generated_wav_to_deterministic_mono_or_stereo(self):
        source_rate = 16000
        time = np.arange(source_rate) / source_rate
        source = 0.25 * np.sin(2 * np.pi * 440 * time)
        audio = self.directory / "generated.wav"
        write_stereo_wav(audio, source, source_rate)

        mono = measured.decode_audio(audio, sample_rate=8000, channels=1)
        stereo = measured.decode_audio(audio, sample_rate=8000, channels=2)
        repeated = measured.decode_audio(audio, sample_rate=8000, channels=1)
        self.assertEqual(mono.shape, (8000, 1))
        self.assertEqual(stereo.shape, (8000, 2))
        np.testing.assert_array_equal(mono, repeated)
        self.assertTrue(np.all(np.isfinite(stereo)))

    @unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg is not installed")
    def test_cache_key_is_reproducible_and_atomic_failure_preserves_cache(self):
        sample_rate = 8000
        audio = self.directory / "click.wav"
        write_stereo_wav(audio, click_track(sample_rate, 2), sample_rate)
        cache = self.directory / "measured.json"

        first = measured.analyze_to_cache(
            audio, cache, sample_rate=sample_rate, channels=1,
            window_size=512, hop_size=128,
        )
        first_bytes = cache.read_bytes()
        second = measured.analyze_to_cache(
            audio, cache, sample_rate=sample_rate, channels=1,
            window_size=512, hop_size=128,
        )
        self.assertEqual(first["cache_key"], second["cache_key"])
        self.assertEqual(first_bytes, cache.read_bytes())

        with self.assertRaises(analysis_io.AnalysisValidationError):
            measured.analyze_to_cache(
                audio, cache, sample_rate=sample_rate, channels=1,
                window_size=512, hop_size=128, ffmpeg="definitely-not-ffmpeg",
            )
        self.assertEqual(first_bytes, cache.read_bytes())

    def test_schema_is_valid_json_and_declares_cross_item_invariants(self):
        schema_path = ROOT / "schemas" / "measured-analysis-v1.schema.json"
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertEqual(
            schema["properties"]["schema_version"]["const"],
            measured.SCHEMA_VERSION,
        )
        self.assertEqual(
            schema["properties"]["summary"]["properties"]["sections"]["x-musializer-coverage"],
            "contiguous-full-duration",
        )

    def test_cli_writes_json_without_network_or_external_fixture(self):
        if not shutil.which("ffmpeg"):
            self.skipTest("FFmpeg is not installed")
        sample_rate = 8000
        audio = self.directory / "tone.wav"
        output = self.directory / "tone.analysis.json"
        time = np.arange(sample_rate // 2) / sample_rate
        write_stereo_wav(audio, 0.2 * np.sin(2 * np.pi * 220 * time), sample_rate)
        completed = subprocess.run(
            [
                sys.executable, str(ROOT / "tools" / "analyze_audio.py"),
                str(audio), str(output), "--sample-rate", "8000",
                "--window", "512", "--hop", "128",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(output.read_text())["lane"], "measured_audio")


if __name__ == "__main__":
    unittest.main()
