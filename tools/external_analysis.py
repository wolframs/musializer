#!/usr/bin/env python3
"""Offline orchestration for Whisper, Codex lyric review, and scene planning.

External programs are invoked as argv arrays without a shell, with bounded
timeouts. Audio/lyrics are sent through files or stdin, never command-line
arguments. Only the explicit ``assist --mode mimo|all`` path may call the
existing OpenRouter helper.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable, Sequence

from analysis_io import (
    ADAPTER_VERSION,
    AnalysisValidationError,
    atomic_write_json,
    canonical_sha256,
    duration,
    read_json,
    sha256_file,
)
from import_whisper import normalize_whisper


ROOT = Path(__file__).resolve().parents[1]
LYRIC_PROMPT = ROOT / "prompts" / "lyrics_cleanup_system.md"
CODEX_OUTPUT_SCHEMA = ROOT / "schemas" / "codex-lyric-review-output-v1.schema.json"
LYRIC_REVIEW_VERSION = "musializer.lyric-review/v1"
SCENE_PLAN_VERSION = "musializer.scene-plan/v1"
SEMANTIC_NOTES_VERSION = "musializer.semantic-notes/v1"
BRIDGE_VERSION = "MUSIALIZER_BRIDGE\t1"
SCENES = ("spectrum", "pulse", "orbital", "ascii", "atlas", "terrarium", "constellation")

Runner = Callable[..., subprocess.CompletedProcess[str]]


def _run(
    argv: Sequence[str], *, timeout: float, stdin: str | None = None,
    cwd: Path | None = None, env: dict[str, str] | None = None,
    runner: Runner = subprocess.run,
) -> subprocess.CompletedProcess[str]:
    if not argv or timeout <= 0 or not math.isfinite(timeout):
        raise AnalysisValidationError("external command and positive finite timeout are required")
    try:
        result = runner(
            list(argv), input=stdin, text=True, capture_output=True,
            timeout=timeout, check=False, cwd=str(cwd) if cwd else None,
            env=env,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{Path(argv[0]).name} exceeded its {timeout:g}s timeout") from error
    except OSError as error:
        raise RuntimeError(f"could not start {Path(argv[0]).name}: {error}") from error
    if result.returncode != 0:
        # Child output may contain private lyrics, paths, or provider diagnostics.
        # Keep it out of logs; callers can rerun the child explicitly to debug.
        raise RuntimeError(f"{Path(argv[0]).name} exited with code {result.returncode}")
    return result


def _safe_local_env() -> dict[str, str]:
    """Do not expose unrelated API credentials to local analysis children."""
    sensitive = ("KEY", "TOKEN", "SECRET", "PASSWORD", "CREDENTIAL", "AUTH")
    return {key: value for key, value in os.environ.items()
            if not any(marker in key.upper() for marker in sensitive)}


def _openrouter_env(dotenv_path: Path | None = None) -> dict[str, str]:
    """Expose only the one credential authorized for the MiMo helper.

    Desktop launchers do not normally inherit interactive shell variables, so
    the repository's ignored .env is accepted as an explicit local credential
    store. It is parsed as data, never sourced as shell code.
    """
    environment = _safe_local_env()
    key = os.environ.get("OPENROUTER_API_KEY", "").strip()
    path = dotenv_path or ROOT / ".env"
    if not key and path.is_file():
        for raw_line in path.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            name, value = line.split("=", 1)
            if name.strip() != "OPENROUTER_API_KEY":
                continue
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            key = value.strip()
            break
    if key:
        environment["OPENROUTER_API_KEY"] = key
    return environment


def _enter_process_group() -> None:
    """Isolate a UI-owned worker so cancellation reaches all of its children."""
    if os.name == "posix":
        os.setsid()


def _command_description(argv: Sequence[str], private_positions: set[int] | None = None) -> list[str]:
    private_positions = private_positions or set()
    return ["<private-file>" if i in private_positions else value for i, value in enumerate(argv)]


def whisper_request(
    audio: Path, *, whisper_bin: Path, model: Path, language: str,
    dtw_model: str | None, ffmpeg: str, output_prefix: Path,
) -> tuple[list[str], list[str]]:
    wav = output_prefix.with_suffix(".16k.wav")
    decode = [
        ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(audio), "-vn", "-ac", "1", "-ar", "16000",
        "-c:a", "pcm_s16le", str(wav),
    ]
    whisper = [
        str(whisper_bin), "-f", str(wav), "--output-file", str(output_prefix),
        "--output-json", "-ojf", "-m", str(model), "-l", language,
    ]
    if dtw_model:
        whisper.extend(["--dtw", dtw_model])
    return decode, whisper


def run_whisper(
    audio: Path, output: Path, *, audio_duration: float, whisper_bin: Path,
    model: Path, language: str = "en", dtw_model: str | None = None,
    ffmpeg: str = "ffmpeg", timeout: float = 3600.0,
    decode_timeout: float = 600.0, raw_output: Path | None = None,
    dry_run: bool = False, runner: Runner = subprocess.run,
) -> dict[str, Any]:
    audio_duration = duration(audio_duration)
    if not audio.is_file() or not whisper_bin.is_file() or not model.is_file():
        raise AnalysisValidationError("audio, Whisper executable, and model must be files")
    audio_sha = sha256_file(audio)
    model_sha = sha256_file(model)
    if dtw_model is None and model.name.startswith("ggml-") and model.name.endswith(".bin"):
        dtw_model = model.name[len("ggml-"):-len(".bin")]
    with tempfile.TemporaryDirectory(prefix="musializer-whisper-") as temporary:
        prefix = Path(temporary) / "transcription"
        decode, whisper = whisper_request(
            audio, whisper_bin=whisper_bin, model=model, language=language,
            dtw_model=dtw_model, ffmpeg=ffmpeg, output_prefix=prefix,
        )
        request = {
            "dry_run": dry_run,
            "audio_sha256": audio_sha,
            "model_sha256": model_sha,
            "language": language,
            "dtw_model": dtw_model,
            "timeouts_seconds": {"decode": decode_timeout, "whisper": timeout},
            "decode_argv": _command_description(decode, {7, len(decode) - 1}),
            "whisper_argv": _command_description(whisper, {2, 4, whisper.index("-m") + 1}),
        }
        if dry_run:
            return request
        local_env = _safe_local_env()
        _run(decode, timeout=decode_timeout, env=local_env, runner=runner)
        _run(whisper, timeout=timeout, env=local_env, runner=runner)
        produced = Path(f"{prefix}.json")
        if not produced.is_file():
            raise RuntimeError("Whisper completed without producing its requested JSON file")
        raw = read_json(produced)
        normalized = normalize_whisper(
            raw, audio_sha256=audio_sha, audio_duration=audio_duration,
            model=model.name,
        )
        normalized["provenance"]["adapter"] = "tools/external_analysis.py"
        normalized["provenance"]["adapter_version"] = ADAPTER_VERSION
        normalized["provenance"]["request_settings"] = {
            "language": language, "dtw_model": dtw_model,
            "model_sha256": model_sha, "gpu_requested": True,
        }
        normalized["provenance"]["generation"] = {
            "raw_whisper_sha256": canonical_sha256(raw),
        }
        atomic_write_json(raw_output or output.with_suffix(".whisper.raw.json"), raw)
        atomic_write_json(output, normalized)
        return normalized


def _validate_codex_review(raw: Any, source: dict[str, Any]) -> tuple[list[dict[str, Any]], list[str]]:
    if not isinstance(raw, dict) or not isinstance(raw.get("lines"), list) or not isinstance(raw.get("notes"), list):
        raise AnalysisValidationError("Codex lyric review must contain lines and notes arrays")
    source_lines = source.get("lines")
    if not isinstance(source_lines, list):
        raise AnalysisValidationError("source lyric lane has no lines array")
    audio_duration = duration(source.get("audio", {}).get("duration_seconds"))
    used: set[int] = set()
    previous_start = -1.0
    cleaned: list[dict[str, Any]] = []
    for index, line in enumerate(raw["lines"]):
        if not isinstance(line, dict):
            raise AnalysisValidationError(f"review line {index} is not an object")
        indices = line.get("source_line_indices")
        if not isinstance(indices, list) or not indices or any(type(i) is not int for i in indices):
            raise AnalysisValidationError(f"review line {index} lacks source evidence")
        if len(set(indices)) != len(indices) or any(i < 0 or i >= len(source_lines) for i in indices):
            raise AnalysisValidationError(f"review line {index} cites an invalid source line")
        if used.intersection(indices):
            raise AnalysisValidationError("one Whisper line cannot support multiple reviewed lines")
        used.update(indices)
        try:
            start = float(line["start_seconds"])
            end = float(line["end_seconds"])
            confidence = float(line["confidence"])
        except (KeyError, TypeError, ValueError) as error:
            raise AnalysisValidationError(f"review line {index} has invalid numbers") from error
        text = line.get("text")
        if not all(math.isfinite(value) for value in (start, end, confidence)) or not isinstance(text, str):
            raise AnalysisValidationError(f"review line {index} has non-finite values or non-text content")
        text = text.strip()
        envelope_start = min(float(source_lines[i]["start_seconds"]) for i in indices)
        envelope_end = max(float(source_lines[i]["end_seconds"]) for i in indices)
        if (not text or len(text) > 2048 or start < max(0.0, envelope_start - 0.25) or
            end > min(audio_duration, envelope_end + 0.25) or end <= start or
            start < previous_start or not 0.0 <= confidence <= 1.0):
            raise AnalysisValidationError(f"review line {index} violates evidence/timing bounds")
        if type(line.get("uncertain")) is not bool:
            raise AnalysisValidationError(f"review line {index} lacks an uncertainty flag")
        previous_start = start
        cleaned.append({
            "start_seconds": start, "end_seconds": end, "text": text,
            "source_line_indices": indices, "confidence": confidence,
            "uncertain": line["uncertain"],
        })
    notes = [str(note).strip() for note in raw["notes"] if str(note).strip()]
    return cleaned, notes


def codex_review_request(source: dict[str, Any]) -> str:
    prompt = LYRIC_PROMPT.read_text(encoding="utf-8")
    evidence = {
        "audio": source.get("audio"),
        "lines": source.get("lines", []),
        "words": source.get("words", []),
    }
    return f"{prompt}\n\nWhisper evidence JSON follows:\n{json.dumps(evidence, ensure_ascii=False)}\n"


def run_codex_review(
    lyrics: Path, output: Path, *, codex_bin: str = "codex",
    model: str | None = None, timeout: float = 600.0, dry_run: bool = False,
    runner: Runner = subprocess.run,
) -> dict[str, Any]:
    source = read_json(lyrics)
    if source.get("schema_version") != "musializer.lyric-timing/v1":
        raise AnalysisValidationError("Codex review input must be a lyric-timing/v1 lane")
    source_sha = sha256_file(lyrics)
    prompt_sha = sha256_file(LYRIC_PROMPT)
    argv = [
        codex_bin, "exec", "--ephemeral", "--ignore-user-config",
        "--sandbox", "read-only", "--output-schema", str(CODEX_OUTPUT_SCHEMA),
        "--skip-git-repo-check", "-C", "<isolated-workdir>",
        "-o", "<temporary-output>", "-",
    ]
    if model:
        argv[2:2] = ["--model", model]
    request = {
        "dry_run": dry_run, "timeout_seconds": timeout,
        "source_sha256": source_sha, "prompt_sha256": prompt_sha,
        "model": model, "argv": argv,
        "stdin": "<repository prompt plus private lyric evidence omitted>",
    }
    if dry_run:
        return request
    with tempfile.TemporaryDirectory(prefix="musializer-codex-") as temporary:
        result_path = Path(temporary) / "review.json"
        actual = [str(result_path) if value == "<temporary-output>" else
                  temporary if value == "<isolated-workdir>" else value for value in argv]
        _run(actual, timeout=timeout, stdin=codex_review_request(source),
             cwd=Path(temporary), env=_safe_local_env(), runner=runner)
        raw = read_json(result_path)
    lines, notes = _validate_codex_review(raw, source)
    reviewed = {
        "schema_version": LYRIC_REVIEW_VERSION,
        "lane": "lyric_review",
        "audio": source["audio"],
        "source": {
            "schema_version": source["schema_version"],
            "sha256": source_sha,
            "adapter": source.get("provenance", {}).get("adapter"),
        },
        "provenance": {
            "adapter": "tools/external_analysis.py",
            "adapter_version": ADAPTER_VERSION,
            "source_kind": "codex_lyric_review",
            "prompt_version": "lyrics_cleanup_system/v1",
            "prompt_sha256": prompt_sha,
            "model": model or "codex-default",
            "request_settings": {"sandbox": "read-only", "ephemeral": True},
        },
        "lines": lines,
        "notes": notes,
    }
    atomic_write_json(output, reviewed)
    return reviewed


def import_mimo_export(export_path: Path, audio_path: Path, audio_duration: float) -> dict[str, Any]:
    """Extract assistant final text without copying embedded audio or reasoning."""
    value = read_json(export_path)
    texts: list[str] = []
    if isinstance(value, dict):
        items = value.get("items", {})
        iterable = items.values() if isinstance(items, dict) else items if isinstance(items, list) else []
        for item in iterable:
            data = item.get("data", {}) if isinstance(item, dict) else {}
            content = data.get("content", [])
            for part in content if isinstance(content, list) else []:
                if (isinstance(part, dict) and part.get("type") == "output_text" and
                    isinstance(part.get("text"), str) and part["text"].strip()):
                    texts.append(part["text"].strip())
        if not texts and isinstance(value.get("choices"), list):
            for choice in value["choices"]:
                content = choice.get("message", {}).get("content") if isinstance(choice, dict) else None
                if isinstance(content, str) and content.strip(): texts.append(content.strip())
    if not texts:
        raise AnalysisValidationError("MiMo export contains no assistant output_text")
    return {
        "schema_version": SEMANTIC_NOTES_VERSION,
        "lane": "semantic_interpretation_notes",
        "audio": {"sha256": sha256_file(audio_path), "duration_seconds": duration(audio_duration)},
        "provenance": {
            "adapter": "tools/external_analysis.py", "adapter_version": ADAPTER_VERSION,
            "source_kind": "mimo_openrouter_export", "source_sha256": sha256_file(export_path),
            "quantitative_values_available": False,
        },
        "text": "\n\n".join(texts),
    }


def _semantic_document(value: Any) -> dict[str, Any] | None:
    if value is None:
        return None
    if isinstance(value, dict) and value.get("schema_version") == "musializer.analysis-cache/v1":
        value = value.get("normalized")
    if isinstance(value, dict) and value.get("schema_version") == SEMANTIC_NOTES_VERSION:
        if not isinstance(value.get("text"), str) or not value["text"].strip():
            raise AnalysisValidationError("semantic notes are empty")
        return value
    if not isinstance(value, dict) or value.get("schema_version") != "musializer.semantic-score/v1":
        raise AnalysisValidationError("semantic input must be a semantic-score/v1 document or cache")
    if not isinstance(value.get("segments"), list) or not value["segments"]:
        raise AnalysisValidationError("semantic score has no segments")
    return value


def _source_ref(path: Path, document: dict[str, Any]) -> dict[str, Any]:
    return {
        "path": str(path), "sha256": sha256_file(path),
        "schema_version": document.get("schema_version"), "lane": document.get("lane"),
    }


def _frame_average(frames: list[dict[str, Any]], start: float, end: float) -> dict[str, float]:
    selected = [f for f in frames if start <= float(f.get("time_seconds", -1)) < end]
    if not selected:
        return {"rms": 0.0, "spectral_flux": 0.0, "onset_strength": 0.0}
    return {
        key: sum(float(frame.get(key, 0.0)) for frame in selected)/len(selected)
        for key in ("rms", "spectral_flux", "onset_strength")
    }


def _semantic_at(semantic: dict[str, Any] | None, time_seconds: float) -> dict[str, Any] | None:
    if semantic is None:
        return None
    if semantic.get("schema_version") == SEMANTIC_NOTES_VERSION:
        return {"summary": semantic["text"], "moods": [], "motion": [], "imagery": []}
    for segment in semantic["segments"]:
        if float(segment["start_seconds"]) <= time_seconds < float(segment["end_seconds"]):
            return segment
    return semantic["segments"][-1]


def _scene_for(features: dict[str, float], semantic: dict[str, Any] | None,
               lyric_count: int, section_index: int) -> tuple[str, list[dict[str, Any]]]:
    reasons: list[dict[str, Any]] = [{
        "source_lane": "measured_audio",
        "detail": f"rms={features['rms']:.3f}, flux={features['spectral_flux']:.3f}, onset={features['onset_strength']:.3f}",
    }]
    words: set[str] = set()
    energy = features["rms"]
    if semantic:
        semantic_words = (semantic.get("moods", []) + semantic.get("motion", []) +
                          semantic.get("imagery", []) + [semantic.get("summary", "")])
        words = {token.lower() for phrase in semantic_words for token in str(phrase).replace("-", " ").split()}
        energy = max(energy, float(semantic.get("energy", 0.0)))
        reasons.append({
            "source_lane": "semantic_interpretation",
            "detail": str(semantic.get("summary", ""))[:512],
        })
    keyword_scenes = (
        ({"cosmic", "stars", "celestial", "dream", "space"}, "constellation"),
        ({"organic", "growth", "forest", "creature", "earth"}, "terrarium"),
        ({"journey", "landscape", "terrain", "vast", "horizon"}, "atlas"),
        ({"mechanical", "drive", "tunnel", "kinetic", "industrial"}, "orbital"),
    )
    for keywords, scene in keyword_scenes:
        if words.intersection(keywords):
            return scene, reasons
    if lyric_count > 0 and energy < 0.48:
        return "ascii", reasons
    if energy > 0.72 or features["onset_strength"] > 0.55:
        return "pulse", reasons
    if features["spectral_flux"] > 0.42:
        return "orbital", reasons
    return ("spectrum" if section_index % 2 == 0 else "constellation"), reasons


def build_scene_plan(
    measured_path: Path, *, lyrics_path: Path | None = None,
    semantic_path: Path | None = None, minimum_section: float = 4.0,
    maximum_section: float = 24.0,
) -> dict[str, Any]:
    measured = read_json(measured_path)
    if measured.get("schema_version") != "musializer.measured-analysis/v1":
        raise AnalysisValidationError("measured input must be measured-analysis/v1")
    audio = measured.get("audio", {})
    audio_duration = duration(audio.get("duration_seconds"))
    audio_sha = audio.get("sha256")
    lyrics = read_json(lyrics_path) if lyrics_path else None
    semantic = _semantic_document(read_json(semantic_path)) if semantic_path else None
    for name, document in (("lyrics", lyrics), ("semantic", semantic)):
        if document and document.get("audio", {}).get("sha256") != audio_sha:
            raise AnalysisValidationError(f"{name} lane belongs to different audio")

    candidates: list[tuple[float, float, str]] = [(0.0, 1.0, "start"), (audio_duration, 1.0, "end")]
    for section in measured.get("summary", {}).get("sections", [])[1:]:
        candidates.append((float(section["start_seconds"]), 0.68, "measured_section"))
    if semantic and semantic.get("schema_version") == "musializer.semantic-score/v1":
        previous = semantic["segments"][0]
        for segment in semantic["segments"][1:]:
            change = abs(float(segment.get("energy", 0)) - float(previous.get("energy", 0)))
            change += abs(float(segment.get("tension", 0)) - float(previous.get("tension", 0)))*0.5
            candidates.append((float(segment["start_seconds"]), min(0.95, 0.55 + change*0.35), "semantic_change"))
            previous = segment
    lyric_lines = lyrics.get("lines", []) if isinstance(lyrics, dict) else []
    for previous, following in zip(lyric_lines, lyric_lines[1:]):
        gap = float(following["start_seconds"]) - float(previous["end_seconds"])
        if gap >= 2.5:
            candidates.append((float(following["start_seconds"]), min(0.8, 0.45 + gap/20.0), "lyric_reentry"))

    candidates.sort()
    merged: list[tuple[float, float, str]] = []
    for candidate in candidates:
        if merged and candidate[0] - merged[-1][0] < minimum_section and candidate[0] != audio_duration:
            if candidate[1] > merged[-1][1] and merged[-1][0] != 0.0:
                merged[-1] = candidate
            continue
        merged.append(candidate)
    if merged[-1][0] != audio_duration:
        merged.append((audio_duration, 1.0, "end"))

    boundaries: list[tuple[float, float, str]] = [merged[0]]
    for right in merged[1:]:
        left_time = boundaries[-1][0]
        while right[0] - left_time > maximum_section:
            forced = min(right[0], left_time + maximum_section)
            boundaries.append((forced, 0.35, "maximum_duration"))
            left_time = forced
        boundaries.append(right)

    frames = measured.get("frames", [])
    sections: list[dict[str, Any]] = []
    for index, (left, right) in enumerate(zip(boundaries, boundaries[1:])):
        start, end = left[0], right[0]
        features = _frame_average(frames, start, end)
        midpoint = (start + end)*0.5
        semantic_segment = _semantic_at(semantic, midpoint)
        lyric_count = sum(start <= float(line["start_seconds"]) < end for line in lyric_lines)
        scene, reasons = _scene_for(features, semantic_segment, lyric_count, index)
        reasons.append({"source_lane": left[2], "detail": "boundary evidence"})
        sections.append({
            "id": f"section-{index:04d}", "start_seconds": start,
            "end_seconds": end, "recommended_scene": scene,
            "transition_strength": 0.0 if index == 0 else left[1],
            "reasons": reasons,
        })
    sources = [_source_ref(measured_path, measured)]
    if lyrics_path and lyrics: sources.append(_source_ref(lyrics_path, lyrics))
    if semantic_path and semantic: sources.append(_source_ref(semantic_path, semantic))
    plan = {
        "schema_version": SCENE_PLAN_VERSION, "lane": "scene_plan",
        "audio": {"sha256": audio_sha, "duration_seconds": audio_duration},
        "sources": sources,
        "provenance": {
            "adapter": "tools/external_analysis.py", "adapter_version": ADAPTER_VERSION,
            "source_kind": "deterministic_section_planner",
            "request_settings": {"minimum_section": minimum_section, "maximum_section": maximum_section},
        },
        "sections": sections,
    }
    validate_scene_plan(plan)
    return plan


def validate_scene_plan(plan: dict[str, Any]) -> None:
    total = duration(plan.get("audio", {}).get("duration_seconds"))
    sections = plan.get("sections")
    if not isinstance(sections, list) or not sections:
        raise AnalysisValidationError("scene plan needs sections")
    cursor = 0.0
    for index, section in enumerate(sections):
        start = float(section.get("start_seconds", -1))
        end = float(section.get("end_seconds", -1))
        if abs(start - cursor) > 1e-6 or end <= start or end > total + 1e-6:
            raise AnalysisValidationError(f"scene section {index} breaks full-duration coverage")
        if section.get("recommended_scene") not in SCENES:
            raise AnalysisValidationError(f"scene section {index} has an unknown scene")
        cursor = end
    if abs(cursor - total) > 1e-6:
        raise AnalysisValidationError("scene plan does not cover complete audio")


def _stable_id(kind: str, index: int, start_ms: int, text: str = "") -> int:
    digest = hashlib.sha256(f"{kind}\0{index}\0{start_ms}\0{text}".encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "big") or 1


def _b64(value: str) -> str:
    return base64.b64encode(value.encode("utf-8")).decode("ascii")


def build_bridge(
    plan: dict[str, Any], *, lyrics: dict[str, Any] | None = None,
    semantic: dict[str, Any] | None = None,
) -> str:
    validate_scene_plan(plan)
    audio = plan["audio"]
    lines = [BRIDGE_VERSION, f"AUDIO\t{audio['sha256']}\t{round(float(audio['duration_seconds'])*1000)}"]
    for index, lyric in enumerate((lyrics or {}).get("lines", [])):
        start = round(float(lyric["start_seconds"])*1000)
        end = round(float(lyric["end_seconds"])*1000)
        confidence = lyric.get("confidence")
        confidence_milli = -1 if confidence is None else round(float(confidence)*1000)
        flags = "uncertain" if lyric.get("uncertain") else "none"
        text = str(lyric["text"])
        lines.append(f"LYRIC\t{_stable_id('lyric', index, start, text)}\t{start}\t{end}\t{confidence_milli}\t{flags}\t{_b64(text)}")
    for index, section in enumerate(plan["sections"]):
        start = round(float(section["start_seconds"])*1000)
        end = round(float(section["end_seconds"])*1000)
        reason = json.dumps(section["reasons"], ensure_ascii=False, separators=(",", ":"))
        lines.append(f"SECTION\t{_stable_id('section', index, start)}\t{start}\t{end}\t{section['recommended_scene']}\t{round(float(section['transition_strength'])*1000)}\t{_b64(reason)}")
    if semantic and semantic.get("schema_version") == "musializer.semantic-score/v1":
        for index, cue in enumerate(semantic["segments"]):
            start = round(float(cue["start_seconds"])*1000)
            end = round(float(cue["end_seconds"])*1000)
            summary = str(cue.get("summary", ""))
            values = [
                round(float(cue.get("energy", 0))*1000),
                round(float(cue.get("tension", 0))*1000),
                round(float(cue.get("valence", 0))*1000),
                round(float(cue.get("confidence", 0))*1000),
            ]
            lines.append(f"SEMANTIC\t{_stable_id('semantic', index, start, summary)}\t{start}\t{end}\t" +
                         "\t".join(map(str, values)) + f"\t{_b64(summary)}")
    elif semantic and semantic.get("schema_version") == SEMANTIC_NOTES_VERSION:
        text = semantic["text"]
        lines.append(f"SEMANTIC_NOTE\t{_stable_id('semantic-note', 0, 0, text)}\t{_b64(text)}")
    result = "\n".join(lines) + "\n"
    parse_bridge(result)
    return result


def parse_bridge(value: str) -> list[list[str]]:
    rows = [line.split("\t") for line in value.splitlines()]
    if not rows or rows[0] != ["MUSIALIZER_BRIDGE", "1"]:
        raise AnalysisValidationError("invalid bridge header")
    expected = {"AUDIO": 3, "LYRIC": 7, "SECTION": 7, "SEMANTIC": 9, "SEMANTIC_NOTE": 3}
    if len(rows) < 2 or rows[1][0] != "AUDIO":
        raise AnalysisValidationError("bridge lacks AUDIO record")
    if len(rows[1][1]) != 64 or any(char not in "0123456789abcdef" for char in rows[1][1]):
        raise AnalysisValidationError("bridge AUDIO hash is invalid")
    audio_duration_ms = int(rows[1][2])
    if audio_duration_ms <= 0: raise AnalysisValidationError("bridge AUDIO duration is invalid")
    ids: set[int] = set()
    previous_time: dict[str, int] = {}
    for row in rows[1:]:
        if row[0] not in expected or len(row) != expected[row[0]]:
            raise AnalysisValidationError("invalid bridge record shape")
        if row[0] in {"LYRIC", "SECTION", "SEMANTIC"}:
            stable_id, start, end = int(row[1]), int(row[2]), int(row[3])
            if stable_id == 0 or stable_id in ids or start < 0 or end <= start or end > audio_duration_ms:
                raise AnalysisValidationError("bridge record id/timing is invalid")
            if start < previous_time.get(row[0], -1):
                raise AnalysisValidationError("bridge records are not time ordered")
            ids.add(stable_id); previous_time[row[0]] = start
            decoded = base64.b64decode(row[-1], validate=True)
            if len(decoded) > 1024*1024: raise AnalysisValidationError("bridge decoded field is too large")
            if row[0] == "LYRIC":
                confidence = int(row[4])
                if confidence < -1 or confidence > 1000 or row[5] not in {"none", "uncertain"}:
                    raise AnalysisValidationError("bridge lyric metadata is invalid")
            elif row[0] == "SECTION":
                if row[4] not in SCENES or not 0 <= int(row[5]) <= 1000:
                    raise AnalysisValidationError("bridge section metadata is invalid")
            else:
                energy, tension, valence, confidence = map(int, row[4:8])
                if not (0 <= energy <= 1000 and 0 <= tension <= 1000 and
                        -1000 <= valence <= 1000 and 0 <= confidence <= 1000):
                    raise AnalysisValidationError("bridge semantic metadata is invalid")
        elif row[0] == "SEMANTIC_NOTE":
            stable_id = int(row[1])
            decoded = base64.b64decode(row[2], validate=True)
            if stable_id == 0 or stable_id in ids or len(decoded) > 1024*1024:
                raise AnalysisValidationError("bridge semantic note is invalid")
            ids.add(stable_id)
    return rows


def atomic_write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
                                     prefix=f".{path.name}.", suffix=".tmp", delete=False) as output:
        temporary = Path(output.name)
        output.write(value)
        output.flush()
        os.fsync(output.fileno())
    try:
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _cache_matches(path: Path, schema_version: str, audio_sha: str) -> dict[str, Any] | None:
    if not path.is_file(): return None
    try:
        value = read_json(path)
    except (OSError, ValueError):
        return None
    if value.get("schema_version") != schema_version: return None
    document = value.get("normalized", {}) if schema_version == "musializer.analysis-cache/v1" else value
    return value if document.get("audio", {}).get("sha256") == audio_sha else None


def _default_whisper_paths() -> tuple[Path | None, Path | None]:
    install = Path("/tmp/music-visualizations-whisper-1.8.6")
    binary = os.environ.get("MUSIALIZER_WHISPER_BIN")
    model = os.environ.get("MUSIALIZER_WHISPER_MODEL")
    return (
        Path(binary) if binary else (install / "build/bin/whisper-cli" if (install / "build/bin/whisper-cli").is_file() else None),
        Path(model) if model else (install / "ggml-medium.en.bin" if (install / "ggml-medium.en.bin").is_file() else None),
    )


def run_assist(
    audio: Path, output_dir: Path, *, audio_duration: float, mode: str,
    bridge_path: Path | None = None, whisper_bin: Path | None = None,
    whisper_model: Path | None = None, codex_bin: str = "codex",
    codex_model: str | None = None, semantic_cache: Path | None = None,
    zdr: bool = False, external_timeout: float = 2400.0,
    dry_run: bool = False, runner: Runner = subprocess.run,
) -> dict[str, Any]:
    """Run one complete, cache-aware UI action and emit JSON plus TSV bridge."""
    if mode not in {"lyrics", "sections", "mimo", "all"}:
        raise AnalysisValidationError("assist mode must be lyrics, sections, mimo, or all")
    audio_duration = duration(audio_duration)
    if not audio.is_file(): raise AnalysisValidationError("assist audio file does not exist")
    if external_timeout < 600 or not math.isfinite(external_timeout):
        raise AnalysisValidationError("assist external timeout must be at least 600 seconds")
    output_dir.mkdir(parents=True, exist_ok=True)
    audio_sha = sha256_file(audio)
    paths = {
        "measured": output_dir / "measured.json",
        "lyrics": output_dir / "lyrics.whisper.json",
        "review": output_dir / "lyrics.review.json",
        "semantic": semantic_cache or output_dir / "semantic.cache.json",
        "plan": output_dir / "scene-plan.json",
        "bridge": bridge_path or output_dir / "analysis.bridge.tsv",
        "manifest": output_dir / "assist-manifest.json",
    }
    detected_bin, detected_model = _default_whisper_paths()
    whisper_bin = whisper_bin or detected_bin
    whisper_model = whisper_model or detected_model
    actions = ["measured", "plan", "bridge"]
    if mode in {"lyrics", "all"}: actions[1:1] = ["whisper", "codex_lyric_review"]
    if mode in {"mimo", "all"}: actions[1:1] = ["mimo_openrouter"]
    if dry_run:
        return {
            "dry_run": True, "mode": mode, "audio_sha256": audio_sha,
            "actions": actions, "paths": {key: str(value) for key, value in paths.items()},
            "whisper_configured": bool(whisper_bin and whisper_model),
            "external_timeout_seconds": external_timeout,
            "credentials": "environment only; omitted",
        }

    cache_status: dict[str, str] = {}
    measured = _cache_matches(paths["measured"], "musializer.measured-analysis/v1", audio_sha)
    if measured is None:
        _run(
            [sys.executable, str(ROOT / "tools/analyze_audio.py"), str(audio), str(paths["measured"])],
            timeout=external_timeout, env=_safe_local_env(), runner=runner,
        )
        measured = _cache_matches(paths["measured"], "musializer.measured-analysis/v1", audio_sha)
        if measured is None: raise RuntimeError("measured analyzer produced an invalid cache")
        cache_status["measured"] = "generated"
    else: cache_status["measured"] = "reused"
    measured_duration = duration(measured.get("audio", {}).get("duration_seconds"))

    lyrics: dict[str, Any] | None = _cache_matches(paths["review"], LYRIC_REVIEW_VERSION, audio_sha)
    if mode in {"lyrics", "all"}:
        whisper_lane = _cache_matches(paths["lyrics"], "musializer.lyric-timing/v1", audio_sha)
        if whisper_lane is None:
            if whisper_bin is None or whisper_model is None:
                raise AnalysisValidationError("GPU Whisper is not configured or autodetectable")
            whisper_lane = run_whisper(
                audio, paths["lyrics"], audio_duration=measured_duration,
                whisper_bin=whisper_bin, model=whisper_model,
                timeout=external_timeout, runner=runner,
            )
            cache_status["lyrics"] = "generated"
        else: cache_status["lyrics"] = "reused"
        source_sha = sha256_file(paths["lyrics"])
        if lyrics is None or lyrics.get("source", {}).get("sha256") != source_sha:
            lyrics = run_codex_review(
                paths["lyrics"], paths["review"], codex_bin=codex_bin,
                model=codex_model, timeout=external_timeout, runner=runner,
            )
            cache_status["review"] = "generated"
        else: cache_status["review"] = "reused"

    semantic: dict[str, Any] | None = None
    if mode in {"mimo", "all"}:
        envelope = _cache_matches(paths["semantic"], "musializer.analysis-cache/v1", audio_sha)
        if envelope is None:
            command = [
                sys.executable, str(ROOT / "tools/mimo_openrouter.py"), str(audio),
                str(paths["semantic"]), "--duration", f"{measured_duration:.9f}",
            ]
            if zdr: command.append("--zdr")
            _run(command, timeout=external_timeout, env=_openrouter_env(), runner=runner)
            envelope = _cache_matches(paths["semantic"], "musializer.analysis-cache/v1", audio_sha)
            if envelope is None: raise RuntimeError("MiMo helper produced an invalid cache")
            cache_status["semantic"] = "generated"
        else: cache_status["semantic"] = "reused"
        semantic = _semantic_document(envelope)
    elif paths["semantic"].is_file():
        try:
            candidate = _semantic_document(read_json(paths["semantic"]))
            if candidate and candidate.get("audio", {}).get("sha256") == audio_sha: semantic = candidate
        except (OSError, ValueError):
            semantic = None

    lyrics_path = paths["review"] if lyrics else None
    plan = build_scene_plan(
        paths["measured"], lyrics_path=lyrics_path,
        semantic_path=paths["semantic"] if semantic else None,
    )
    atomic_write_json(paths["plan"], plan)
    atomic_write_text(paths["bridge"], build_bridge(plan, lyrics=lyrics, semantic=semantic))
    manifest = {
        "schema_version": "musializer.assist-manifest/v1", "mode": mode,
        "audio": {"sha256": audio_sha, "duration_seconds": measured_duration},
        "cache_status": cache_status,
        "artifacts": {key: str(value) for key, value in paths.items() if key != "manifest"},
        "provenance_streams": [
            "measured_audio", *( ["lyrics", "lyric_review"] if lyrics else [] ),
            *( [semantic.get("lane")] if semantic else [] ), "scene_plan",
        ],
    }
    atomic_write_json(paths["manifest"], manifest)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    whisper = sub.add_parser("whisper", help="run configured GPU whisper.cpp and normalize timings")
    whisper.add_argument("audio", type=Path); whisper.add_argument("output", type=Path)
    whisper.add_argument("--duration", type=float, required=True)
    whisper.add_argument("--whisper-bin", type=Path, default=os.environ.get("MUSIALIZER_WHISPER_BIN"))
    whisper.add_argument("--model", type=Path, default=os.environ.get("MUSIALIZER_WHISPER_MODEL"))
    whisper.add_argument("--language", default="en"); whisper.add_argument("--dtw-model")
    whisper.add_argument("--ffmpeg", default="ffmpeg"); whisper.add_argument("--timeout", type=float, default=3600)
    whisper.add_argument("--decode-timeout", type=float, default=600); whisper.add_argument("--raw-output", type=Path)
    whisper.add_argument("--dry-run", action="store_true"); whisper.add_argument("--request-dump", type=Path)

    clean = sub.add_parser("clean-lyrics", help="run evidence-preserving Codex lyric review")
    clean.add_argument("lyrics", type=Path); clean.add_argument("output", type=Path)
    clean.add_argument("--codex-bin", default="codex"); clean.add_argument("--model")
    clean.add_argument("--timeout", type=float, default=600); clean.add_argument("--dry-run", action="store_true")
    clean.add_argument("--request-dump", type=Path)

    mimo = sub.add_parser("import-mimo", help="extract final notes from an existing OpenRouter Chat export")
    mimo.add_argument("export", type=Path); mimo.add_argument("audio", type=Path); mimo.add_argument("output", type=Path)
    mimo.add_argument("--duration", type=float, required=True)

    plan_parser = sub.add_parser("plan", help="derive deterministic scene-switch sections")
    plan_parser.add_argument("measured", type=Path); plan_parser.add_argument("output", type=Path)
    plan_parser.add_argument("--lyrics", type=Path); plan_parser.add_argument("--semantic", type=Path)
    plan_parser.add_argument("--minimum-section", type=float, default=4); plan_parser.add_argument("--maximum-section", type=float, default=24)
    plan_parser.add_argument("--bridge", type=Path)

    assist = sub.add_parser("assist", help="cache-aware one-shot orchestration for a UI action")
    assist.add_argument("audio", type=Path); assist.add_argument("output_dir", type=Path)
    assist.add_argument("--duration", type=float, required=True)
    assist.add_argument("--mode", choices=("lyrics", "sections", "mimo", "all"), required=True)
    assist.add_argument("--bridge", type=Path); assist.add_argument("--whisper-bin", type=Path)
    assist.add_argument("--whisper-model", type=Path); assist.add_argument("--codex-bin", default="codex")
    assist.add_argument("--codex-model"); assist.add_argument("--semantic-cache", type=Path)
    assist.add_argument("--zdr", action="store_true"); assist.add_argument("--timeout", type=float, default=2400)
    assist.add_argument("--new-process-group", action="store_true", help=argparse.SUPPRESS)
    assist.add_argument("--dry-run", action="store_true"); assist.add_argument("--request-dump", type=Path)

    args = parser.parse_args(argv)
    try:
        if args.command == "whisper":
            if args.whisper_bin is None or args.model is None:
                raise AnalysisValidationError("configure --whisper-bin and --model (or MUSIALIZER_WHISPER_BIN/MODEL)")
            result = run_whisper(
                args.audio, args.output, audio_duration=args.duration,
                whisper_bin=args.whisper_bin, model=args.model, language=args.language,
                dtw_model=args.dtw_model, ffmpeg=args.ffmpeg, timeout=args.timeout,
                decode_timeout=args.decode_timeout, raw_output=args.raw_output,
                dry_run=args.dry_run,
            )
            if args.dry_run:
                if args.request_dump: atomic_write_json(args.request_dump, result)
                else: print(json.dumps(result, indent=2))
        elif args.command == "clean-lyrics":
            result = run_codex_review(
                args.lyrics, args.output, codex_bin=args.codex_bin,
                model=args.model, timeout=args.timeout, dry_run=args.dry_run,
            )
            if args.dry_run:
                if args.request_dump: atomic_write_json(args.request_dump, result)
                else: print(json.dumps(result, indent=2))
        elif args.command == "import-mimo":
            atomic_write_json(args.output, import_mimo_export(args.export, args.audio, args.duration))
        elif args.command == "assist":
            if args.new_process_group:
                _enter_process_group()
            result = run_assist(
                args.audio, args.output_dir, audio_duration=args.duration, mode=args.mode,
                bridge_path=args.bridge, whisper_bin=args.whisper_bin,
                whisper_model=args.whisper_model, codex_bin=args.codex_bin,
                codex_model=args.codex_model, semantic_cache=args.semantic_cache,
                zdr=args.zdr, external_timeout=args.timeout, dry_run=args.dry_run,
            )
            if args.dry_run:
                if args.request_dump: atomic_write_json(args.request_dump, result)
                else: print(json.dumps(result, indent=2))
        else:
            plan = build_scene_plan(
                args.measured, lyrics_path=args.lyrics, semantic_path=args.semantic,
                minimum_section=args.minimum_section, maximum_section=args.maximum_section,
            )
            atomic_write_json(args.output, plan)
            if args.bridge:
                lyrics = read_json(args.lyrics) if args.lyrics else None
                semantic = _semantic_document(read_json(args.semantic)) if args.semantic else None
                atomic_write_text(args.bridge, build_bridge(plan, lyrics=lyrics, semantic=semantic))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"External analysis failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
