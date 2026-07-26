from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import musializer_doctor as doctor


SUPPORT_FILES = (
    "tools/external_analysis.py", "tools/analysis_io.py",
    "tools/analyze_audio.py", "tools/import_whisper.py",
    "tools/mimo_openrouter.py", "prompts/lyrics_cleanup_system.md",
    "schemas/analysis-cache-v1.schema.json",
    "schemas/analysis-provenance-v1.schema.json",
    "schemas/measured-analysis-v1.schema.json",
    "schemas/scene-plan-v1.schema.json",
    "schemas/codex-lyric-review-output-v1.schema.json",
    "schemas/lyric-review-v1.schema.json",
    "schemas/lyric-timing-v1.schema.json",
    "schemas/semantic-notes-v1.schema.json",
    "schemas/semantic-score-v1.schema.json",
    "tools/google_fonts.py",
    "schemas/font-import-v1.schema.json",
)


class DoctorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for relative in SUPPORT_FILES:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("{}\n", encoding="utf-8")
        app = self.root / "build/musializer"
        app.parent.mkdir(parents=True, exist_ok=True)
        app.write_bytes(b"app")
        app.chmod(0o755)
        self.analysis = self.root / "build/analysis"
        self.analysis.mkdir()
        self.output = self.root / "output"
        self.output.mkdir()
        self.whisper = self.root / "whisper-cli"
        self.whisper.write_bytes(b"whisper")
        self.whisper.chmod(0o755)
        self.model = self.root / "ggml-medium.en.bin"
        self.model.write_bytes(b"model")

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def runner(argv, **_kwargs):
        return subprocess.CompletedProcess(argv, 0, "Mock GPU, 24576 MiB\n", "")

    def test_ready_report_is_secret_free_and_capability_specific(self):
        programs = {
            "ffmpeg": "/mock/bin/ffmpeg", "ffprobe": "/mock/bin/ffprobe",
            "codex": "/mock/bin/codex", "nvidia-smi": "/mock/bin/nvidia-smi",
        }
        gpu_environment = {}

        def gpu_runner(argv, **kwargs):
            gpu_environment.update(kwargs["env"])
            return self.runner(argv, **kwargs)

        with mock.patch.object(
            doctor.external_analysis, "_default_whisper_paths",
            return_value=(self.whisper, self.model),
        ), mock.patch.object(
            doctor.external_analysis, "_openrouter_env",
            return_value={"OPENROUTER_API_KEY": "must-never-appear"},
        ) as openrouter:
            report = doctor.audit(
                root=self.root, analysis_dir=self.analysis, output_dir=self.output,
                environ={"PATH": "/mock/bin", "OPENROUTER_API_KEY": "gpu-secret"},
                which=programs.get, find_spec=lambda name: object(),
                runner=gpu_runner,
            )
        openrouter.assert_called_once_with(self.root / ".env")
        self.assertTrue(all(value["ready"] for value in report["capabilities"].values()))
        rendered = json.dumps(report, sort_keys=True)
        self.assertNotIn("must-never-appear", rendered)
        self.assertNotIn("OPENROUTER_API_KEY", rendered)
        self.assertNotIn("OPENROUTER_API_KEY", gpu_environment)
        self.assertEqual(gpu_environment["PATH"], "/mock/bin")
        self.assertEqual(report["gpu"]["kind"], "nvidia-smi")
        self.assertIn("Preview/playback: READY", doctor.render_human(report))

    def test_missing_optional_stacks_do_not_block_preview(self):
        for relative in ("tools/import_whisper.py", "tools/mimo_openrouter.py",
                         "tools/google_fonts.py"):
            (self.root / relative).unlink()
        with mock.patch.object(
            doctor.external_analysis, "_default_whisper_paths",
            return_value=(None, None),
        ), mock.patch.object(
            doctor.external_analysis, "_openrouter_env", return_value={},
        ):
            report = doctor.audit(
                root=self.root, analysis_dir=self.analysis, output_dir=self.output,
                environ={}, which=lambda _name: None,
                find_spec=lambda _name: None,
                runner=self.runner,
            )
        self.assertTrue(report["capabilities"]["preview"]["ready"])
        self.assertFalse(report["capabilities"]["export"]["ready"])
        self.assertFalse(report["capabilities"]["local_lyrics"]["ready"])
        self.assertFalse(report["capabilities"]["remote_mimo"]["ready"])
        self.assertIn("ffmpeg", report["capabilities"]["export"]["missing"])
        self.assertNotIn("ffprobe", report["capabilities"]["export"]["missing"])
        # A distribution that shipped without the helper must say so, and must
        # say it without having tried to reach the network to find out.
        self.assertFalse(report["capabilities"]["font_import"]["ready"])
        self.assertIn("font_assets", report["capabilities"]["font_import"]["missing"])

    def test_writable_probe_handles_existing_missing_and_file_paths(self):
        existing = self.root / "existing"
        existing.mkdir()
        self.assertTrue(doctor._probe_directory(existing)[0])
        self.assertTrue(doctor._probe_directory(existing / "future/nested")[0])
        regular = self.root / "not-a-directory"
        regular.write_text("x", encoding="utf-8")
        ok, detail = doctor._probe_directory(regular)
        self.assertFalse(ok)
        self.assertIn("not a directory", detail)

    def test_json_and_human_cli_do_not_probe_external_program_versions(self):
        report = {
            "schema_version": doctor.SCHEMA_VERSION,
            "root": str(self.root), "analysis_directory": str(self.analysis),
            "output_directory": str(self.output), "checks": [],
            "capabilities": {
                name: {"ready": name == "preview", "missing": [] if name == "preview" else ["mock"]}
                for name in doctor.CAPABILITIES
            },
            "gpu": {"kind": "none", "available": False, "devices": []},
        }
        with mock.patch.object(doctor, "audit", return_value=report), \
             mock.patch("builtins.print") as output:
            self.assertEqual(doctor.main(["--json"]), 0)
            payload = output.call_args.args[0]
            self.assertEqual(json.loads(payload)["schema_version"], doctor.SCHEMA_VERSION)
        with mock.patch.object(doctor, "audit", return_value=report), \
             mock.patch("builtins.print") as output:
            self.assertEqual(doctor.main(["--require", "export"]), 1)
            self.assertIn("MP4 export: BLOCKED", output.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
