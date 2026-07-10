from __future__ import annotations

import io
import hashlib
import json
import os
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import analysis_io  # noqa: E402
import import_whisper  # noqa: E402
import mimo_openrouter as mimo  # noqa: E402


def creative(**updates):
    value = {
        "summary": "Restless neon machinery becoming weightless",
        "moods": ["playful", "uncanny"],
        "energy": 0.8,
        "tension": 0.6,
        "valence": 0.2,
        "motion": ["stutter", "spiral"],
        "textures": ["pixelated", "metallic"],
        "imagery": ["an arcade growing wings"],
        "palette": ["#ff00cc", "electric blue"],
        "scene_cues": ["fracture the tunnel"],
        "confidence": 0.75,
    }
    value.update(updates)
    return value


def response_for(score, **metadata):
    return {
        "id": "generation-test",
        "model": mimo.MODEL,
        "provider": "MockProvider",
        "choices": [{"message": {"content": json.dumps(score)}}],
        **metadata,
    }


class FakeResponse:
    def __init__(self, value):
        self.payload = json.dumps(value).encode("utf-8")

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self):
        return self.payload


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.audio = self.directory / "sample.mp3"
        self.audio.write_bytes(b"synthetic-not-real-audio")
        self.cache = self.directory / "score.cache.json"

    def tearDown(self):
        self.temp.cleanup()

    def test_mimo_success_clamps_values_and_writes_raw_and_normalized_atomically(self):
        score = {
            "global": creative(energy=4, valence=-9),
            "segments": [creative(start_seconds=-2, end_seconds=99, confidence=2)],
        }
        seen = {}

        def transport(request, timeout):
            seen["timeout"] = timeout
            seen["authorization"] = request.get_header("Authorization")
            return FakeResponse(response_for(score, usage={"prompt_tokens": 1}))

        with mock.patch.dict(os.environ, {"OPENROUTER_API_KEY": "test-only-secret"}, clear=False):
            envelope = mimo.analyze_to_cache(
                self.audio,
                self.cache,
                audio_duration=12.5,
                transport=transport,
                sleeper=lambda _delay: None,
            )

        on_disk = json.loads(self.cache.read_text(encoding="utf-8"))
        self.assertEqual(on_disk, envelope)
        self.assertEqual(seen["timeout"], 600.0)
        self.assertEqual(seen["authorization"], "Bearer test-only-secret")
        self.assertEqual(envelope["raw_response"]["provider"], "MockProvider")
        normalized = envelope["normalized"]
        self.assertEqual(normalized["lane"], "semantic_interpretation")
        self.assertEqual(normalized["global"]["energy"], 1)
        self.assertEqual(normalized["global"]["valence"], -1)
        self.assertEqual(normalized["segments"][0]["start_seconds"], 0)
        self.assertEqual(normalized["segments"][0]["end_seconds"], 12.5)
        self.assertEqual(normalized["segments"][0]["confidence"], 1)

    def test_mimo_hashes_the_exact_audio_snapshot_it_submits(self):
        original = b"first immutable audio snapshot"
        replacement = b"file contents changed after the read"
        self.audio.write_bytes(original)
        score = {
            "global": creative(),
            "segments": [creative(start_seconds=0, end_seconds=10)],
        }
        submitted = {}

        class MutatingAudioPath:
            suffix = ".mp3"

            def __fspath__(_self):
                return str(self.audio)

            def read_bytes(_self):
                snapshot = self.audio.read_bytes()
                self.audio.write_bytes(replacement)
                return snapshot

        def transport(request, timeout):
            del timeout
            payload = json.loads(request.data.decode("utf-8"))
            submitted["audio"] = payload["messages"][0]["content"][1]["input_audio"]["data"]
            return FakeResponse(response_for(score))

        with mock.patch.dict(os.environ, {"OPENROUTER_API_KEY": "test-only-secret"}, clear=False):
            envelope = mimo.analyze_to_cache(
                MutatingAudioPath(),
                self.cache,
                audio_duration=10,
                transport=transport,
                sleeper=lambda _delay: None,
            )

        expected_hash = hashlib.sha256(original).hexdigest()
        self.assertEqual(submitted["audio"], base64_text(original))
        self.assertEqual(envelope["request"]["audio_sha256"], expected_hash)
        self.assertEqual(envelope["normalized"]["audio"]["sha256"], expected_hash)
        self.assertEqual(self.audio.read_bytes(), replacement)

    def test_malformed_model_output_does_not_replace_existing_cache(self):
        original = b'{"known_good":true}\n'
        self.cache.write_bytes(original)
        malformed = response_for({"global": creative(), "segments": "not-an-array"})

        def transport(_request, _timeout=None, **_kwargs):
            return FakeResponse(malformed)

        with mock.patch.dict(os.environ, {"OPENROUTER_API_KEY": "test-only-secret"}, clear=False):
            with self.assertRaises(analysis_io.AnalysisValidationError):
                mimo.analyze_to_cache(
                    self.audio,
                    self.cache,
                    audio_duration=10,
                    transport=transport,
                    sleeper=lambda _delay: None,
                )
        self.assertEqual(self.cache.read_bytes(), original)

    def test_transient_http_error_honors_retry_after_then_succeeds(self):
        score = {"global": creative(), "segments": [creative(start_seconds=0, end_seconds=10)]}
        calls = []
        delays = []

        def transport(request, timeout):
            calls.append((request, timeout))
            if len(calls) == 1:
                raise urllib.error.HTTPError(
                    request.full_url, 429, "busy", {"Retry-After": "3"}, io.BytesIO()
                )
            return FakeResponse(response_for(score))

        payload, _settings = mimo.build_request(b"audio", audio_format="mp3", audio_duration=10)
        with mock.patch.dict(os.environ, {"OPENROUTER_API_KEY": "test-only-secret"}, clear=False):
            result = mimo.submit_request(payload, transport=transport, sleeper=delays.append)
        self.assertEqual(result["provider"], "MockProvider")
        self.assertEqual(len(calls), 2)
        self.assertEqual(delays, [3.0])

    def test_non_transient_error_is_not_retried(self):
        calls = []

        def transport(request, timeout):
            calls.append(timeout)
            raise urllib.error.HTTPError(request.full_url, 400, "bad", {}, io.BytesIO())

        payload, _settings = mimo.build_request(b"audio", audio_format="mp3", audio_duration=10)
        with mock.patch.dict(os.environ, {"OPENROUTER_API_KEY": "test-only-secret"}, clear=False):
            with self.assertRaisesRegex(RuntimeError, "HTTP 400"):
                mimo.submit_request(payload, transport=transport, sleeper=lambda _delay: None)
        self.assertEqual(calls, [600.0])

    def test_dry_run_redacts_audio_and_needs_no_key(self):
        payload, settings = mimo.build_request(b"private audio", audio_format="mp3", audio_duration=10)
        with mock.patch.dict(os.environ, {}, clear=True):
            dump = mimo.redacted_request_dump(payload, settings, "a" * 64)
        rendered = json.dumps(dump)
        self.assertNotIn(base64_text(b"private audio"), rendered)
        self.assertNotIn("private audio", rendered)
        self.assertEqual(dump["headers"]["Authorization"], "Bearer <redacted>")

    def test_whisper_import_clamps_timing_and_marks_corrected_lines(self):
        raw = {
            "segments": [{
                "start": -1,
                "end": 20,
                "text": " guessed line ",
                "words": [
                    {"start": 1, "end": 2, "word": " hello", "probability": 1.4},
                    {"start": 12, "end": 13, "word": "outside"},
                ],
            }]
        }
        corrected = [{"startMs": 500, "endMs": 4000, "text": "Authoritative lyric"}]
        result = import_whisper.normalize_whisper(
            raw,
            audio_sha256="b" * 64,
            audio_duration=10,
            model="medium.en",
            corrected_lines=corrected,
        )
        self.assertEqual(result["words"][0]["text"], "hello")
        self.assertEqual(result["words"][0]["confidence"], 1)
        self.assertEqual(len(result["words"]), 1)
        self.assertEqual(result["lines"][0]["text"], "Authoritative lyric")
        self.assertEqual(result["lines"][0]["start_seconds"], 0.5)
        self.assertTrue(result["lines"][0]["corrected"])

    def test_remotion_whisper_cpp_offsets_and_tokens_are_imported(self):
        raw = {
            "transcription": [{
                "offsets": {"from": 1000, "to": 4000},
                "text": " Hello world!",
                "tokens": [
                    {"text": "[_BEG_]", "offsets": {"from": 1000, "to": 1000}, "p": 0.99},
                    {"text": " Hello", "offsets": {"from": 1000, "to": 1800}, "p": 0.8},
                    {"text": " world", "offsets": {"from": 1800, "to": 3000}, "p": 0.9},
                    {"text": "!", "offsets": {"from": 3000, "to": 3200}, "p": 1.0},
                    {"text": "[_TT_200]", "offsets": {"from": 4000, "to": 4000}, "p": 0.9},
                ],
            }]
        }
        result = import_whisper.normalize_whisper(
            raw,
            audio_sha256="c" * 64,
            audio_duration=10,
            model="medium.en",
        )
        self.assertEqual(result["lines"][0]["text"], "Hello world!")
        self.assertEqual(result["lines"][0]["start_seconds"], 1)
        self.assertEqual(result["words"][0]["text"], "Hello")
        self.assertEqual(result["words"][0]["start_seconds"], 1)
        self.assertEqual(result["words"][1]["text"], "world!")
        self.assertAlmostEqual(result["words"][1]["confidence"], 0.95)
        self.assertEqual(len(result["words"]), 2)

    def test_remotion_caption_array_is_imported_as_timed_word_evidence(self):
        captions = [
            {"text": " First", "startMs": 250, "endMs": 750, "confidence": 0.8},
            {"text": " second", "startMs": 750, "endMs": 1250, "confidence": None},
        ]
        result = import_whisper.normalize_whisper(
            captions,
            audio_sha256="d" * 64,
            audio_duration=2,
        )
        self.assertEqual([word["text"] for word in result["words"]], ["First", "second"])
        self.assertEqual(result["words"][0]["start_seconds"], 0.25)
        self.assertEqual(result["words"][1]["end_seconds"], 1.25)

    def test_semantic_segments_must_cover_full_duration_without_gaps(self):
        gap = {
            "global": creative(),
            "segments": [
                creative(start_seconds=0, end_seconds=4),
                creative(start_seconds=5, end_seconds=10),
            ],
        }
        with self.assertRaisesRegex(analysis_io.AnalysisValidationError, "gap"):
            mimo.normalize_semantic_score(
                gap,
                audio_sha256="e" * 64,
                audio_duration=10,
                request_settings={},
            )

        incomplete = {
            "global": creative(),
            "segments": [creative(start_seconds=0, end_seconds=9)],
        }
        with self.assertRaisesRegex(analysis_io.AnalysisValidationError, "final"):
            mimo.normalize_semantic_score(
                incomplete,
                audio_sha256="e" * 64,
                audio_duration=10,
                request_settings={},
            )


def base64_text(value: bytes) -> str:
    import base64
    return base64.b64encode(value).decode("ascii")


if __name__ == "__main__":
    unittest.main()
