# Lyrics production workflow (sync-known-lyrics + transcription fixes)

Status: in progress (2026-07-19)

## Diagnosis (verified on `Ctrl+Z The Apocalypse`, job 8fbbd8ccda3bd2d1)

- The MP3 carries the full authored lyrics in its `lyrics-eng` tag (4,746
  chars); the assist pipeline never looks at container metadata.
- Whisper `medium.en` produced 131 lines / 757 DTW word timestamps to 333 s.
  From 248.66 s it collapsed into a repetition-loop hallucination
  ("He-he-heaving over this" x45), so the tail evidence is garbage.
- The Codex review contract cannot split a source line, has no coverage or
  display-size requirements, and allows 2,048-char lines while the C editor
  caps cue text at 512 bytes and captions render at most 3 lines. Result:
  27 cues, up to 166 chars / 16.5 s, ending at 4:08 in a 5:38 track.
- The whisper-cli build at /tmp/music-visualizations-whisper-1.8.6 is
  CPU-only (no CUDA) and the adapter passes neither `--threads` nor a valid
  large-v3 `--dtw` name (`large-v3` vs whisper.cpp's `large.v3`).

## Prototype evidence (scratchpad align_prototype.py)

Deterministic global monotonic alignment (Needleman-Wunsch over normalized
tokens, pronunciation equivalences, zero ML deps) of the embedded lyrics
against the existing medium.en word evidence timed 112/121 timeable authored
lines with plausible monotonic windows; all failures trace to the hallucinated
tail. Conclusion: known-lyrics sync does not need Demucs/stable-ts/WhisperX;
better whisper evidence + deterministic alignment suffices for v1.

## Architecture (improving on the GPT proposal)

Reference text discovery -> line classification (display vs alignment text)
-> whisper word evidence -> deterministic monotonic alignment with
hallucination-loop detection and bounded interpolation -> authored-line cues
-> staged candidate for Apply/Discard. Codex review remains only for the
no-reference transcription fallback, with a fixed contract (splitting allowed,
coverage accounting, display bounds) plus a deterministic post-splitter.

Explicitly deferred: Demucs vocal stems, external forced aligners
(stable-ts/WhisperX/CTC), online lyrics lookup (network boundary), word-level
timing in the C model / bridge (Cadence keeps derived estimates), CUDA rebuild
of whisper.cpp (needs toolkit install decision).

## Slices

1. Adapter hygiene: valid `--dtw` names, `--threads`, best-available model
   preference (large-v3 > large-v3-turbo > medium.en, env override wins),
   doctor reporting.
2. tools/lyric_align.py + schemas/lyric-sync-v1.schema.json + unit tests.
3. Discovery (ffprobe tags, sibling .lrc/.srt/.lyrics.tsv, --lyrics-file)
   and sync mode wiring with stage cache + lyric_sync provenance.
4. Transcription review contract v2 + deterministic cue splitting.
5. Docs + fixture evaluation (medium.en vs large-v3 vs large-v3-turbo).
