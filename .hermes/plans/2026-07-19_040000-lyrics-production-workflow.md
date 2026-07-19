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

## Model evaluation on the fixture (2026-07-19, CPU-only whisper build)

Reference: 123 timeable authored lines. "direct" = evidence-matched, "est" =
flagged interpolation, rest reported unmatched. All runs through the shipped
sync pipeline (loop detection active).

| evidence                | words | direct | est | unmatched | hallucination | notes |
| ----------------------- | ----- | ------ | --- | --------- | ------------- | ----- |
| medium.en (4 threads)   | 757   | 83     | 21  | 19        | 81 s loop     | heard the 99.x count-up; missed whispered outro; ~8 min |
| large-v3 (24 threads)   | 410   | 84     | 13  | 26        | none          | heard whispered outro + finale; suppressed count-up; 32.5 min CPU |
| union (per-source clean)| —     | 96     | 10  | 17        | filtered      | med+large union; residual = shouted rave chorus |
| large-v3-turbo (24 thr) | 467   | 102    | 20  | 1         | none          | heard everything incl. the rave chorus; 4.6 min CPU; sole miss: "Oh no" |

Conclusions: large-v3-turbo is the discovery default (shipped) — it beat the
union of the other two models outright on sung material and stays far inside
the assist timeout on CPU. Full large-v3's beam search suppressed loud
ensemble passages, so "bigger" was not better here. A multi-model evidence
union with per-source loop cleaning remains a viable Phase-2 lever, now
lower priority. CUDA rebuild DONE 2026-07-19: whisper.cpp v1.8.6 with
GGML_CUDA (arch 86, nvcc 12.4/g++-13) at ~/.local/share/musializer/whisper.cpp
(`build` symlinks `build-cuda`; models copied out of tmpfs) transcribes the
fixture in 13.5 s versus 4.6 CPU minutes. Discovery prefers the durable
install and picks the best model across installs. Caveat: GPU beam search can
diverge from CPU — one CUDA run looped on "100." for 46 s; the loop detector
flagged the stretch and reported the 7 covered lines as unmatched instead of
guessing, and at 13 s a re-run is cheap.

## Slices

1. Adapter hygiene: valid `--dtw` names, `--threads`, best-available model
   preference (large-v3 > large-v3-turbo > medium.en, env override wins),
   doctor reporting.
2. tools/lyric_align.py + schemas/lyric-sync-v1.schema.json + unit tests.
3. Discovery (ffprobe tags, sibling .lrc/.srt/.lyrics.tsv, --lyrics-file)
   and sync mode wiring with stage cache + lyric_sync provenance.
4. Transcription review contract v2 + deterministic cue splitting.
5. Docs + fixture evaluation (medium.en vs large-v3 vs large-v3-turbo).
