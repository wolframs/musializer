# Musializer Extension Plan and Implementation Tracker

> **Canonical project document.** Keep architectural decisions, milestone
> status, validation results, and cross-session handoffs here. Do not create a
> competing roadmap. Update the checklists and session log as implementation
> proceeds.

## Current status

- **Last updated:** 2026-07-12, Europe/Berlin.
- **Active milestone:** M3 render-product hardening and reusable visual layers.
- **Next vertical slice:** a layered render graph with reusable post-processing,
  followed by bounded asynchronous readback/encoding and a semantic-lane
  inspector. The `.musi` v1 workspace, lyric editor, assistance staging, and
  configurable transactional MP4 export are implemented.
- **Baseline source:** upstream commit
  `4d7d2fa849ef66e94ce03a53a2e7aa3e36aa2392` on `master`.
- **Remotes:** `origin` is the private Forgejo repository,
  `github` is `wolframs/musializer`, and public project provenance lives at
  `upstream` (`tsoding/musializer`). Normal pushes default to private
  Forgejo; do not push feature work to `upstream` unintentionally.
- **Build status:** Linux release, debug, sanitizer, hot-reload, distribution,
  launcher, doctor, and real FFmpeg render checks pass on this machine.
  Generated artifacts remain ignored under `build/`.
- **Tracked planning/security changes:** `.gitignore`, `.env.example`, and this
  document. The real `.env` is intentionally untracked.

### Desktop launcher

The Linux source checkout and unpacked portable distribution ship an
idempotent, per-user XDG launcher installer at
`tools/install-linux-launcher.sh`. A checkout builds the release profile; an
archive uses its packaged executable without build tooling. Both register
Musializer plus `.musi` projects in KDE/GNOME, install the icon, and launch from
the containing directory. The runtime wrapper logs desktop-started sessions and
surfaces startup failures through `kdialog` or `zenity`. This remains directory
backed rather than a native relocatable distro package.

### M2 editing and assisted-analysis slice (implemented 2026-07-12)

- Apply anti-aliasing consistently to interactive preview and deterministic
  offline rendering; do not create a preview-only visual path.
- Add explicit UI jobs for local Whisper timing, a headless Codex cleanup pass,
  measured section/change detection, and MiMo semantic interpretation. Jobs run
  outside realtime/audio callbacks, expose progress/failure, use bounded
  timeouts, and only promote validated sidecars into editor state.
- The Codex pass receives a repository-owned system instruction and structured
  output schema. It may normalize text and repair timing from supplied evidence,
  but must never invent unheard lyrics or blur Whisper evidence with user edits.
- Add a bounded lyrics document with stable IDs, text, start/end timestamps,
  provenance, and deterministic ordering. The UI edits lyric content and sync on
  the shared playback timeline with tabular timing and explicit save/apply.
- Scene-switch suggestions are authored cues derived from measured section
  boundaries. Users can inspect/edit/enable them; model output must not silently
  take control of the render.
- MiMo contributes subjective feeling/semantic lanes and creative suggestions,
  never measured audio facts. Keep raw response, normalized sidecar, provider,
  model, prompt version, and source audio hash available for provenance.

Implementation checkpoint: preview uses 4x MSAA; offline export offers
720p-2160p, 24/30/60 fps, and three quality intents with a deterministic 2x
spatial resolve and validated 1x fallback. Fixed-pixel scene/caption details
scale with the supersample target, so quality changes sampling rather than
composition. The UI owns cancellable
external-analysis jobs with explicit privacy confirmation and Apply/Discard
staging, an opt-in automatic scene-switch timeline, and a bounded lyric
content/timing editor. `.musi` v1 atomically persists lyrics, embedded semantic
events, manual events, scene suggestions, per-track seed, and output settings;
provenance references are not required to reconstruct evaluated lanes. The
first-run workspace, project actions, export configuration/progress, structured
notices, and dependency doctor form the current product shell.

Product hardening now stages Raylib's decoded Wave as the exact PCM input to
FFmpeg, caps output at the deterministic video-frame boundary, and verifies
H.264 High/yuv420p/BT.709 plus AAC in real renders. Multilingual timed captions
use a curated bundled atlas and bounded three-line wrapping. Safe descendant
audio references serialize project-relative, Linux installs a `.musi` MIME
association, and populated ASCII grids require explicit Clear before save.

## Non-negotiable invariants

1. **One deterministic visual path.** Preview and offline export call the same
   scene code with a sample-derived clock. Saved projects must not change when
   wall-clock timing, network conditions, or preview FPS change.
2. **The C renderer stays offline.** Cloud/model calls belong to optional
   analysis helpers. The application consumes cached, versioned results and
   never owns API credentials.
3. **Evidence streams remain distinct.** Measured signal features, corrected
   lyric timing, and model-produced interpretation retain separate provenance.
   A subjective MiMo description must never silently become an audio fact.
4. **Realtime code stays realtime-safe.** Audio callbacks do not allocate,
   block, perform file/network I/O, or race with render-thread state.
5. **Hot reload is version-safe.** Persistent state carries a schema version;
   incompatible state is migrated or recreated rather than blindly cast.
6. **Every generated artifact is reproducible.** Record source hashes, seeds,
   analyzer/model versions, prompt/schema versions, provider metadata when
   relevant, and user corrections.
7. **External failures degrade gracefully.** Missing FFmpeg, rejected audio,
   failed model calls, malformed cached JSON, and shader failures produce
   actionable errors without corrupting the project and have bounded recovery.
   Synchronous encoder back-pressure can still pause repaint/input; that known
   limitation stays visible until asynchronous readback/writing lands.
8. **The legacy visualizer is the parity oracle.** Refactors land behind tests
   and visual checks before its behavior is intentionally changed.

## Critical local references

- Prior Whisper workflow:
  `/home/wolfram/Projects/music-visualizations/scripts/transcribe_lyrics.mjs`
  and `transcribe_lyric_sections.mjs`.
- Corrected lyric example:
  `/home/wolfram/Projects/music-visualizations/src/generated/mind-crafty-kitty.captions.json`.
- MiMo/OpenRouter example export:
  `/home/wolfram/Downloads/OpenRouter Chat Fri Jul 10 2026.json`, SHA-256
  `af0c883c93f4b79530533fa0a35f4f0626c60f1d352288d9d46793af05c21b87`.
- The MiMo example requested `xiaomi/mimo-v2.5`; OpenRouter first received a
  Xiaomi-provider `400`, then succeeded through DeepInfra. Recorded duration:
  209,608 ms; recorded cost: USD 0.0082576. Treat ten minutes as a legitimate
  client timeout, not as an interactive-rendering budget.
- `.env` exists locally with `OPENROUTER_API_KEY`, is ignored, and has mode
  `0600`. Never print, persist, test-fixture, or pass the value on a command
  line. `.env.example` contains only the variable name.
- The earlier Remotion Whisper output identifies that run as CPU/OpenMP. A GPU
  Whisper executable was not discoverable in the restricted shell. The host
  GPU itself is verified outside that sandbox: NVIDIA GeForce RTX 3090, 24 GiB,
  driver 580.159.03, with roughly 21.9 GiB free at the time of inspection.
  Locate the user's intended Whisper environment before implementing its GPU
  adapter.

These absolute paths are workstation references, not runtime dependencies.
Anything required to reopen a Musializer project must be imported, copied, or
referenced explicitly by the project format.

## Decision record

| ID | Decision | Status | Reason |
| --- | --- | --- | --- |
| D-001 | Evolve the program into a deterministic scene engine rather than adding modes directly to `plug.c`. | Accepted | Prevents UI, analysis, rendering, and export from becoming inseparable. |
| D-002 | Preserve the current spectrum renderer as the first registered scene. | Accepted | Provides an observable parity target during extraction. |
| D-003 | Use a bounded, serializable scene recipe; do not generate or execute arbitrary C from an AI response. | Accepted | Keeps projects inspectable, safe, and reproducible. |
| D-004 | Run Whisper and MiMo as out-of-process adapters that produce cached sidecars. | Accepted | Avoids Python/CUDA/network coupling in the C renderer. |
| D-005 | Treat supplied/corrected lyrics as authoritative over transcription guesses. | Accepted | Singing transcription is useful evidence, not ground truth. |
| D-006 | Treat MiMo as a creative interpreter, not a timing or lyric authority. | Accepted | The example was perceptive but also hallucinated content and structure. |
| D-007 | Import generated images as assets; keep generation providers and credentials external. | Accepted | Rendering remains deterministic and provider-agnostic. |
| D-008 | Implement the Whole-Song Atlas before the Terrarium and Constellation Mode. | Accepted | It exercises caching, 3D meshes, cameras, timeline navigation, and export with fewer new failure domains. |
| D-009 | Stage every assisted-analysis result and require Apply or Discard. | Accepted | Model output must remain inspectable and cannot overwrite authored work merely because a worker completed. |
| D-010 | Publish project and video output transactionally. | Accepted | Cancellation, encoder failure, or a concurrent save must preserve the last complete artifact. |
| D-011 | Embed evaluated semantic events in `.musi`; retain sidecars only as provenance. | Accepted | A moved project or later analysis job must not lose the visual meaning already accepted by the user. |

## Validation baseline

Current machine:

- Ubuntu/Kubuntu 26.04 LTS.
- GCC/`cc` 15.2.0 and binutils 2.46.
- Vendored raylib 5.5 with installed X11 and OpenGL development headers.
- FFmpeg 8.0.1.
- Stock `nob` bootstrap and full application build: **passed**.
- Release, debug, sanitizer, and runtime hot-reload builds: **passed**.
- Headless C tests: **119/119 passed** in the latest integrated debug and
  ASan+UBSan runs; a final release matrix is repeated at each checkpoint.
- Offline Python/product tests: **40/40 passed**; HTTP remains mocked and the
  measured lane is entirely local.
- Live demo-track smoke test: window, RTX 3090 OpenGL context, shaders,
  PulseAudio, MP3 loading/playback, analyzer, and legacy scene all initialized
  and rendered successfully. The sanitizer build ran the same path for eight
  seconds without reporting an ASan/UBSan fault.
- A full sanitizer Constellation export reached clean application shutdown with
  no ASan/UBSan access error; LeakSanitizer then reported about 270 KB retained
  inside the system `libdbus`/PulseAudio client stack. Core sanitizer tests use
  `detect_leaks=0` because this environment also runs under ptrace constraints.
- Link check: application resolved only the expected system C/math loader
  dependencies because raylib was linked statically.
- The local stage-2 empty-format warning is fixed. Remaining release warnings,
  if emitted, originate in vendored raylib/`nob.h`; record new first-party
  warnings separately rather than silently expanding this baseline.

Optional tools currently absent: Clang, ImageMagick, and Valgrind. Xvfb is
installed and drives real window/render smoke tests. None of the absent tools
blocks core development; GCC sanitizers remain the first memory-safety tool.

## Direction

Turn Musializer from a single FFT visualization into a small, deterministic
audio-reactive scene engine. A scene should behave identically in the live
preview and in offline FFmpeg rendering, remain hot-reloadable, and be able to
consume generated or local media without putting a cloud API inside the core
application.

The existing visualizer remains the first built-in scene and the compatibility
target while the engine is extracted around it.

## What exists now

- A C-only `nob` build which compiles vendored raylib 5.5.
- File playback and experimental microphone input through raylib/miniaudio.
- A hand-written 8192-sample FFT with logarithmic bands, smoothing, and smear.
- Seven registered deterministic 2D/3D scenes sharing one preview/export path.
- A versioned, resource-safe hot-reloadable `libplug` mode.
- Configurable 720p-2160p, 24/30/60 fps H.264/AAC export with three quality
  intents, decoded-sample-derived frame scheduling, progress/ETA/cancellation,
  and transactional publication.
- A Swiss record-label-style workspace for projects, tracks, scene selection,
  timeline/event editing, staged assistance, lyric synchronization, and export.
- Atomic `.musi` v1 persistence plus a capability-aware product doctor and
  source/archive-backed Linux launcher.

The main constraint is architectural: audio capture, analysis, UI, scene
drawing, application state, and offline export currently meet in
`src/plug.c`. New visual modes should not be added as more branches in that
file.

## Engine spine

Build these pieces before adding several scenes:

1. **Deterministic frame context**
   - Define `AudioFrame`: time, delta, frame index, waveform window, frequency
     bands, RMS/peak, spectral flux/onset, beat phase, and optional stereo data.
   - Feed it from the same sample-clocked analyzer in preview and export. Wall
     clock time must never drive a rendered scene.
   - Replace the audio callback's shared `memmove` buffer with an SPSC ring
     buffer or another explicitly synchronized handoff.

2. **Scene interface and registry**
   - Give scenes `init`, `update`, `draw`, `resize`, `unload`, and parameter
     serialization hooks.
   - Pass a `SceneFrame` containing `AudioFrame`, camera/output dimensions,
     deterministic RNG state, and shared assets.
   - Move today's `fft_render()` into `scene_spectrum.c` without changing its
     output. That parity step proves the interface.
   - Version hot-reloaded state so a changed struct is migrated or safely
     recreated instead of being blindly reused.

3. **Scene stack, transitions, and projects**
   - Allow multiple layers and post-process passes, each with blend mode and
     audio mappings.
   - Add keyframed parameters and cue events on the timeline; later add
     automatic cues from onset/section analysis.
   - Store projects in a small documented `.musi` format. Vendoring a compact
     parser is preferable to creating a runtime package dependency.
   - Copy or explicitly reference imported assets so an offline render is
     reproducible.

4. **Shared render path**
   - Make preview and export call the same `scene_render()` function with
     different render targets.
   - Make width, height, FPS, duration/range, and quality settings data rather
     than compile-time constants.
   - Move GPU readback and FFmpeg writes off the UI/render critical path; a
     double-buffered readback plus bounded writer queue is a suitable first
     design.

5. **Tests and diagnostics**
   - Test the analyzer with generated sine, sweep, impulse, silence, and stereo
     fixtures.
   - Hash a few fixed-resolution frames from deterministic scenes to catch
     render drift.
   - Add analyzer/scene assertions, structured FFmpeg errors, and an optional
     sanitizer build configuration.

## Requested directions

### Procedural scene generation

Start with a deterministic scene grammar rather than AI-generated C. A scene
recipe selects geometry, palette, camera rig, mappings, and transitions from a
seed. Offline whole-track analysis supplies a beat grid, loudness envelope,
section boundaries, and spectral character; the grammar turns those into a
repeatable visual score. A later text-to-scene adapter can generate or edit the
same bounded recipe format.

### Richer 3D

Add a 3D scene foundation with camera rigs, reusable meshes/materials, instanced
drawing, lights, depth-aware post-processing, and audio-mapped transforms.
Begin with one scene made from instanced primitives, then add mesh generation
and GPU feedback passes. Keep scene simulation independent of the preview FPS.

### Image generation to ASCII to visualization

Treat image generation as an external producer. Musializer watches/imports a
PNG, JPEG, or QOI produced by a user-configurable command or service adapter;
the core never owns API credentials. Convert the image into a colored glyph
grid using luminance plus edge/orientation matching, render it from a cached
font atlas, and expose the grid as either a 2D layer or a 3D field of glyph
particles. Cache the source image, conversion settings, and resulting grid in
the project for deterministic video export.

### Semantic soundtrack analysis with MiMo-V2.5

Use Xiaomi MiMo-V2.5 through OpenRouter as an optional networked interpreter of
the music's felt character. It should produce a global creative brief and a
timestamped emotional/visual arc: mood, tension, energy, perceived motion,
texture, imagery, palette suggestions, narrative turns, and candidate scene
cues.

Keep this interpretive stream explicitly separate from measured or
user-supplied evidence:

- FFT, loudness, onset, beat, and structural estimators provide measured audio
  features.
- Whisper plus corrected or supplied lyrics provide timestamped vocal content.
- MiMo provides subjective description and creative hypotheses, never timing
  ground truth or authoritative lyrics.

The provided OpenRouter export demonstrates both sides of this boundary. MiMo
captured the track's breakcore/glitch/chiptune texture, cybernetic playfulness,
visual motion, palette, and existential character unusually well. It also
invented or misheard some lyrics and proposed a structural timeline that did
not cover the full track. Preserve its valuable interpretation without silently
promoting guesses into facts.

Implementation path:

1. Add an optional external analysis helper rather than networking code inside
   the C renderer. It reads `OPENROUTER_API_KEY` from the environment, never
   logs it, and submits a compact stereo MP3 using OpenRouter's `input_audio`
   message content.
2. Request a versioned JSON `SemanticScore` containing a global summary plus
   timestamped segments. Each segment should carry affect labels, continuous
   energy/tension/valence values, motion verbs, textures, imagery, palette,
   scene suggestions, and model-stated confidence.
3. Validate all returned JSON, clamp timestamps to the measured audio duration,
   reject overlaps or gaps when the schema forbids them, and retain the raw
   response beside the normalized result for audit and reinterpretation.
4. Cache by audio hash, exact model/variant, prompt version, request settings,
   and normalized response schema version. Record the actual provider and
   generation metadata when OpenRouter returns them.
5. Use a 600-second request timeout. Retry only appropriate transient failures
   (`429`, `502`, `503`, empty completion), honor `Retry-After`, and avoid blind
   retries that can duplicate billable work.
6. Keep provider routing configurable. Default to fallbacks and require support
   for the requested audio/structured parameters; optionally prefer throughput
   or pin/ignore providers after enough observed runs.
7. Make remote analysis opt-in because audio may be copyrighted, private, or
   unreleased. Offer Zero Data Retention routing when available, while warning
   that it can reduce the eligible provider pool.

Scenes consume the normalized semantic score as slowly changing creative
signals. They do not call MiMo during preview or rendering, so network latency,
provider availability, and model nondeterminism cannot change a saved render.

## Three further wild ideas

### 1. The Spectral Terrarium

A persistent artificial ecosystem whose creatures, plants, and weather emerge
from the music rather than merely scaling to it. Bass acts as gravity pulses,
midrange steers flocking fields, treble seeds sparks/spores, and detected
sections change the ecosystem's rules. The same track and seed always grow the
same world.

Implementation path:

1. Add fixed-step simulation and deterministic RNG to `SceneFrame`.
2. Ship a CPU boids/particle prototype with spatial hashing and instanced
   rendering.
3. Add field emitters mapped to analyzer features and scene cues.
4. Add ping-pong texture simulations for reaction-diffusion/fluid-like effects
   using OpenGL 3.3-compatible fragment passes.
5. Add saveable organism presets and camera behaviors.

### 2. The Whole-Song Atlas

Pre-analyze a track into a navigable 3D place: time is distance, frequency is
terrain shape, harmony is color/material, and beats become architecture. Live
playback flies through the atlas; timeline scrubbing physically moves the
camera; the user can also leave the rails and explore the entire song.

Implementation path:

1. Build an offline multi-resolution analysis cache keyed by audio file hash
   and analyzer version.
2. Generate chunked heightfields/tunnels from band history and section data.
3. Stream mesh chunks around the playhead and use lower-detail distant chunks.
4. Add rail, orbit, and free-fly camera rigs tied to timeline navigation.
5. Let cue markers place generated assets or ASCII monuments in the atlas.

### 3. Constellation Mode

Turn several laptops, projectors, or audience phones into one synchronized
visual organism. A conductor instance broadcasts the transport clock, audio
features, scene seed, and sparse control events; each node renders a different
camera or layer. Audience motion/touches can become bounded scene forces
without making any client authoritative.

Implementation path:

1. Define a versioned, endian-safe UDP packet for clock, analyzer summary,
   scene identity, seed, and cue events.
2. Add clock estimation, jitter buffering, packet-loss tolerance, and a local
   fallback mode.
3. Add conductor/node roles and per-node camera/output assignments.
4. Expose a tiny browser controller through an optional local HTTP/WebSocket
   bridge, keeping networking out of the audio callback and renderer.
5. Record incoming events into the project so a live performance can be
   replayed and rendered offline.

## Live implementation tracker

Checkboxes in this section represent actual repository state. A task is checked
only after its acceptance gate passes. Detailed milestone intent remains in the
next section.

### M0 - Baseline and development foundation (complete locally)

- [x] Verify the stock Linux build on the current machine.
- [x] Record the baseline commit, toolchain, external references, and accepted
  architectural decisions.
- [x] Ignore `.env`, restrict it to mode `0600`, and provide a secret-free
  `.env.example`.
- [x] Create local branch `feature/scene-engine-foundation`.
- [x] Configure private Forgejo `origin`, public GitHub fork `github`, and keep
  the provenance remote named `upstream`.
- [x] Add explicit `debug`, `sanitize`, `hotreload`, and release-equivalent
  build configurations without changing the default release behavior.
- [x] Add a non-copyrighted synthetic audio fixture generator: silence, sine,
  sweep, impulses, stereo imbalance, and a short beat pattern.
- [x] Add a small test executable and `nob test` entry point that does not need
  a window or audio device.
- [x] Complete and document an automated offline-export smoke procedure for the
  legacy scene; live preview has also been visually checked with the external
  demo track.
- [x] Run the baseline under GCC AddressSanitizer and UndefinedBehaviorSanitizer
  and triage findings before structural refactoring.

M0 acceptance gate:

- A fresh checkout can bootstrap, build, and run unit tests with documented
  commands.
- Sanitizer/test configurations do not alter release defaults.
- No credential, copyrighted music fixture, generated video, or local absolute
  path is accidentally tracked as runtime content.

### M1 - Deterministic analyzer and scene kernel (complete for current ABI)

- [x] Extract FFT state and operations into `audio_analyzer.c/.h` with a pure,
  testable API.
- [x] Replace the callback/render-thread shared sliding buffer with a bounded,
  explicitly synchronized sample handoff.
- [x] Test frequency localization, silence, impulse response, smoothing,
  channel handling, buffer wraparound, and invalid/short input.
- [x] Define `AudioFrame`, `SceneFrame`, `Scene`, and the built-in scene
  registry.
- [x] Move `fft_render()` into the registered legacy spectrum scene with visual
  parity.
- [x] Add versioned plug state and resource-safe reload/reinitialization
  behavior. The stable handoff makes old code release callbacks, audio/GPU
  handles, FFmpeg, and scene resources before unload; crossing directly from a
  legacy build predating the handoff still requires one process restart.
- [x] Drive preview and export through the same `SceneFrame` and scene update
  path; final offline-export parity automation remains open.
- [x] Add one deliberately simple second scene to prove registration without
  editing application or export control flow.

M1 acceptance gate:

- Analyzer tests pass under normal, ASan, and UBSan builds.
- Preview and offline rendering consume the same `SceneFrame` contract.
- Legacy scene output passes agreed visual/frame checks.
- Adding a scene requires a scene implementation plus registry entry, not new
  branches in preview/export orchestration.

### M2 - Projects, authored cues, and analysis adapters (core slice complete)

- [x] Specify the versioned `.musi` project and sidecar schemas before choosing
  a parser implementation.
- [ ] Add scene stacks, parameters, mappings, automation, cues, transitions,
  asset provenance, and deterministic seeds.
- [x] Add whole-track measured analysis and a versioned cache.
- [x] Import Whisper word/line timing with confidence and manual corrections,
  including the prior Remotion whisper.cpp/caption formats and an in-app
  content/timing editor with interim bridge import/export.
- [x] Implement the optional MiMo/OpenRouter helper with `input_audio`, a
  600-second timeout, bounded retries, provenance, privacy controls, and raw
  plus normalized outputs.
- [ ] Add a semantic-score editor/inspector that never overwrites measured or
  corrected lyric lanes.
- [x] Add deterministic section/scene recommendations from measured, lyric,
  and semantic lanes; broader parameter/stack recipe generation remains.

M2 acceptance gate:

- Reopening the same project produces matching normalized analysis and scene
  frames without network access.
- A failed/cancelled remote analysis leaves the last valid cache and project
  intact.
- Every visible cue can be traced to its source lane and overridden by the
  user.

### M3 - 3D, render graph, and generated-media import (in progress)

- [ ] Add camera rigs, reusable mesh/material resources, instancing, lighting,
  and fixed-step scene simulation. Orbital Lattice and Song Atlas prove the
  shared 3D viewport/camera path; reusable resource and fixed-step layers remain.
- [ ] Add layered render targets and OpenGL 3.3-compatible post/feedback passes.
- [x] Make export resolution, FPS, and quality durable project/render settings.
- [ ] Add an explicit bounded render range to the project and export UI.
- [ ] Add bounded asynchronous GPU readback/FFmpeg writing. Complete-write,
  cancellation, child cleanup, and error propagation are hardened; readback and
  encoding are still synchronous.
- [ ] Add generated/local image import, glyph analysis, cached ASCII grids, and
  2D/3D ASCII scenes. Local import, deterministic glyph analysis, and the 2D
  audio-reactive ASCII Field are working through CLI, drag-and-drop, and the
  scene rail; cache serialization and 3D remain.

M3 acceptance gate:

- A generated image becomes a reproducible audio-reactive 3D ASCII scene.
- Export and preview share scene/render-graph code.
- Long exports remain responsive to cancellation and propagate FFmpeg errors.

### M4 - Wild prototypes (queued)

- [ ] Whole-Song Atlas. A bounded streaming 3D terrain prototype now works in
  preview/export; measured-cache prefill, saved parameters, and failure tests
  remain before graduation.
- [ ] Spectral Terrarium. A bounded fixed-step ecosystem prototype now works;
  saved parameters, reusable instancing, and failure tests remain before
  graduation.
- [ ] Constellation Mode with recordable/replayable external events. Independent
  1,024-event manual and semantic lanes now persist and merge into a lossless,
  lane-qualified 2,048-event frame view; seek/replay, CLI/UI recording, colored
  markers, and the 3D scene work in preview/export. Live transport adapters and
  broader scene parameters remain before graduation.

Each prototype graduates only after it has a deterministic seed, saveable
parameters, bounded resource use, offline export support, and at least one
failure-path test.

## Session log

### 2026-07-10 - Orientation and roadmap

- Audited the C/`nob`/raylib architecture and the monolithic `plug.c` seam.
- Confirmed the stock project builds successfully on Kubuntu 26.04.
- Defined the deterministic scene-engine direction, requested media/3D work,
  and three wild prototypes.

### 2026-07-10 - Audio intelligence inputs

- Reused lessons from the prior `music-visualizations` Whisper workflow:
  full-track transcription, overlapping prompted section passes, corrected
  lyrics, and deterministic cached captions.
- Inspected the supplied MiMo-V2.5 OpenRouter export and established the
  measured/lyric/interpretive provenance boundary.
- Added the cached semantic-score design, ten-minute timeout policy, routing
  metadata, privacy option, and retry constraints.
- Secured the local OpenRouter credential file without reading its value.

### 2026-07-10 - Tracking initialized

- Promoted this document into the canonical plan and implementation tracker.
- Recorded invariants, local references, accepted decisions, validation
  baseline, prioritized checklists, and milestone acceptance gates.
- Next implementation session starts with the remaining M0 development/test
  foundation, then proceeds directly into the analyzer/scene-kernel slice.

### 2026-07-10 - Foundation implementation wave

- Added four build profiles, a headless C test harness, synthetic in-memory
  audio fixtures, and a 31-test sanitizer-clean core suite.
- Extracted the legacy analyzer, replaced callback buffer shifting with a
  bounded C11-atomic SPSC ring, and introduced a hot-reload-safe scene registry.
- Registered Spectrum, Pulse Field, Orbital Lattice, ASCII Field, Song Atlas,
  and Spectral Terrarium; preview and offline export now call the same scene
  contract.
- Added the versioned project C model and JSON Schemas, Whisper import, MiMo
  semantic adapter, and deterministic measured-audio analysis with 1/4/16-second
  summaries. The adapter suite now has 17 offline tests.
- Hardened POSIX and Windows FFmpeg process transport, including complete
  writes, interruption handling, safe Windows argument quoting, cleanup,
  cancellation, and child exit propagation.
- Verified real RTX/OpenGL exports for the 3D and ASCII scenes. A 12-second
  untracked demo excerpt produced 282 measured frames and a conservative pulse
  estimate; no source audio or rendered media entered the repository.
- Historical parity behavior at this checkpoint let video continue through the
  FFT smear tail (a two-second fixture yielded about 4.50 seconds). This was
  superseded on 2026-07-12 by decoded-sample-derived frame scheduling and a
  matched deterministic A/V boundary.

### 2026-07-10 - Adversarial foundation audit

- Found and fixed export inheritance of mutable preview scene state: every
  render now recreates the selected scene transactionally from its stable seed.
- Corrected offline frame zero to time zero and replaced truncated per-frame
  sample stepping with a rational sample cursor, eliminating accumulated drift
  when sample rate is not divisible by render FPS.
- Added Wave/sample validation, nonzero CLI failure status, transactional scene
  selection, bounded analyzer stall deltas, and full application shutdown that
  cancels/reaps FFmpeg before releasing audio and OpenGL resources.
- Two independent Terrarium renders of the same fixture were byte-identical
  (`197dc722d46bfcfb93c740686e1a49d633ee9664ae839c3c75671baef3e04942`).
- At this checkpoint M1 remained open for incompatible hot-reload resource
  handoff, repeated-export in-process automation, and broader scene/CLI tests.

### 2026-07-10 - Event replay and reload-safety wave

- Added a fixed 1,024-record event timeline with canonical ordering, validation,
  overflow behavior, revision-safe cursors, deterministic seek/replay, and seven
  focused tests.
- Added Constellation Mode as scene seven. CLI `--event` records lyric,
  semantic, cue, or custom events into the immutable per-frame event view;
  quiet mode does not fabricate recorded evidence.
- Made incompatible hot reload resource-safe through a stable ABI-v1 handoff:
  the old plug releases live code-owned resources before unload, inventories
  opaque allocations, and lets the new plug restore compatible state or safely
  reject/free it. The recovery record preserves track/scene seed and the bounded
  event snapshot; active renders are intentionally cancelled.
- Made named profiles honest across macOS, OpenBSD, MinGW-w64, and MSVC.
  Unsupported sanitizer combinations now fail explicitly; target compilation
  still requires validation on those operating systems/toolchains.
- Visually verified an offline Constellation render with three recorded event
  types. No rendered output or demo audio was added to the repository.
- Verified a compatible real shared-library handoff with `--reload-once`, then
  rendered Constellation successfully; the recorded event highlights survived
  the unload/reload cycle.

### 2026-07-10 - First-class authoring UI

- Split the existing left rail into track and scene libraries with all seven
  scenes selectable by mouse and an unambiguous active state.
- Added an image picker and image drag-and-drop path that imports the glyph grid
  and selects ASCII Field without CLI arguments.
- Added timeline authoring controls for lyric, semantic/feel, cue, and custom
  Constellation events, including colored markers, event count, and clear-all.
- Kept rendering scene-agnostic: the existing film button exports whichever
  scene and event timeline the UI currently holds.

### 2026-07-12 - Anti-aliasing, assisted analysis, and lyric editing

- Added 4x preview MSAA, analytic shader edge coverage, bounded 3D tube
  geometry, and deterministic 2x offline spatial supersampling with a tested
  1x fallback. Preview and export still share scene code.
- Added a cache-aware external-analysis orchestrator with separate measured,
  Whisper, Codex-review, MiMo, and scene-plan provenance; a repository-owned
  evidence-only Codex system instruction; bounded timeouts; credential
  isolation; process-tree cancellation; and a strict derived bridge.
- Added first-class UI panels for timed lyric content/sync editing and assisted
  analysis. Lyric cues support add/select/edit/apply/delete, playhead timing,
  100 ms nudging, and interim import/export. Analysis jobs do not block audio.
- Added opt-in scene switching from measured/semantic section recommendations.
  Preview, seeking, hot reload, and offline export use the same tested timeline
  selection model; recommendations never enable themselves.
- Added SHA-256 identity checks before bridge import, per-track replacement of
  semantic cues, bounded pre-allocation checks, and atomic cross-lane import.
  A wrong-song same-duration bridge is rejected by CLI smoke coverage.
- Validation: 56/56 C tests in debug/release/ASan+UBSan, 29/29 Python adapter
  tests, release/sanitize/hotreload builds, real demo section planning, and a
  validated 1600x900/30 fps bridge-driven render across one hot reload.

### 2026-07-12 - Product workspace, durable projects, and solid export

- Reframed the UI as a near-white Swiss record-label workspace with one Klein
  blue accent, hairline scene grid, deliberate first-run screen, persistent
  project actions, a large timecode/timeline spine, and structured notices.
- Added explicit Apply/Discard staging, truthful local/network labels, privacy
  confirmation, cancellation, elapsed state, and lane-specific authority for
  Whisper/Codex, measured sections, and MiMo assistance. A completed worker no
  longer mutates authored data automatically.
- Added a shared timed-lyric caption layer to preview/export and a separate
  sampled semantic frame. MiMo valence/tension now steers every built-in
  scene's palette/motion while measured audio values remain unmodified.
- Added `.musi` v1 JSON I/O with strict parsing/validation and atomic durable
  writes. Projects restore lyrics, embedded semantic cues, manual cues, scene
  suggestions/opt-in, per-track deterministic seed, base scene, audio identity,
  provenance references, and output intent. Dirty lyric drafts block context
  replacement until Apply or Discard.
- Made manual and semantic event lanes independently bounded and merged them
  into a deterministic 2,048-event frame view with lane-qualified display IDs;
  full lanes and colliding source IDs no longer silently lose semantic cues.
- Added the Export workspace and CLI settings for 720p/1080p/1440p/2160p,
  24/30/60 fps, and Balanced/High/Master. Offline frame scheduling is computed
  from the decoded sample count and publishes at the enclosing frame boundary;
  FFmpeg consumes the same staged PCM used by analysis. H.264/AAC output carries
  High-profile BT.709/yuv420p/fast-start metadata, A/V/container duration
  matched within the MP4 time base, bounded finalization, ETA/cancel feedback,
  and transactional
  publication.
- Added multilingual caption shaping for accented Latin, Greek, and Cyrillic,
  bounded three-line UTF-8 wrapping with visible ellipsis, and supersample-aware
  fixed-pixel composition. A real mixed-script High-quality render verified the
  caption and bundled atlas.
- Made project audio references destination-aware: verified descendants are
  normalized relative to `.musi`, with canonical absolute fallback. Populated
  ASCII grids now block save regardless of the selected scene until **Clear
  image** explicitly discards them. The Linux installer registers `.musi` with
  shared MIME info and packages its desktop integration assets.
- Added a capability-aware product doctor, hardened portable distribution
  allowlists, launcher preflight/escaping tests, cross-target recipe checks,
  and a documented product-readiness matrix. No credential is packaged.
- Added a real conditional product smoke that synthesizes an awkward-duration
  MP3, runs the application under Xvfb, saves a portable project, renders MP4,
  probes exact frame count/codecs/color/A-V time bases, checks source identity,
  and rejects leftover transaction files.
- Remaining release work is explicit: render ranges, asynchronous GPU readback
  and FFmpeg writing/finalization, chunked PCM decode for very long media, a
  reusable render graph/post stack, native non-Linux CI/signing/packaging, and
  a semantic inspector. These are product roadmap items rather than hidden
  correctness debts in the current single-scene MP4 workflow.

### 2026-07-14 - Readable interface type and scaled-window viewport repair

- Replaced raylib's bitmap default throughout the editor with bundled Space
  Grotesk, retaining Alegreya for lyric captions and scene glyph work. The
  Swiss record-label hierarchy remains, but compact controls and instructional
  copy now use a proportional grotesk designed for non-display readability.
- Added the Space Grotesk OFL notice and began packaging both bundled font
  licenses alongside distributions.
- Enabled raylib's HiDPI window mode and backported its post-5.5 framebuffer
  resize callback. GLFW window sizes remain logical UI coordinates while the
  OpenGL viewport follows physical framebuffer pixels, fixing the lower-left,
  half-sized render seen when maximizing under scaled KDE/XWayland.
- Added product regression checks for framebuffer callback ownership, HiDPI
  configuration, the bundled UI font, default-font fallback isolation, and
  packaged license coverage.

### 2026-07-14 - Waveform transport and scene-wide scaled fullscreen

- Turned the lower editor strip into a bounded transport surface: every loaded
  track receives a normalized 2,048-bin waveform preview, timeline clicks can
  be dragged, and exact start, tenth-second, one-second, and ten-second buttons
  complement Ctrl/ordinary/Shift arrow-key seeking.
- Reduced the playhead from a ten-pixel panel-spanning bar to a two-pixel
  hairline clipped to the waveform lane, so it no longer crosses authoring or
  assistance controls.
- Kept waveform reduction and seek/clamp arithmetic independent of raylib and
  added headless boundary tests for stereo envelopes, silence, non-finite PCM,
  exact deltas, invalid geometry, and track-end clamping.
- Completed the scaled-window repair by synchronizing raylib 5.5's internal
  rlgl framebuffer dimensions during physical framebuffer resize. The newer
  scissored and 3D scenes query that state, while Spectrum and Pulse Field draw
  directly in logical 2D coordinates; all seven now share the same resize
  contract.

## Milestones

### M0 - Preserve the baseline

- Work on a fork/feature branch; upstream explicitly asks feature development
  to happen in forks.
- Add a smoke fixture and document the known-good Linux build.
- Add `debug`, `sanitize`, and `hotreload` build configurations without changing
  the release defaults.

Exit condition: the existing visualizer previews and exports as before.

### M1 - Scene kernel

- Extract analyzer, scene interface, legacy spectrum scene, and render context.
- Make preview and offline rendering use the same frame clock and scene call.
- Add scene selection and parameter inspection to the existing UI.

Exit condition: legacy output is at parity and a second trivial scene can be
added without editing application/render-export control flow.

### M2 - Authored and generated scenes

- Add `.musi` projects, scene stack, parameter mappings, automation, cues, and
  transitions.
- Add whole-track analysis and the deterministic recipe generator.
- Add imported Whisper lyric tracks and cached MiMo semantic scores as distinct
  timeline lanes, with provenance and manual correction support.

Exit condition: a saved project can be reopened and render matching frames.

### M3 - 3D and media pipeline

- Add 3D cameras, instancing, generated meshes, render passes, and feedback.
- Add image import, glyph analysis, cached ASCII grids, and ASCII 2D/3D scenes.

Exit condition: a generated image can become a reproducible audio-reactive 3D
ASCII scene in preview and export.

### M4 - Wild prototypes

- Build the Whole-Song Atlas first; it exercises analysis caching, mesh
  generation, cameras, timeline integration, and deterministic export.
- Build the Spectral Terrarium second on the fixed-step/instancing foundation.
- Build Constellation Mode last, once projects and deterministic event replay
  give networking a stable protocol surface.

## Tooling on this Kubuntu 26.04 machine

The normal Linux build already succeeds with the installed GCC 15.2 toolchain,
binutils, X11 development headers, `libgl-dev`, and vendored raylib. FFmpeg 8 is
installed, so video rendering has its external executable too. Clang is not
required.

No installation is needed to continue core development. Optional additions:

- ImageMagick 7 (`imagemagick`) only for the `./nob svg` asset-regeneration
  command; neither `magick` nor the README's legacy `convert` command is
  currently installed, and the build does not use them.
- `xvfb` for automated graphical smoke tests without a desktop session.
- `valgrind` for an additional memory diagnostic; GCC sanitizers can cover the
  first pass without it.
