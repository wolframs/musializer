# Manual end-to-end tests

Tests in this directory are run deliberately by a human and are **excluded
from every automated suite** (`./nob test ...` and
`python3 -m unittest discover -s tests/adapters`). They exist for the
questions unit tests cannot answer honestly: does the real external
pipeline, with the real binaries and a real model call, still produce a
result the real application accepts, applies, and persists?

Run them explicitly:

```sh
./nob build debug          # sanitize binaries false-fail under Xvfb (LSan)
python3 -m unittest discover -s tests/e2e -v
```

## test_lyrics_assist_e2e.py

Verifies the Lyrics Assist chain end to end: synthesized spoken audio with
a known script → `tools/external_analysis.py assist --mode lyrics` using
the exact argv the in-app Assist button spawns (measured analysis →
whisper.cpp → Codex lyric review → bridge TSV) → bridge grammar and
transcription-recall checks → the built application applying the bridge
via `--analysis-bridge` (the same code path as the UI Apply button,
including SHA-256 identity and duration verification) → assertions on the
persisted `.musi` lyric cues and `lyric_timing` provenance lane → a second
open/save round-trip proving the result is editor-supported.

Prerequisites — each missing one produces an explicit skip, so read the
verbose output rather than assuming a green run verified anything:

- `build/musializer` (debug or release profile);
- `ffmpeg` built with the `flite` filter, plus `ffprobe`;
- `xvfb-run` and a working GL software rasterizer;
- whisper.cpp: `MUSIALIZER_WHISPER_BIN`/`MUSIALIZER_WHISPER_MODEL`, or the
  default `/tmp/music-visualizations-whisper-1.8.6` install;
- an authenticated `codex` CLI on `PATH`.

Cost and privacy: every run invokes Whisper and one Codex review live (a
fresh temporary workspace defeats the assist caches on purpose), so it
takes minutes and sends the synthesized — never private — transcript to
the Codex backend. This, plus the model dependency and network access, is
why it must never join the deterministic suites or CI.
