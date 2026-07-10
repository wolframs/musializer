# Optional analysis adapters

These Python 3 helpers run outside Musializer's C renderer. They never load a
`.env` file. Remote analysis reads `OPENROUTER_API_KEY` exclusively from the
process environment, and only when a request is actually submitted.

## Whisper timing import

```console
python3 tools/import_whisper.py whisper.json track.mp3 lyrics.json \
  --duration 213.7 --model medium.en \
  --corrected-lines corrected-lines.json
```

The importer accepts common Whisper `segments[].words[]`, root-level `words`,
Remotion whisper.cpp `transcription[].offsets` with nested BPE tokens, and
Remotion caption arrays using `startMs`/`endMs`. Nested tokens are aggregated
into words using whisper.cpp's leading-space boundaries. Supplied corrected
caption arrays replace guessed segment text but do not erase independently
timed word evidence. Timestamps and confidences are clamped; empty intervals
outside the audio duration are discarded.

## MiMo semantic interpretation

Inspect the exact request shape without a key or network access:

```console
python3 tools/mimo_openrouter.py track.mp3 --duration 213.7 --dry-run \
  --request-dump request.redacted.json --zdr
```

Submit an explicitly authorized analysis and atomically write its cache:

```console
OPENROUTER_API_KEY=... python3 tools/mimo_openrouter.py \
  track.mp3 track.semantic-cache.json --duration 213.7 --zdr
```

`--provider NAME` may be repeated to set provider order. `--no-fallbacks`
pins routing to that eligible set. `--zdr` asks OpenRouter to restrict routing
to Zero Data Retention endpoints, which can reduce provider availability.

The MiMo cache is one atomic envelope containing:

- the deterministic cache key and credential-free request settings;
- the raw OpenRouter response for audit/re-normalization;
- the validated `musializer.semantic-score/v1` document consumed by projects.

No cache replace occurs until the HTTP response, completion JSON, timing, and
creative fields have all validated. The cache key covers audio SHA-256, exact
model, prompt version, response schema, audio format, and routing/privacy
settings. Prompt/schema hashes and the measured duration ensure a version bump
cannot be accidentally skipped without changing the key. It intentionally does
not contain the credential or base64 audio.

Semantic segments must form a contiguous, ordered, non-overlapping partition
of the complete audio duration. The prompt and request schema ask for this,
the normalizer enforces it with a one-millisecond boundary tolerance, and the
persisted schema records the cross-item rule as
`x-musializer-coverage: contiguous-full-duration`. Incomplete model timelines
are rejected rather than silently promoted to a complete creative score.

## Schemas and tests

JSON Schemas live in `schemas/`. Run the dependency-free offline suite with:

```console
python3 -m unittest discover -s tests/adapters -v
```

All HTTP behavior in the suite uses injected mock transports; tests never call
OpenRouter.
