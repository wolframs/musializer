from __future__ import annotations

import base64
import contextlib
import inspect
import io
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import analysis_io
import analyze_audio as measured_adapter
import external_analysis as external


SHA = "a" * 64


def lyrics_document() -> dict:
    return {
        "schema_version": "musializer.lyric-timing/v1",
        "lane": "lyrics",
        "audio": {"sha256": SHA, "duration_seconds": 12.0},
        "provenance": {"adapter": "test"},
        "words": [],
        "lines": [
            {"start_seconds": 1.0, "end_seconds": 2.0, "text": "hello wrld", "confidence": 0.7, "corrected": False},
            {"start_seconds": 4.0, "end_seconds": 5.0, "text": "again", "confidence": 0.8, "corrected": False},
        ],
    }


def measured_document() -> dict:
    frames = []
    for second in range(12):
        frames.append({
            "time_seconds": float(second), "rms": 0.2 + second / 30,
            "spectral_flux": 0.1 if second < 6 else 0.7,
            "onset_strength": 0.05 if second < 6 else 0.8,
        })
    settings = {
        "sample_rate": external.MEASURED_SAMPLE_RATE,
        "channels": external.MEASURED_CHANNELS,
        "window_size": external.MEASURED_WINDOW,
        "hop_size": external.MEASURED_HOP,
    }
    analysis = {
        "analyzer_version": external.MEASURED_ANALYZER_VERSION,
        **settings,
        "window_function": "hann",
        "band_edges_hz": {},
    }
    return {
        "schema_version": "musializer.measured-analysis/v1", "lane": "measured_audio",
        "audio": {"sha256": SHA, "duration_seconds": 12.0},
        "analysis": analysis,
        "provenance": {
            "adapter": "tools/analyze_audio.py",
            "adapter_version": external.MEASURED_ANALYZER_VERSION,
            "source_kind": "offline_measured_analysis",
            "request_settings": settings,
        },
        "frames": frames,
        "summary": {"sections": [
            {"start_seconds": 0.0, "end_seconds": 6.0},
            {"start_seconds": 6.0, "end_seconds": 12.0},
        ]},
    }


def semantic_document() -> dict:
    creative = {
        "summary": "stars opening into a kinetic tunnel", "moods": ["cosmic"],
        "motion": ["drifting"], "imagery": ["stars"], "energy": 0.6,
        "tension": 0.4, "valence": 0.2, "confidence": 0.8,
    }
    return {
        "schema_version": "musializer.semantic-score/v1", "lane": "semantic_interpretation",
        "audio": {"sha256": SHA, "duration_seconds": 12.0},
        "segments": [
            {**creative, "start_seconds": 0.0, "end_seconds": 6.0},
            {**creative, "moods": ["mechanical"], "energy": 0.9,
             "start_seconds": 6.0, "end_seconds": 12.0},
        ],
    }


class ExternalAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def write_json(self, name: str, value: object) -> Path:
        path = self.root / name
        analysis_io.atomic_write_json(path, value)
        return path

    def test_external_timeout_does_not_include_private_child_output(self):
        def runner(*_args, **_kwargs):
            raise subprocess.TimeoutExpired("private-audio-name", 2, output="secret lyric")

        with self.assertRaisesRegex(RuntimeError, "whisper-cli exceeded") as raised:
            external._run(["whisper-cli", "private.wav"], timeout=2, runner=runner)
        self.assertNotIn("secret lyric", str(raised.exception))
        self.assertNotIn("private.wav", str(raised.exception))

    def test_failed_child_output_lands_only_in_the_named_diagnostic_sink(self):
        def runner(argv, **_kwargs):
            return subprocess.CompletedProcess(
                argv, 1, "partial stdout", "ERROR: invalid_json_schema details")

        sink = self.root / "lyrics.review.diagnostic.log"
        with self.assertRaisesRegex(RuntimeError, "codex exited with code 1") as raised:
            external._run(["codex", "exec", "-"], timeout=5, runner=runner,
                          diagnostic_sink=sink)
        # The summary error stays clean of child output but points at the sink.
        self.assertNotIn("invalid_json_schema", str(raised.exception))
        self.assertIn(sink.name, str(raised.exception))
        diagnostic = sink.read_text(encoding="utf-8")
        self.assertIn("codex: exited with code 1", diagnostic)
        self.assertIn("ERROR: invalid_json_schema details", diagnostic)
        self.assertIn("partial stdout", diagnostic)

    def test_failed_child_without_sink_keeps_historical_opaque_error(self):
        def runner(argv, **_kwargs):
            return subprocess.CompletedProcess(argv, 1, "private", "private too")

        with self.assertRaisesRegex(RuntimeError, "codex exited with code 1") as raised:
            external._run(["codex", "exec", "-"], timeout=5, runner=runner)
        self.assertNotIn("private", str(raised.exception))

    def test_diagnostic_sink_bounds_oversized_child_output(self):
        def runner(argv, **_kwargs):
            return subprocess.CompletedProcess(argv, 1, "", "x"*(external.DIAGNOSTIC_TAIL_LIMIT*3))

        sink = self.root / "bounded.diagnostic.log"
        with self.assertRaises(RuntimeError):
            external._run(["codex"], timeout=5, runner=runner, diagnostic_sink=sink)
        self.assertLess(sink.stat().st_size,
                        external.DIAGNOSTIC_TAIL_LIMIT + 512)

    def test_real_child_output_is_drained_into_bounded_memory_tails(self):
        sink = self.root / "real-child.diagnostic.log"
        script = (
            "import sys; "
            f"sys.stdout.write('o'*{external.DIAGNOSTIC_TAIL_LIMIT*4}); "
            f"sys.stderr.write('e'*{external.DIAGNOSTIC_TAIL_LIMIT*4}); "
            "raise SystemExit(7)"
        )
        with self.assertRaisesRegex(RuntimeError, "exited with code 7"):
            external._run([sys.executable, "-c", script], timeout=5,
                          diagnostic_sink=sink)
        diagnostic = sink.read_text(encoding="utf-8")
        self.assertIn("exited with code 7", diagnostic)
        self.assertLess(len(diagnostic), external.DIAGNOSTIC_TAIL_LIMIT*2 + 512)

    @unittest.skipUnless(os.name == "posix", "POSIX process-group behavior")
    def test_successful_child_cannot_leave_a_grandchild_running(self):
        script = (
            "import subprocess,sys; "
            "child=subprocess.Popen([sys.executable,'-c',"
            "'import time; time.sleep(30)']); "
            "print(child.pid, flush=True)"
        )
        result = external._run([sys.executable, "-c", script], timeout=5)
        grandchild = int(result.stdout.strip())
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            try:
                os.kill(grandchild, 0)
            except ProcessLookupError:
                break
            time.sleep(0.01)
        else:
            self.fail("successful child left its grandchild running")

    def test_successful_assist_writes_privacy_safe_counts_to_job_log(self):
        audio = self.root / "track.wav"
        audio.write_bytes(b"fixture")
        output = self.root / "analysis"
        result = {"result_counts": {
            "lyrics": 0, "sections": 21, "semantics": 0,
        }}
        stderr = io.StringIO()
        with mock.patch.object(external, "run_assist", return_value=result):
            with contextlib.redirect_stderr(stderr):
                status = external.main([
                    "assist", str(audio), str(output), "--duration", "12",
                    "--mode", "lyrics",
                ])
        self.assertEqual(status, 0)
        self.assertIn("0 lyric cues, 21 scene sections", stderr.getvalue())
        self.assertNotIn(str(audio), stderr.getvalue())

    def test_codex_output_schema_stays_structured_output_compatible(self):
        # codex exec forwards the schema to a structured-output endpoint that
        # rejects several JSON Schema keywords outright; uniqueItems silently
        # broke every Timed lyrics run until 2026-07-16. Enforce the subset
        # here and rely on _validate_codex_review for uniqueness.
        schema_path = ROOT / "schemas/codex-lyric-review-output-v1.schema.json"
        schema_text = schema_path.read_text(encoding="utf-8")
        for keyword in ("patternProperties", "contains", '"if"', '"then"',
                        '"oneOf"', '"not"'):
            self.assertNotIn(keyword, schema_text)
        self.assertNotIn('"uniqueItems":', schema_text)
        validator = inspect.getsource(external._validate_codex_review)
        self.assertIn("len(set(indices)) != len(indices)", validator)

    def test_codex_review_success_removes_stale_diagnostics(self):
        source_path = self.write_json("lyrics.json", lyrics_document())
        output = self.root / "review.json"
        stale = self.root / "review.diagnostic.log"
        stale.write_text("codex: exited with code 1\n", encoding="utf-8")

        def runner(argv, **kwargs):
            result_path = Path(argv[argv.index("-o") + 1])
            analysis_io.atomic_write_json(result_path, {"lines": [], "notes": []})
            return subprocess.CompletedProcess(argv, 0, "", "")

        external.run_codex_review(source_path, output, runner=runner)
        self.assertFalse(stale.exists())

    def test_local_child_environment_strips_credentials(self):
        with mock.patch.dict(os.environ, {
            "OPENROUTER_API_KEY": "secret", "SOME_TOKEN": "secret2",
            "PATH": "/usr/bin", "CUDA_VISIBLE_DEVICES": "0",
        }, clear=True):
            environment = external._safe_local_env()
        self.assertNotIn("OPENROUTER_API_KEY", environment)
        self.assertNotIn("SOME_TOKEN", environment)
        self.assertEqual(environment["PATH"], "/usr/bin")
        self.assertEqual(environment["CUDA_VISIBLE_DEVICES"], "0")

    def test_openrouter_env_reads_only_named_key_without_sourcing_dotenv(self):
        dotenv = self.root / ".env"
        dotenv.write_text(
            "OPENROUTER_API_KEY='local-key'\n"
            "OTHER_SECRET=do-not-copy\n"
            "MALICIOUS=$(touch should-not-run)\n",
            encoding="utf-8",
        )
        with mock.patch.dict(os.environ, {"PATH": "/usr/bin"}, clear=True):
            environment = external._openrouter_env(dotenv)
        self.assertEqual(environment["OPENROUTER_API_KEY"], "local-key")
        self.assertEqual(environment["PATH"], "/usr/bin")
        self.assertNotIn("OTHER_SECRET", environment)
        self.assertNotIn("MALICIOUS", environment)
        self.assertFalse((self.root / "should-not-run").exists())

    @unittest.skipUnless(os.name == "posix", "POSIX worker isolation")
    def test_ui_worker_enters_a_new_process_group(self):
        with mock.patch("os.setsid") as setsid:
            external._enter_process_group()
        setsid.assert_called_once_with()

    def test_whisper_dry_run_redacts_audio_and_temporary_files(self):
        audio = self.root / "private song.mp3"
        executable = self.root / "whisper-cli"
        model = self.root / "ggml-medium.en.bin"
        audio.write_bytes(b"audio"); executable.write_bytes(b"binary"); model.write_bytes(b"model")
        request = external.run_whisper(
            audio, self.root / "lyrics.json", audio_duration=12,
            whisper_bin=executable, model=model, dry_run=True,
        )
        rendered = json.dumps(request)
        self.assertNotIn(str(audio), rendered)
        self.assertNotIn("musializer-whisper-", rendered)
        self.assertEqual(request["audio_sha256"], analysis_io.sha256_file(audio))

    def test_whisper_executes_argv_without_shell_and_normalizes_output(self):
        audio = self.root / "track.mp3"; audio.write_bytes(b"audio")
        executable = self.root / "whisper-cli"; executable.write_bytes(b"binary")
        model = self.root / "ggml-medium.en.bin"; model.write_bytes(b"model")
        calls = []

        def runner(argv, **kwargs):
            calls.append((argv, kwargs))
            self.assertIsInstance(argv, list)
            if "--output-file" in argv:
                prefix = Path(argv[argv.index("--output-file") + 1])
                Path(f"{prefix}.json").write_text(json.dumps({"transcription": [{
                    "offsets": {"from": 1000, "to": 2000}, "text": " hello",
                    "tokens": [{"offsets": {"from": 1000, "to": 2000}, "text": " hello"}],
                }]}), encoding="utf-8")
            return subprocess.CompletedProcess(argv, 0, "", "")

        output = self.root / "lyrics.json"
        result = external.run_whisper(
            audio, output, audio_duration=12, whisper_bin=executable,
            model=model, runner=runner,
        )
        self.assertEqual(len(calls), 2)
        self.assertTrue(output.is_file())
        self.assertEqual(result["lines"][0]["text"], "hello")
        self.assertEqual(result["provenance"]["source_kind"], "whisper_import")
        whisper_argv = calls[1][0]
        self.assertIn("-t", whisper_argv)
        self.assertGreaterEqual(int(whisper_argv[whisper_argv.index("-t") + 1]), 1)
        self.assertEqual(whisper_argv[whisper_argv.index("--dtw") + 1], "medium.en")

    def test_dtw_names_map_to_whisper_cpp_presets(self):
        cases = {
            "ggml-medium.en.bin": "medium.en",
            "ggml-large-v3.bin": "large.v3",
            "ggml-large-v3-turbo.bin": "large.v3.turbo",
            "ggml-large-v3-q5_0.bin": "large.v3",
            "ggml-large-v3-turbo-q5_0.bin": "large.v3.turbo",
            "ggml-large-v2.bin": "large.v2",
            "ggml-tiny.bin": "tiny",
            # Unknown or foreign names must run without DTW rather than
            # passing a preset whisper-cli rejects outright.
            "ggml-large.bin": None,
            "ggml-distil-large-v3.bin": None,
            "custom-model.bin": None,
            "ggml-medium.en.gguf": None,
        }
        for name, expected in cases.items():
            self.assertEqual(external._dtw_model_name(name), expected, name)

    def test_default_whisper_model_prefers_the_most_accurate_available(self):
        install = self.root / "whisper-install"
        (install / "build/bin").mkdir(parents=True)
        (install / "build/bin/whisper-cli").write_bytes(b"binary")
        with mock.patch.object(external, "_WHISPER_INSTALL", install):
            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(external._default_whisper_paths()[1], None)
                (install / "ggml-medium.en.bin").write_bytes(b"m")
                self.assertEqual(external._default_whisper_paths()[1],
                                 install / "ggml-medium.en.bin")
                (install / "ggml-large-v3.bin").write_bytes(b"l")
                self.assertEqual(external._default_whisper_paths()[1],
                                 install / "ggml-large-v3.bin")
                (install / "ggml-large-v3-turbo.bin").write_bytes(b"t")
                self.assertEqual(external._default_whisper_paths()[1],
                                 install / "ggml-large-v3-turbo.bin")
            with mock.patch.dict(
                    os.environ,
                    {"MUSIALIZER_WHISPER_MODEL": "/override/model.bin"},
                    clear=True):
                self.assertEqual(external._default_whisper_paths()[1],
                                 Path("/override/model.bin"))

    def test_codex_review_is_stdin_only_and_evidence_bounded(self):
        source_path = self.write_json("lyrics.json", lyrics_document())
        observed = {}

        def runner(argv, **kwargs):
            observed["argv"] = argv; observed["stdin"] = kwargs["input"]
            result_path = Path(argv[argv.index("-o") + 1])
            result_path.write_text(json.dumps({
                "lines": [{"start_seconds": 1.0, "end_seconds": 2.0,
                           "text": "Hello, world.", "source_line_indices": [0],
                           "confidence": 0.7, "uncertain": True}],
                "notes": ["Second line omitted as uncertain."],
            }), encoding="utf-8")
            return subprocess.CompletedProcess(argv, 0, "", "")

        output = self.root / "review.json"
        result = external.run_codex_review(source_path, output, runner=runner)
        self.assertNotIn("hello wrld", " ".join(observed["argv"]))
        self.assertIn("hello wrld", observed["stdin"])
        self.assertEqual(result["lane"], "lyric_review")
        self.assertEqual(result["lines"][0]["source_line_indices"], [0])

    def test_codex_review_may_split_source_lines_but_stays_chronological(self):
        source = lyrics_document()
        line = {"start_seconds": 1.0, "end_seconds": 1.5, "text": "hello",
                "source_line_indices": [0], "confidence": 0.7,
                "uncertain": False}
        second = {**line, "start_seconds": 1.5, "end_seconds": 2.0,
                  "text": "world"}
        cleaned, _ = external._validate_codex_review(
            {"lines": [line, second], "notes": []}, source)
        self.assertEqual(len(cleaned), 2)
        shuffled = {**line, "source_line_indices": [1],
                    "start_seconds": 4.0, "end_seconds": 5.0}
        with self.assertRaisesRegex(analysis_io.AnalysisValidationError,
                                    "chronological"):
            external._validate_codex_review(
                {"lines": [shuffled, second], "notes": []}, source)

    def test_codex_review_rejects_unreadable_display_lines(self):
        source = lyrics_document()
        overlong = {"start_seconds": 1.0, "end_seconds": 2.0,
                    "text": "x" * (external.REVIEW_TEXT_LIMIT + 1),
                    "source_line_indices": [0], "confidence": 0.7,
                    "uncertain": False}
        with self.assertRaises(analysis_io.AnalysisValidationError):
            external._validate_codex_review(
                {"lines": [overlong], "notes": []}, source)
        source["lines"][0]["end_seconds"] = 40.0
        source["audio"]["duration_seconds"] = 60.0
        endless = {"start_seconds": 1.0, "end_seconds": 39.0, "text": "hum",
                   "source_line_indices": [0], "confidence": 0.7,
                   "uncertain": False}
        with self.assertRaises(analysis_io.AnalysisValidationError):
            external._validate_codex_review(
                {"lines": [endless], "notes": []}, source)

    def test_codex_review_reports_coverage_and_flags_hallucination_loops(self):
        source = lyrics_document()
        source["audio"]["duration_seconds"] = 60.0
        source["lines"] = [
            {"start_seconds": 1.0, "end_seconds": 2.0, "text": "hello wrld",
             "confidence": 0.7, "corrected": False},
            {"start_seconds": 4.0, "end_seconds": 5.0, "text": "again",
             "confidence": 0.8, "corrected": False},
        ] + [
            {"start_seconds": 10.0 + 2.0 * i, "end_seconds": 11.8 + 2.0 * i,
             "text": "loop loop loop", "confidence": 0.4, "corrected": False}
            for i in range(8)
        ]
        source_path = self.write_json("lyrics.json", source)
        observed = {}

        def runner(argv, **kwargs):
            observed["stdin"] = kwargs["input"]
            result_path = Path(argv[argv.index("-o") + 1])
            result_path.write_text(json.dumps({
                "lines": [{"start_seconds": 1.0, "end_seconds": 2.0,
                           "text": "Hello, world.", "source_line_indices": [0],
                           "confidence": 0.7, "uncertain": False}],
                "notes": ["Loop omitted."],
            }), encoding="utf-8")
            return subprocess.CompletedProcess(argv, 0, "", "")

        result = external.run_codex_review(
            source_path, self.root / "review.json", runner=runner)
        self.assertIn("suspected_hallucination_intervals", observed["stdin"])
        coverage = result["coverage"]
        self.assertEqual(coverage["source_lines"], 10)
        self.assertEqual(coverage["cited_source_lines"], 1)
        # The eight-line loop is flagged, so only "again" counts as an
        # uncited reliable source line.
        self.assertEqual(coverage["uncited_reliable_source_lines"], 1)
        self.assertEqual(len(coverage["suspected_hallucination_intervals"]), 1)

    def test_codex_review_rejects_uncited_or_out_of_envelope_lines(self):
        source = lyrics_document()
        with self.assertRaises(analysis_io.AnalysisValidationError):
            external._validate_codex_review({
                "lines": [{"start_seconds": 0, "end_seconds": 10, "text": "invented",
                           "source_line_indices": [0], "confidence": 1, "uncertain": False}],
                "notes": [],
            }, source)

    def test_scene_plan_combines_distinct_evidence_lanes_deterministically(self):
        measured = self.write_json("measured.json", measured_document())
        lyrics = self.write_json("lyrics.json", lyrics_document())
        semantic = self.write_json("semantic.json", semantic_document())
        first = external.build_scene_plan(measured, lyrics_path=lyrics, semantic_path=semantic)
        second = external.build_scene_plan(measured, lyrics_path=lyrics, semantic_path=semantic)
        self.assertEqual(first, second)
        self.assertEqual(first["sections"][0]["start_seconds"], 0)
        self.assertEqual(first["sections"][-1]["end_seconds"], 12)
        self.assertEqual(first["sections"][0]["recommended_scene"], "constellation")
        lanes = {reason["source_lane"] for section in first["sections"] for reason in section["reasons"]}
        self.assertIn("measured_audio", lanes)
        self.assertIn("semantic_interpretation", lanes)

    def test_existing_mimo_export_becomes_subjective_notes_without_embedded_audio(self):
        audio = self.root / "track.mp3"; audio.write_bytes(b"audio snapshot")
        export = self.write_json("openrouter.json", {
            "items": {
                "input": {"data": {"content": [{"type": "input_audio", "input_audio": {"data": "PRIVATE_BASE64"}}]}},
                "reasoning": {"data": {"content": [{"type": "reasoning_text", "text": "private chain"}]}},
                "answer": {"data": {"content": [{"type": "output_text", "text": "Cosmic stars drift into a mechanical tunnel."}]}},
            }
        })
        notes = external.import_mimo_export(export, audio, 12)
        rendered = json.dumps(notes)
        self.assertEqual(notes["lane"], "semantic_interpretation_notes")
        self.assertNotIn("PRIVATE_BASE64", rendered)
        self.assertNotIn("private chain", rendered)
        self.assertIn("Cosmic stars", notes["text"])

        measured_value = measured_document()
        measured_value["audio"]["sha256"] = notes["audio"]["sha256"]
        measured = self.write_json("measured.json", measured_value)
        notes_path = self.write_json("notes.json", notes)
        plan = external.build_scene_plan(measured, semantic_path=notes_path)
        self.assertEqual(plan["sections"][0]["recommended_scene"], "constellation")
        bridge = external.build_bridge(plan, semantic=notes)
        rows = external.parse_bridge(bridge)
        self.assertTrue(any(row[0] == "SEMANTIC_NOTE" for row in rows))

    def test_bridge_is_ascii_bounded_and_round_trips_private_text(self):
        measured = self.write_json("measured.json", measured_document())
        semantic = semantic_document(); lyrics = lyrics_document()
        plan = external.build_scene_plan(measured)
        bridge = external.build_bridge(plan, lyrics=lyrics, semantic=semantic)
        rows = external.parse_bridge(bridge)
        self.assertTrue(bridge.isascii())
        lyric = next(row for row in rows if row[0] == "LYRIC")
        self.assertEqual(base64.b64decode(lyric[-1]).decode(), "hello wrld")
        self.assertNotIn("hello wrld", bridge)
        self.assertTrue(any(row[0] == "SECTION" for row in rows))
        self.assertTrue(any(row[0] == "SEMANTIC" for row in rows))

    def test_assist_sections_reuses_cache_and_always_emits_bridge(self):
        audio = self.root / "track.wav"; audio.write_bytes(b"fixture audio")
        output_dir = self.root / "analysis"; output_dir.mkdir()
        measured = measured_document()
        measured["audio"]["sha256"] = analysis_io.sha256_file(audio)
        self.write_json("analysis/measured.json", measured)

        def forbidden_runner(*_args, **_kwargs):
            self.fail("sections mode should reuse the measured cache")

        result = external.run_assist(
            audio, output_dir, audio_duration=12, mode="sections",
            runner=forbidden_runner,
        )
        self.assertEqual(result["cache_status"]["measured"], "reused")
        self.assertEqual(result["result_counts"]["lyrics"], 0)
        self.assertGreater(result["result_counts"]["sections"], 0)
        self.assertEqual(result["result_counts"]["semantics"], 0)
        self.assertTrue((output_dir / "analysis.bridge.tsv").is_file())
        self.assertTrue((output_dir / "scene-plan.json").is_file())
        self.assertTrue((output_dir / "assist-manifest.json").is_file())

    def test_assist_sections_never_consumes_a_cached_mimo_lane(self):
        audio = self.root / "track.wav"; audio.write_bytes(b"fixture audio")
        audio_sha = analysis_io.sha256_file(audio)
        output_dir = self.root / "analysis"; output_dir.mkdir()
        measured = measured_document(); measured["audio"]["sha256"] = audio_sha
        semantic = semantic_document(); semantic["audio"]["sha256"] = audio_sha
        self.write_json("analysis/measured.json", measured)
        self.write_json("analysis/semantic.cache.json", semantic)

        def forbidden_runner(*_args, **_kwargs):
            self.fail("sections mode should use only its measured cache")

        external.run_assist(
            audio, output_dir, audio_duration=12, mode="sections",
            runner=forbidden_runner,
        )
        plan = analysis_io.read_json(output_dir / "scene-plan.json")
        self.assertEqual(plan["sections"][0]["recommended_scene"], "spectrum")
        self.assertEqual([source["lane"] for source in plan["sources"]],
                         ["measured_audio"])
        reasons = {
            reason["source_lane"]
            for section in plan["sections"] for reason in section["reasons"]
        }
        self.assertNotIn("semantic_interpretation", reasons)
        bridge_rows = external.parse_bridge(
            (output_dir / "analysis.bridge.tsv").read_text(encoding="ascii")
        )
        self.assertFalse(any(row[0].startswith("SEMANTIC") for row in bridge_rows))

    def test_cache_fingerprints_reject_only_relevant_identity_mutations(self):
        self.assertEqual(external.MEASURED_ANALYZER_VERSION,
                         measured_adapter.ANALYZER_VERSION)
        self.assertEqual(external.MEASURED_SAMPLE_RATE,
                         measured_adapter.DEFAULT_SAMPLE_RATE)
        self.assertEqual(external.MEASURED_CHANNELS,
                         measured_adapter.DEFAULT_CHANNELS)
        self.assertEqual(external.MEASURED_WINDOW,
                         measured_adapter.DEFAULT_WINDOW)
        self.assertEqual(external.MEASURED_HOP,
                         measured_adapter.DEFAULT_HOP)
        measured = measured_document()
        self.assertTrue(external._measured_cache_accepts(measured))
        changed_measured = json.loads(json.dumps(measured))
        changed_measured["provenance"]["adapter_version"] = "older"
        self.assertFalse(external._measured_cache_accepts(changed_measured))

        model = self.root / "ggml-medium.en.bin"; model.write_bytes(b"model-a")
        whisper = lyrics_document()
        whisper["provenance"] = {
            "adapter": "tools/external_analysis.py",
            "adapter_version": analysis_io.ADAPTER_VERSION,
            "source_kind": "whisper_import",
            "model": model.name,
            "request_settings": {
                "language": "en", "dtw_model": "medium.en",
                "model_sha256": analysis_io.sha256_file(model),
                "gpu_requested": True,
            },
        }
        self.assertTrue(external._whisper_cache_accepts(
            whisper, measured_duration=12.0, model=model,
        ))
        model.write_bytes(b"model-b")
        self.assertFalse(external._whisper_cache_accepts(
            whisper, measured_duration=12.0, model=model,
        ))

        source_path = self.write_json("lyrics.json", lyrics_document())
        source_sha = analysis_io.sha256_file(source_path)
        review = {
            "source": {"sha256": source_sha},
            "provenance": {
                "adapter": "tools/external_analysis.py",
                "adapter_version": analysis_io.ADAPTER_VERSION,
                "source_kind": "codex_lyric_review",
                "model": "codex-default",
                "prompt_version": external.LYRIC_PROMPT_VERSION,
                "prompt_sha256": analysis_io.sha256_file(external.LYRIC_PROMPT),
                "request_settings": {"sandbox": "read-only", "ephemeral": True},
            },
        }
        self.assertTrue(external._review_cache_accepts(
            review, source_sha256=source_sha, model=None,
        ))
        review["provenance"]["prompt_sha256"] = "0" * 64
        self.assertFalse(external._review_cache_accepts(
            review, source_sha256=source_sha, model=None,
        ))

        audio = self.root / "track.mp3"; audio.write_bytes(b"audio")
        audio_sha = analysis_io.sha256_file(audio)
        request = external._mimo_request_identity(
            audio, audio_sha=audio_sha, measured_duration=12.0, zdr=True,
        )
        semantic = semantic_document(); semantic["audio"]["sha256"] = audio_sha
        semantic["provenance"] = {
            "adapter": "tools/mimo_openrouter.py",
            "adapter_version": analysis_io.ADAPTER_VERSION,
            "source_kind": "mimo_openrouter",
            "model": external.mimo_adapter.MODEL,
            "prompt_version": external.mimo_adapter.PROMPT_VERSION,
            "request_settings": {
                key: value for key, value in request.items()
                if key != "audio_sha256"
            },
        }
        envelope = {
            "request": request,
            "cache_key": analysis_io.canonical_sha256(request),
            "normalized": semantic,
        }
        self.assertTrue(external._mimo_cache_accepts(
            envelope, request_identity=request,
        ))
        changed_request = {**request, "zero_data_retention": False}
        self.assertFalse(external._mimo_cache_accepts(
            envelope, request_identity=changed_request,
        ))

    def test_reference_discovery_prefers_override_then_sibling_then_tags(self):
        audio = self.root / "track.mp3"; audio.write_bytes(b"audio")
        probe_payload = json.dumps(
            {"format": {"tags": {"lyrics-eng": "Embedded lines"}}})

        def prober(argv, **_kwargs):
            self.assertEqual(Path(argv[0]).name, "ffprobe")
            self.assertNotIn("Embedded lines", " ".join(argv))
            return subprocess.CompletedProcess(argv, 0, probe_payload, "")

        found = external.discover_reference_lyrics(audio, runner=prober)
        self.assertEqual(found["source"], "embedded:lyrics-eng")
        self.assertEqual(found["text"], "Embedded lines")

        sibling = self.root / "track.lyrics.txt"
        sibling.write_text("Sibling lines", encoding="utf-8")
        found = external.discover_reference_lyrics(audio, runner=prober)
        self.assertEqual(found["source"], "file:track.lyrics.txt")

        override = self.root / "pasted.txt"
        override.write_text("Pasted lines", encoding="utf-8")
        found = external.discover_reference_lyrics(
            audio, override=override, runner=prober)
        self.assertEqual(found["source"], "file:pasted.txt")

        def silent_prober(argv, **_kwargs):
            return subprocess.CompletedProcess(argv, 0, "{}", "")

        sibling.unlink()
        self.assertIsNone(
            external.discover_reference_lyrics(audio, runner=silent_prober))
        with self.assertRaises(analysis_io.AnalysisValidationError):
            empty = self.root / "empty.txt"
            empty.write_text("  \n", encoding="utf-8")
            external.discover_reference_lyrics(
                audio, override=empty, runner=prober)

    def test_discovered_reference_switches_assist_to_deterministic_sync(self):
        audio = self.root / "track.wav"; audio.write_bytes(b"fixture audio")
        audio_sha = analysis_io.sha256_file(audio)
        output_dir = self.root / "analysis"; output_dir.mkdir()
        measured = measured_document(); measured["audio"]["sha256"] = audio_sha
        self.write_json("analysis/measured.json", measured)
        model = self.root / "ggml-medium.en.bin"; model.write_bytes(b"model")
        whisper_bin = self.root / "whisper-cli"; whisper_bin.write_bytes(b"bin")
        whisper = lyrics_document(); whisper["audio"]["sha256"] = audio_sha
        whisper["provenance"] = {
            "adapter": "tools/external_analysis.py",
            "adapter_version": analysis_io.ADAPTER_VERSION,
            "source_kind": "whisper_import",
            "model": model.name,
            "request_settings": {
                "language": "en", "dtw_model": "medium.en",
                "model_sha256": analysis_io.sha256_file(model),
                "gpu_requested": True,
            },
        }
        self.write_json("analysis/lyrics.whisper.json", whisper)
        probe_payload = json.dumps(
            {"format": {"tags": {"lyrics-eng": "Hello world\nAgain"}}})

        def runner(argv, **_kwargs):
            name = Path(argv[0]).name
            self.assertNotEqual(name, "codex",
                                "sync mode must not invoke the Codex review")
            self.assertEqual(name, "ffprobe")
            return subprocess.CompletedProcess(argv, 0, probe_payload, "")

        result = external.run_assist(
            audio, output_dir, audio_duration=12, mode="lyrics",
            whisper_bin=whisper_bin, whisper_model=model, runner=runner,
        )
        self.assertEqual(result["cache_status"], {
            "measured": "reused", "lyrics": "reused", "sync": "generated",
        })
        self.assertEqual(result["lyric_source"], "embedded:lyrics-eng")
        self.assertIn("lyric_sync", result["provenance_streams"])
        self.assertNotIn("lyric_review", result["provenance_streams"])
        synced = analysis_io.read_json(output_dir / "lyrics.sync.json")
        self.assertEqual(synced["lane"], "lyric_sync")
        texts = [line["text"] for line in synced["lines"]]
        self.assertEqual(texts, ["Hello world", "Again"])
        self.assertEqual(result["result_counts"]["lyrics"], 2)
        bridge = (output_dir / "analysis.bridge.tsv").read_text(encoding="utf-8")
        encoded = base64.b64encode("Hello world".encode()).decode()
        self.assertIn(encoded, bridge)

        # A second run must reuse the sync stage byte-for-byte.
        again = external.run_assist(
            audio, output_dir, audio_duration=12, mode="lyrics",
            whisper_bin=whisper_bin, whisper_model=model, runner=runner,
        )
        self.assertEqual(again["cache_status"]["sync"], "reused")

    def test_sync_cache_identity_covers_reference_evidence_and_aligner(self):
        whisper_path = self.write_json("lyrics.whisper.json", lyrics_document())
        whisper_sha = analysis_io.sha256_file(whisper_path)
        reference = external._reference_from_text("Hello world", "embedded:lyrics")
        document = {
            "schema_version": external.LYRIC_SYNC_VERSION,
            "lane": "lyric_sync",
            "aligner_version": "1",
            "audio": {"sha256": SHA, "duration_seconds": 12.0},
            "reference": {"source": "embedded:lyrics",
                          "sha256": reference["sha256"]},
            "provenance": {
                "adapter": "tools/external_analysis.py",
                "adapter_version": analysis_io.ADAPTER_VERSION,
                "source_kind": "lyric_sync",
                "request_settings": {"aligner_version": "1"},
                "generation": {"whisper_sha256": whisper_sha,
                               "reference_sha256": reference["sha256"]},
            },
        }
        self.assertTrue(external._sync_cache_accepts(
            document, whisper_sha256=whisper_sha,
            reference_sha256=reference["sha256"]))
        self.assertTrue(external._sync_cache_accepts(
            document, whisper_sha256=whisper_sha, reference_sha256=None))
        self.assertFalse(external._sync_cache_accepts(
            document, whisper_sha256="0" * 64,
            reference_sha256=reference["sha256"]))
        self.assertFalse(external._sync_cache_accepts(
            document, whisper_sha256=whisper_sha, reference_sha256="0" * 64))
        stale = json.loads(json.dumps(document))
        stale["aligner_version"] = "0"
        self.assertFalse(external._sync_cache_accepts(
            stale, whisper_sha256=whisper_sha,
            reference_sha256=reference["sha256"]))

    def test_stale_codex_fingerprint_regenerates_only_review_stage(self):
        audio = self.root / "track.wav"; audio.write_bytes(b"fixture audio")
        audio_sha = analysis_io.sha256_file(audio)
        output_dir = self.root / "analysis"; output_dir.mkdir()
        measured = measured_document(); measured["audio"]["sha256"] = audio_sha
        self.write_json("analysis/measured.json", measured)

        model = self.root / "ggml-medium.en.bin"; model.write_bytes(b"model")
        whisper_bin = self.root / "whisper-cli"; whisper_bin.write_bytes(b"bin")
        whisper = lyrics_document(); whisper["audio"]["sha256"] = audio_sha
        whisper["provenance"] = {
            "adapter": "tools/external_analysis.py",
            "adapter_version": analysis_io.ADAPTER_VERSION,
            "source_kind": "whisper_import",
            "model": model.name,
            "request_settings": {
                "language": "en", "dtw_model": "medium.en",
                "model_sha256": analysis_io.sha256_file(model),
                "gpu_requested": True,
            },
        }
        whisper_path = self.write_json("analysis/lyrics.whisper.json", whisper)
        stale_review = {
            "schema_version": external.LYRIC_REVIEW_VERSION,
            "lane": "lyric_review",
            "audio": whisper["audio"],
            "source": {
                "schema_version": whisper["schema_version"],
                "sha256": analysis_io.sha256_file(whisper_path),
            },
            "provenance": {
                "adapter": "tools/external_analysis.py",
                "adapter_version": analysis_io.ADAPTER_VERSION,
                "source_kind": "codex_lyric_review",
                "prompt_version": external.LYRIC_PROMPT_VERSION,
                "prompt_sha256": "0" * 64,
                "model": "codex-default",
            },
            "lines": [], "notes": [],
        }
        self.write_json("analysis/lyrics.review.json", stale_review)
        calls: list[list[str]] = []

        def codex_runner(argv, **_kwargs):
            if Path(argv[0]).name == "ffprobe":
                return subprocess.CompletedProcess(argv, 0, "{}", "")
            calls.append(argv)
            self.assertEqual(argv[0], "codex")
            result_path = Path(argv[argv.index("-o") + 1])
            analysis_io.atomic_write_json(result_path, {
                "lines": [{
                    "start_seconds": 1.0, "end_seconds": 2.0,
                    "text": "hello world", "source_line_indices": [0],
                    "confidence": 0.7, "uncertain": False,
                }],
                "notes": [],
            })
            return subprocess.CompletedProcess(argv, 0, "", "")

        result = external.run_assist(
            audio, output_dir, audio_duration=12, mode="lyrics",
            whisper_bin=whisper_bin, whisper_model=model,
            runner=codex_runner,
        )
        self.assertEqual(result["cache_status"], {
            "measured": "reused", "lyrics": "reused", "review": "generated",
        })
        self.assertEqual(len(calls), 1)
        regenerated = analysis_io.read_json(output_dir / "lyrics.review.json")
        self.assertEqual(
            regenerated["provenance"]["prompt_sha256"],
            analysis_io.sha256_file(external.LYRIC_PROMPT),
        )


if __name__ == "__main__":
    unittest.main()
