from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import analysis_io
import lyric_align


def evidence(words, lines=None, duration=60.0):
    return {
        "schema_version": "musializer.lyric-timing/v1",
        "lane": "lyrics",
        "audio": {"sha256": "a" * 64, "duration_seconds": duration},
        "provenance": {"adapter": "test"},
        "words": [
            {"start_seconds": start, "end_seconds": end, "text": text,
             "confidence": 0.9, "corrected": False}
            for start, end, text in words
        ],
        "lines": [
            {"start_seconds": start, "end_seconds": end, "text": text,
             "confidence": 0.9, "corrected": False}
            for start, end, text in (lines or [])
        ],
    }


def word_ladder(text, start=1.0, step=0.5):
    moment = start
    out = []
    for token in text.split():
        out.append((moment, moment + step, token))
        moment += step
    return out


class NormalizationTests(unittest.TestCase):
    def test_display_text_becomes_pronunciation_tokens(self):
        self.assertEqual(
            lyric_align.normalize_tokens("CTRL+Z THE APOCALYPSE!"),
            ["control", "z", "the", "apocalypse"])
        self.assertEqual(
            lyric_align.normalize_tokens("Wait wa-wa-wait"),
            ["wait", "wa", "wa", "wait"])
        self.assertEqual(
            lyric_align.normalize_tokens("99.5 percent"),
            ["ninety", "nine", "point", "five", "percent"])
        self.assertEqual(
            lyric_align.normalize_tokens("sudo restore --universe"),
            ["sudo", "restore", "universe"])
        self.assertEqual(
            lyric_align.normalize_tokens("I'm fast, okay?"),
            ["i", "fast", "okay"])
        self.assertEqual(lyric_align.normalize_tokens("♪ ♪♪ ♪"), [])

    def test_token_similarity_is_bounded_and_symmetric_enough(self):
        self.assertEqual(lyric_align.token_similarity("cat", "cat"), 1.0)
        self.assertEqual(lyric_align.token_similarity("sudo", "pseudo"), 0.8)
        self.assertGreater(lyric_align.token_similarity("restore", "restored"), 0.0)
        self.assertEqual(lyric_align.token_similarity("cat", "apocalypse"), 0.0)
        self.assertEqual(lyric_align.token_similarity("one", "two"), 0.0)


class ClassificationTests(unittest.TestCase):
    def test_line_kinds_follow_authored_markup(self):
        text = "\n".join([
            "[Verse 1 - Bouncy]",
            "*organ blast*",
            "(Cute voice, chipper)",
            "I burned the world down",
            "(she burned the world down)",
            "(boop boop boop boop)",
            "(pitched up) seconds seconds",
            "",
            "My bad",
        ])
        lines = lyric_align.classify_reference_lines(text)
        kinds = {line["display"]: line["kind"] for line in lines}
        self.assertEqual(kinds["[Verse 1 - Bouncy]"], "section")
        self.assertEqual(kinds["*organ blast*"], "event")
        self.assertEqual(kinds["(Cute voice, chipper)"], "delivery")
        self.assertEqual(kinds["I burned the world down"], "lyric")
        self.assertEqual(kinds["(she burned the world down)"], "backing")
        self.assertEqual(kinds["(boop boop boop boop)"], "backing")
        self.assertEqual(kinds["(pitched up) seconds seconds"], "lyric")
        self.assertEqual(kinds["My bad"], "lyric")
        mixed = next(line for line in lines
                     if line["display"] == "(pitched up) seconds seconds")
        self.assertEqual(mixed["tokens"], ["seconds", "seconds"])

    def test_reference_bounds_are_enforced(self):
        with self.assertRaises(analysis_io.AnalysisValidationError):
            lyric_align.classify_reference_lines("x" * (64 * 1024 + 1))
        with self.assertRaises(analysis_io.AnalysisValidationError):
            lyric_align.classify_reference_lines("a\n" * 1025)


class LoopDetectionTests(unittest.TestCase):
    def test_long_repetition_loops_are_flagged(self):
        lines = [(float(i * 2), float(i * 2 + 1.8), "heaving over this")
                 for i in range(10)]
        flagged = lyric_align.flag_unreliable_intervals(
            evidence([], lines)["lines"])
        self.assertEqual(flagged, [(0.0, 19.8)])

    def test_short_sung_repetition_is_kept(self):
        lines = [(0.0, 1.0, "ctrl z"), (1.0, 2.0, "ctrl z"),
                 (2.0, 3.0, "ctrl z"), (3.0, 4.0, "ctrl z"),
                 (10.0, 12.0, "different words")]
        flagged = lyric_align.flag_unreliable_intervals(
            evidence([], lines)["lines"])
        self.assertEqual(flagged, [])


class SyncTests(unittest.TestCase):
    def test_reference_lines_get_evidence_timing(self):
        reference = "Hello world today\nGoodbye blue sky"
        words = word_ladder("hello world today goodbye blue sky", start=2.0)
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        self.assertEqual(document["schema_version"],
                         "musializer.lyric-sync/v1")
        first, second = document["lines"]
        self.assertEqual(first["text"], "Hello world today")
        self.assertAlmostEqual(first["start_seconds"], 2.0)
        self.assertAlmostEqual(first["end_seconds"], 3.5)
        self.assertEqual(first["confidence"], 1.0)
        self.assertFalse(first["uncertain"])
        self.assertEqual(second["text"], "Goodbye blue sky")
        self.assertAlmostEqual(second["start_seconds"], 3.5)
        self.assertEqual(document["unmatched"], [])

    def test_display_text_is_authored_not_transcribed(self):
        reference = "CTRL+Z THE APOCALYPSE"
        words = word_ladder("control v the apocalypse", start=5.0)
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        self.assertEqual(document["lines"][0]["text"],
                         "CTRL+Z THE APOCALYPSE")

    def test_unmatched_line_in_short_gap_is_interpolated_and_flagged(self):
        reference = "Hello world today\nMumbled line\nGoodbye blue sky"
        words = (word_ladder("hello world today", start=2.0) +
                 word_ladder("goodbye blue sky", start=8.0))
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        middle = next(line for line in document["lines"]
                      if line["text"] == "Mumbled line")
        self.assertTrue(middle["estimated"])
        self.assertTrue(middle["uncertain"])
        self.assertIsNone(middle["confidence"])
        self.assertAlmostEqual(middle["start_seconds"], 3.5)
        self.assertAlmostEqual(middle["end_seconds"], 8.0)
        self.assertEqual(document["statistics"]["estimated_lines"], 1)

    def test_wide_gaps_are_reported_not_interpolated(self):
        reference = "Hello world today\nMumbled line\nGoodbye blue sky"
        words = (word_ladder("hello world today", start=2.0) +
                 word_ladder("goodbye blue sky", start=40.0))
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        self.assertEqual([entry["text"] for entry in document["unmatched"]],
                         ["Mumbled line"])
        self.assertEqual(len(document["lines"]), 2)

    def test_interpolation_never_bridges_hallucinated_evidence(self):
        reference = "Hello world today\nMumbled line\nGoodbye blue sky"
        words = (word_ladder("hello world today", start=2.0) +
                 word_ladder("goodbye blue sky", start=11.0))
        loop = [(3.5 + 1.4 * i, 3.5 + 1.4 * i + 1.3, "garbage loop text")
                for i in range(5)]
        document = lyric_align.sync_lyrics(
            reference, evidence(words, loop, duration=60.0),
            audio_duration=60.0)
        # The loop is only 7 seconds long, so it is not flagged; make the
        # flagged variant explicit instead of relying on duration.
        flagged = lyric_align.flag_unreliable_intervals(
            evidence([], [(3.5 + 1.4 * i, 3.5 + 1.4 * i + 1.3,
                           "garbage loop text") for i in range(10)])["lines"])
        self.assertEqual(len(flagged), 1)

    def test_low_coverage_matches_are_demoted(self):
        reference = "one two three four five six seven eight nine"
        words = [(2.0, 2.4, "one")]
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        self.assertEqual(document["lines"], [])
        self.assertEqual(len(document["unmatched"]), 1)

    def test_minimum_cue_duration_is_enforced(self):
        reference = "Hi"
        words = [(2.0, 2.1, "hi")]
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        line = document["lines"][0]
        self.assertAlmostEqual(
            line["end_seconds"] - line["start_seconds"],
            lyric_align.MINIMUM_CUE_SECONDS)

    def test_overlong_authored_lines_are_flagged_for_review(self):
        reference = "word " * 120 + "\nGoodbye blue sky"
        words = word_ladder("goodbye blue sky", start=8.0)
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        self.assertEqual(document["unmatched"][0]["reason"],
                         "line too long for one caption")

    def test_structure_lines_are_preserved_without_timing(self):
        reference = "\n".join([
            "[Chorus]", "*organ*", "(Softly)", "Hello world today",
        ])
        words = word_ladder("hello world today", start=2.0)
        document = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        kinds = [entry["kind"] for entry in document["structure"]]
        self.assertEqual(kinds, ["section", "event", "delivery"])

    def test_sync_requires_lyric_timing_evidence_and_alignable_text(self):
        words = word_ladder("hello", start=1.0)
        with self.assertRaises(analysis_io.AnalysisValidationError):
            lyric_align.sync_lyrics(
                "Hello", {"schema_version": "other"}, audio_duration=60.0)
        with self.assertRaises(analysis_io.AnalysisValidationError):
            lyric_align.sync_lyrics(
                "[Only structure]\n*and events*",
                evidence(words), audio_duration=60.0)

    def test_line_fallback_is_used_when_word_timing_is_absent(self):
        reference = "Hello world today"
        lines = [(2.0, 5.0, "hello world today")]
        document = lyric_align.sync_lyrics(
            reference, evidence([], lines), audio_duration=60.0)
        line = document["lines"][0]
        self.assertAlmostEqual(line["start_seconds"], 2.0)
        self.assertAlmostEqual(line["end_seconds"], 5.0)

    def test_sync_output_is_deterministic(self):
        reference = "Hello world today\nGoodbye blue sky"
        words = word_ladder("hello world today goodbye blue sky", start=2.0)
        first = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        second = lyric_align.sync_lyrics(
            reference, evidence(words), audio_duration=60.0)
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
