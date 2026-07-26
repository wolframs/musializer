# Musializer Extension Plan and Implementation Tracker

> **Canonical project document.** Keep architectural decisions, milestone
> status, validation results, and cross-session handoffs here. Do not create a
> competing roadmap. Update the checklists and session log as implementation
> proceeds.

## Current status

- **Last updated:** 2026-07-18, Europe/Berlin.
- **Active milestone:** M3 render-product hardening, reusable visual layers,
  and decomposition of the application composition root.
- **Next vertical slice:** finish the dependency-ordered `plug.c` split around
  project workflow, render control, and Assist, then proceed to a layered render
  graph with reusable post-processing and bounded asynchronous
  readback/encoding. The `.musi` v1 workspace, extracted lyric editor,
  assistance staging, full-track UI export, and deterministic windowed CLI
  export are implemented.
- **Reactivity routing:** audio-to-parameter routes (the format's dynamic
  `Musi_Parameter_Mapping` capability) are implemented at runtime for all ten
  scenes, authorable via repeatable `--route` CLI arguments and visually in
  the Tune inspector (per-row `~` affordance, inline draft editor with live
  source meter, Apply/Discard with save/context/render/close guards), and
  persisted in `.musi` beside slider constants with byte-identical reopened
  renders. Toggle routes resolve canonically at their midpoint, flat output
  ranges remain ordinary slider constants, and CLI routes are applied after
  project loading regardless of argument order. Linux-only runtime validation
  so far. Deferred follow-ons (temporal shaping, smoothed sources, cues UI,
  expression routes) are listed in
  `.hermes/plans/2026-07-17_150000-reactivity-routing-layer.md`.
- **Baseline source:** upstream commit
  `4d7d2fa849ef66e94ce03a53a2e7aa3e36aa2392` on `master`.
- **Remotes:** `origin` is the private Forgejo repository,
  `github` is `wolframs/musializer`, and public project provenance lives at
  `upstream` (`tsoding/musializer`). Normal pushes default to private
  Forgejo; do not push feature work to `upstream` unintentionally.
- **Build status:** Linux release, debug, sanitizer, hot-reload, distribution,
  launcher, doctor, and real FFmpeg render checks pass on this machine.
  The 2026-07-18 routing-hardening pass reran 200/200 C tests across debug,
  release, and ASan/UBSan plus 83/83 Python adapter/product tests. Generated
  artifacts remain ignored under `build/`.
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
| D-012 | Store bounded built-in scene controls as canonical constant `.musi` mappings while continuing to reject arbitrary automation mappings. | Accepted | Presets round-trip through the existing v1 contract without pretending the editor supports general parameter automation. |

## Validation baseline

Current machine:

- Ubuntu/Kubuntu 26.04 LTS.
- GCC/`cc` 15.2.0 and binutils 2.46.
- Vendored raylib 5.5 with installed X11 and OpenGL development headers.
- FFmpeg 8.0.1.
- Stock `nob` bootstrap and full application build: **passed**.
- Release, debug, sanitizer, and runtime hot-reload builds: **passed**.
- Headless C tests: **154/154 passed** in the latest integrated debug, release,
  and ASan+UBSan runs; a final release matrix is repeated at each checkpoint.
- Offline Python/product tests: **56/56 passed**; HTTP remains mocked and the
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
  live parameter tuning, timeline/event editing, staged assistance, lyric
  synchronization, and export.
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
- [x] Add bounded per-scene parameters, a responsive live inspector, canonical
  constant preset mappings, asset provenance, and deterministic seeds.
- [ ] Add scene stacks, time-varying mappings, parameter automation/cues, and
  transitions.
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
- [ ] Add an explicit bounded render range to the project and export UI. The
  deterministic `--render-window START DURATION` CLI surface is implemented;
  persistence and interactive range authoring remain.
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

### M4 - Wild prototypes (in progress)

- [x] Whole-Song Atlas prototype. A bounded whole-track time/frequency map is
  prepared from decoded PCM and rendered as batched 3D terrain in preview and
  export, with deterministic map/failure tests and a live-input fallback.
  Persistent analysis caching, saved camera parameters, multi-resolution mesh
  detail, and free exploration remain follow-up product work.
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

## UI remediation backlog (2026-07-25 audit)

A headless capture loop (`tools/UI_REVIEW.md`, `--ui-probe`) plus a multi-agent
audit produced 34 candidate findings; 11 were killed by adversarial review and 23
survived. Everything below is *verified mechanism* with citations, not a wish
list. Landed items are struck from the ranking and recorded in the session log.

**Landed.** Track identity (SHA-256 shown as a track name), per-button
shrink-to-fit typography, timeline tick-label contrast, command-line flags
silently rewriting the opened project, a collapsed tracks panel stealing scene
clicks, the lyric editor drawing its action row past the bottom of the window,
item 4 ("+ Feel" honesty), the silent Auto-scenes disable, and item 3's
`scene_switch` editing API.

**Item 3 landed as API + tests only (2026-07-26).** `scene_switch_remove`,
`scene_switch_retime` and `scene_switch_retarget` exist with 10 headless tests.
No UI calls them yet -- that is gate D4, still unanswered. Two things learned
while building it that the original entry got wrong:

- `scene_switch_reset` does **not** delete cues; it only rewinds
  `active_index`. The earlier note that picking a base scene "destroys the plan"
  was wrong. It disables the plan, which is correct behaviour (a running plan
  would override the base scene at every cued moment), and the cues survive.
  What was actually broken was that it happened silently and autosaved.
- The retarget trap is **not fully closable**. `scene_settings_snapshot_valid`
  rejects a carry between differently shaped scenes, but scenes 0, 1, 5, 6 and 9
  all expose 8 controls, and Spectral Terrarium's defaults validate as
  Spectrum's. A stale snapshot passes and is reinterpreted control for control.
  `scene_switch_retarget` therefore takes NULL as the safe default and the
  caller must capture from the *target* scene.
  `tests/test_scene_switch.c:scene_switch_retarget_cannot_catch_a_same_shaped_carry`
  pins this limit; if it ever fails, the control tables diverged and that is
  good news.

**Item 5 landed (2026-07-26).** `src/timeline_layout.c` places the control row,
the clear button and the timecode as one band with the parent extent computed
from the children, plus 7 headless tests including a 1 px sweep from 680 to
4000 px. The row scales down to share the strip and, past the readability
floor, the timecode relocates to the transport row (where the shortcut hint now
yields the space) instead of printing over the buttons. Verified by capture:
`panel-tune-min` at 960x640 with the inspector open went from the timecode
overprinting "+ Custom" and "Clear manual" to a clean row, while `workspace-min`
is pixel-identical -- the fix does nothing where there was no problem. Note the
original estimate of "~1125 px workspace width" was loose; the real threshold is
`content - timecode - 12 px < 628 px`, and the narrowest supported band is 680 px
(a 960 px window with the inspector open).

### Remaining, safe to implement

| # | Item | Mechanism | Notes |
| --- | --- | --- | --- |
| 6 | Invert sidebar elasticity | The empty track list is the only elastic region; the scene grid and timeline are hard-capped. | Extend `workspace_sidebar_layout` so tracks are content-fit and the surplus raises the scene-browser cap. The 292 cap achieves nothing while the 38 px row cap at `plug.c:5209-5210` stands -- raise both or neither. Do not route surplus into the timeline: `lane_height` is pinned to 58 whenever a panel is open. |
| 7 | Tune inspector: collapse the empty PRESETS block | 214 px of chrome precedes the first slider; the empty block costs 98 px for one live control. | Skip the placeholder and the three disabled buttons when `preset_count == 0`. `tests/adapters/test_scene_quality.py:79/82/134` pin literal source strings -- preserve them or move the assertions in the same diff. |
| 8 | Caption geometry, resolution independence | Caption size is `min(42*ps, max(20*ps, h*0.047))` (`plug.c:1072`) where `pixel_scale` is only the supersample factor, so the 42 px cap binds above 893 px and the same cue is typeset at 4.7% of frame height at 720p and 1.944% at 2160p. | Prerequisite for D1. Verify 720p output is byte-identical first as a canary. Keep `border = 1.0f * pixel_scale`. |
| 9 | Lyric text field: caret, selection, paste | `lyrics_editor_ui.c:164-180` is the whole implementation -- backspace, escape, append. `GetClipboardText` appears nowhere in `src/`. | Reset the caret at all three `draft_text` writers or a stale index becomes an insert offset past `strlen`. Paste must be refused whole when over-long, never cut mid-sequence: `validate_text` rejects truncated UTF-8. Bump `PLUG_STATE_VERSION`. |
| 10 | Lyric direct manipulation in the lane | The lane only selects and the scrubber steals the press; the finest adjustment is a 0.1 s nudge. | Claim `active_button_id` on press and **release unconditionally on mouse-up** -- `ui_widgets.c:147-156` only frees an id through the owning widget, so an unreleased claim freezes every button in the app. |
| 11 | Cadence timing | `hold = cadence_smooth((1-p)*9)` crosses 0.5 at `p = 17/18` (`scene_cadence.c:446`); `active && hold > 0.5f` at `:460` feeds the non-active arm a `focus < 0.5`, yielding 0, gated out at `:381`. Reachable when a ~12-word line ends in a short word. | **State the symptom precisely**: `wants_particles` is `focus < 0.985` with alpha 0.88, so the word still renders as a particle cloud -- it is never *legible type*, not never drawn. Changes exported pixels; needs a real render plus `ffprobe`. |

### Decision gates -- do not start without an answer

| ID | Question | Recommendation |
| --- | --- | --- |
| D1 | `.musi` caption typography (face, size, colour, box, anchor). Root required set is `mask & 0xff` (`project_io.c:215`) so an optional root member parses with no migration. Needs schema, codec, validation, fixtures, `musi_project_editor_support` rejection and compatibility notes together. Cadence bypasses the shared overlay entirely (`plug.c:1143`) and typesets at `height*0.20*scale`. | Ship item 8 first; take this as its own session. Sub-questions: is rejection by older builds acceptable and documented in `packaging/PRODUCT_READINESS.md`; bundled faces only or content-addressed fonts in `<stem>.assets/` with SHA-256 verify-before-use; does `CAPTION_LAYOUT_MAX_LINES 3` become runtime-selectable (it sizes `lines[]` and is baked into the documented ellipsis contract). |
| D2 | 960x640 collapse policy. At 208 px against a 215 px scene-browser floor, something must disappear. | Current shipped behaviour hides the track list first and the tracks panel last. Surfacing Open/Add/Save from the toolbar in the hidden state is unimplemented. |
| D4 | Scene-plan editing UI shape. The engine side is done: `scene_switch_remove`/`retime`/`retarget` are implemented and tested, so this gate is now purely about presentation. | A dedicated row under the waveform, not hit-testing the 1-3 px markers. Item 10's lane drag wants the same press -- the two must agree on `active_button_id` ownership before either is written. Whatever the shape, retarget must capture a fresh snapshot from the target scene or pass NULL; reusing the outgoing cue's snapshot is silently wrong between two 8-control scenes. |
| D5 | "+ Feel" scene coverage. Widening it changes exported pixels in every scene it touches. | 3-4 scenes where a brief transient accent is defensible, documented, rather than wiring all ten into noise. `scene_constellation.c:104` uses `fabsf(event->values[0])`, so the existing 1.0f payload keeps today's flare strength. |
| D6 | Tune `row_height` 76 -> 64 and an interactive inspector scrollbar. | Both real wins, both the most bug-prone edits in the backlog. `row_height` couples to a bare `- 35.0f` at `plug.c:4432` six hundred lines away with no headless test. The scrollbar handler must be hoisted **above** the row loop or `scene_setting_slider` claims `active_button_id` first and drives a value to max. |

### Gaps a completeness critic found that no finding covered

- ~~**Picking a scene silently discards the scene plan.**~~ **Fixed
  2026-07-26, and the original wording overstated it.** `track_select_base_scene`
  disables the plan but does not discard it: `scene_switch_reset` only rewinds
  `active_index`. Disabling is correct -- a running plan overrides the base
  scene at every cued moment, so the click would otherwise look inert. The
  defect was that it was silent while autosave committed it 1.5 s later. The
  notice now names the consequence and says the cues are kept.
- **The landing screen was never reviewed.** Three of the reference captures are
  landing states and no finding mentions them; at 1080p content occupies the
  left ~66% x top ~50%.
- **No keyboard operability at all.** No `KEY_TAB`, no focus index, no focus ring
  anywhere in `src/`; every control is mouse-only.
- **Lyric script coverage.** `caption_layout.c:12-30` loads Latin/Greek/Cyrillic
  only. `.musi` will validate, persist and export a Japanese or Arabic cue that
  renders as missing glyphs, with no warning. Per-codepoint drawing means no
  shaping or bidi even with a bundled face. This is the part of the typography
  complaint that blocks non-Latin users.
- **Microphone capture and the export panel** are unopened subsystems: no
  finding, no screenshot, no plan line.
- **No undo for new destructive operations.** Item 3 adds persisted mutations
  that autosave in 1.5 s; the only undo in the app is one bulk snapshot for
  "Clear manual".
- **The fixture is not a real project.** One track, eight cues, forty seconds, no
  presets. The row-count arithmetic above is derived from that shape; a 200-cue,
  multi-track, preset-populated project changes the character of the lyrics list,
  the scene-cue lane and the tracks list.
- **Panel exclusivity is never questioned.** Only one bottom panel can be open at
  a time, so each is paid for out of the stage. That structural choice is
  plausibly the root of the display-space complaint.
- `CHANGELOG.txt` is still upstream tsoding's and has never been touched by this
  fork.

### Sequencing constraints

Items 6 and any further panel-height work share one vertical budget with
`workspace_sidebar_layout` and `lyric_editor_panel_height`; change them together.
Items 3 and 10 both want the press inside the 22 px lane. Item 8 is a
prerequisite for D1. Each new engine `.c` must appear in **both** lists in
`src_build/nob_stage2.c`; test files are globbed and need no registration.

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
- At that milestone, project audio references were destination-aware: verified
  descendants were normalized relative to `.musi`, with canonical absolute
  fallback, and populated ASCII grids blocked save. The later portable-bundle
  milestone below supersedes both limitations. The Linux installer registers
  `.musi` with shared MIME info and packages its desktop integration assets.
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

### 2026-07-14 - Crackle-resistant preview streaming

- Increased raylib music decode-ahead from its roughly 33 ms default half-buffer
  to 8,192 frames (about 170 ms at 48 kHz), giving durable autosave and analysis
  completion work bounded main-thread breathing room without changing the
  audio-device period.
- Preview streams are primed before playback and serviced before potentially
  slow UI/job polling as well as afterward. Whole-track SHA-256 identity is now
  calculated during initial load, rather than first autosave during playback.
- Added product regression checks for buffer configuration order, minimum
  refill headroom, early stream service, and pre-playback priming. The remaining
  long-term improvement is moving all project serialization and large media
  preparation off the UI thread rather than relying solely on decode-ahead.

### 2026-07-14 - Assist product-state and responsive-layout pass

- Reworked all four Assist workflows around one explicit selection and launch
  confirmation model. Local/remote boundaries, real workflow stages, active
  track, elapsed time, hard timeout, cancellation, immutable failure log, and
  lane-specific replacement counts now remain visible through the lifecycle.
- Added disabled mode states with actionable reasons, a supported-small-window
  panel height policy, a two-column narrow fallback, clipping containment, and
  a compact fullscreen handoff status. The longest staged-result view now fits
  the 960x640 minimum window instead of extending below the timeline surface.
- Made interactive cancellation non-blocking and polled, retained bounded
  process-tree cleanup for unload/shutdown, recovered impossible active states,
  and prevented assisted lyric replacement from clearing an authored draft.
  Apply and Discard return the state machine to idle after a staged result is
  resolved.
- Added a dependency-light Assist policy module with headless tests for all
  mode boundaries, start guards, responsive layouts, and lyric-draft conflict
  protection, plus product-source wiring coverage.

### 2026-07-14 - Experimental scene quality pass

- Rebuilt ASCII image conversion around aspect-preserving bounds, robust tonal
  normalization, alpha-aware source color, coherent edge detail, and a richer
  glyph ramp. The scene now behaves as a stable image plane with restrained
  audio response and supersampling-aware CRT accents instead of resizing and
  scattering individual characters.
- Replaced Orbital Lattice's absolute-time audio phase multiplication with
  integrated, damped motion state. Separate attack/release envelopes and
  bounded camera, node, ring, and convoy behavior keep it responsive without
  late-track phase jumps or disorienting whole-scene jitter.
- Turned Song Atlas into a bounded whole-track time/frequency terrain prepared
  from decoded PCM, with normalized dynamics, onset landmarks, and a moving
  playhead. Its live-input fallback scrolls continuously at renderer cadence;
  both paths batch the surface and contours rather than issuing hundreds of
  small 3D primitive calls.
- Added deterministic ASCII glyph cycling and restrained two-axis wave motion,
  while keeping coherent edges and empty image regions stable. Wider dark/light
  CRT bands remain visible after aggressive web-video recompression.
- Added focused tests for ASCII layout/conversion, frame-rate-independent
  orbital motion, whole-track atlas mapping, and the scene performance wiring.
- Integrated validation passed 148/148 C tests in debug, release, and
  ASan/UBSan profiles; 52/52 Python adapter/product tests; all four Linux app
  build profiles; the portable distribution build; and real 640x360 High
  renders for ASCII Field, Orbital Lattice, and Song Atlas. Each render produced
  exactly 144 H.264 High/yuv420p frames and a six-second AAC stream.
- A final lifecycle audit added a true job-wide Assist deadline, race-free
  Windows Job Object containment/cancellation, reload/shutdown ownership
  retention, and the complete Assist support tree in the macOS bundle. Loading
  an additional track now intentionally pauses and resumes active preview while
  synchronous decode/hash/atlas preparation runs, avoiding buffer underrun
  crackle during that bounded operation.

### 2026-07-14 - Live scene-parameter inspector

- Added bounded, descriptor-driven controls for all seven scenes. Spectrum,
  ASCII Field, Spectral Terrarium, and Constellation expose three or four
  focused controls; Pulse Field and Orbital Lattice expose five. Song Atlas
  exposes ten controls for terrain, camera, contours, hue, sampling detail,
  motion, and Filled/Wireframe surface style.
- Added a right-side inspector with exact numeric readouts, drag sliders,
  per-scene Reset, and dirty-state/autosave integration. **Tune** fits inside
  the current window; a visible **Expand** action can add 340 logical pixels
  when the monitor has room. The responsive layout compacts the track rail
  before the preview falls below 30% of the window.
- Kept preview and export on one parameterized renderer path. Motion remains
  sample-clock deterministic, integer-like settings are quantized at the UI
  boundary, invalid/nonfinite values fall back to documented defaults, and
  every setting has a bounded range.
- Persisted presets through canonical constant v1 parameter mappings. Older
  zero-mapping projects reopen with defaults; unknown, dynamic, duplicate, or
  out-of-range settings remain rejected rather than silently normalized.
- Expanded Song Atlas terrain height to 2.75, terrain width to 3.20, and camera
  height to 0.25-1.75. Hue shift affects the background, terrain, landmarks,
  and contour palette; camera speed changes only deterministic camera travel,
  preserving the audio-synchronized whole-song playhead. Wireframe mode skips
  filled triangles and submits the complete row/frequency grid in batched line
  passes for both decoded-track and live-input paths.
- Validation passed the warning-clean debug application build, 154/154 C tests
  in release and ASan+UBSan profiles, all eight focused scene-quality/UI
  contract tests, and the complete 56/56 Python adapter/product suite.

### 2026-07-14 - Scene presets and parameterized cues

- Added an eight-slot preset library for every scene. Presets capture a complete
  validated scene-specific snapshot and can be loaded, updated, or deleted from
  the Tune inspector; they persist in the owning `.musi` project.
- Replaced the misleading generic **+ Cue** action with **+ Scene**. A scene cue
  now splits or replaces the contiguous scene plan at the playhead and captures
  the selected scene plus its current tuning values atomically.
- Preview, seeking, generated scene plans, project reload, and offline export
  now resolve cue-owned tuning snapshots through the shared renderer. Editing
  controls while an automatic cue is active updates that cue instead of
  silently changing the track-wide defaults.
- Removed the accidental Constellation selection performed by generic timeline
  events, retained deterministic hard cuts at scene boundaries, and made every
  scene cue label eligible for display in the timeline lane.
- Validation passed 158/158 C tests in debug, release, and ASan/UBSan profiles;
  the complete 57-test Python product suite; all four application build
  profiles; and the portable distribution build. A six-second 640x360 High
  render produced 144 H.264 High/yuv420p BT.709 frames plus a six-second AAC
  stream. Reopening its saved parameterized scene plan reproduced every decoded
  video frame byte-for-byte by `framemd5`.

### 2026-07-14 - Song Atlas sampling detail and hue motion

- Tripled the bounded whole-song time analysis from 192 to 576 slices while
  preserving the original physical terrain scale and approximate smoothing
  time constants. The new integer **Sampling detail** setting renders 192,
  384, or all 576 rows at 1x, 2x, and 3x respectively; it does not multiply the
  fixed 28 frequency bands, so the highest setting is a true 3x geometry load.
- Added an optional **Hue motion** toggle beside the manual hue setting. Music
  mode deterministically combines the current whole-song energy and spectral
  flux with the render clock, keeping preview, seeking, cue playback, and
  offline export repeatable without claiming an unavailable BPM estimate.
- Both settings participate in scene defaults, numbered presets, scene-cue
  snapshots, project validation, and offline export. Legacy eight-setting Song
  Atlas snapshots remain valid and acquire 1x detail plus manual hue defaults.
- Validation passed 160/160 C tests in debug, release, and ASan+UBSan profiles;
  the complete 57-test Python product suite; all four application build
  profiles; and the portable distribution build with its updated project
  schema. Matched 1x, 3x, and 3x music-hue renders each produced 144 H.264
  High/yuv420p BT.709 frames plus six-second AAC audio. Repeating the
  music-reactive render with the same release binary reproduced every decoded
  video frame byte-for-byte by `framemd5`.

### 2026-07-14 - Portable asset bundles and loose-end hardening

- Project Save and Save As now import audio and optional ASCII source imagery
  into content-addressed sibling directories under
  `<project-stem>.assets/{audio,images}`. Stored references are strict relative
  descendants with SHA-256 identity; matching immutable objects are reused,
  conflicts and symlink escapes fail closed, and the `.musi` file is published
  only after every required object is present.
- Reopening an imported project verifies both assets before changing editor
  state, rebuilds the deterministic ASCII grid, and rejects changed dimensions.
  Original v1 projects without `ascii_image` continue to open; legacy
  referenced-audio resolution remains available only for those references.
- Made transport capability explicit for tracker formats that raylib cannot
  seek reliably. Timeline drags now pause once and commit a single seek on
  release; the seek transaction invalidates decoder buffers, refills them, and
  resets FFT and scene clocks before playback resumes.
- Made accepted Assist content and its provenance one staged operation. The UI
  no longer mutates lyrics/scenes/semantics if its evidence file cannot be
  hashed or represented, and project publication reports parent-directory
  durability failures truthfully.
- Split Assist cache validity by measured, Whisper, Codex, and MiMo stage. A
  sections-only run cannot consume cached MiMo semantics, while a changed model,
  prompt, routing, or analyzer fingerprint invalidates only the affected stage
  and its downstream evidence.
- Rebased Constellation motion deterministically across seeks and made live Song
  Atlas detail increase sample density without stretching the terrain's
  physical depth. Whole-track Atlas analysis is now lazy in preview, pauses and
  refills active playback around its one-time preparation, and reuses the
  already-decoded canonical PCM during export instead of adding startup work to
  every imported track.
- Validation passed 164/164 C tests independently in debug, release, and
  ASan/UBSan profiles; the complete 65-test Python product suite; all four
  application build profiles; and the portable distribution build and archive
  allowlist inspection. A six-second compressed-audio ASCII project was saved,
  moved with only its sibling bundle, and reopened into a second 640x360/24 fps
  H.264 High/yuv420p BT.709 plus AAC render. Its 146 decoded video frames (the
  MP3 includes decoder padding) matched the pre-move render byte-for-byte by
  `framemd5`.

### 2026-07-14 - Creative scene mechanisms and signature scenes

- Populated the previously inert continuous beat-phase scene input with a
  bounded onset-interval tracker. It uses a deterministic neutral clock before
  learning, folds octave-equivalent intervals, and resets after seek/transport
  discontinuities.
- Reworked the seven established scenes at their visual-mechanism layer: rose
  symmetry for Pulse Field, procedural spectral history for ASCII Field,
  additive light for Spectrum/Constellation, flexible Orbital links with
  faceted nodes, slope lighting for Song Atlas, and spectral-centroid flocking
  for the Terrarium creatures.
- Added Cadence as scene eight. It derives approximate word windows from each
  line cue, draws individual Unicode codepoints as deterministic assembling
  geometry, and uses beat phase/onsets for anticipation and focus. The ordinary
  caption overlay is suppressed only for Cadence to avoid duplicating lyrics.
- Added Loom as scene nine. It samples the accepted semantic event lane over
  the complete track duration and turns energy, tension, valence, and confidence
  into a left-to-right woven structure; measured audio supplies a local fallback
  when no semantic lane is present.
- Added seven bounded parameters to each signature scene and integrated both
  with presets, parameterized scene cues, strict project mappings, CLI names,
  keys 8/9, assisted-scene schemas, and all platform source lists. The complete
  nine-scene setting set uses 59 of the v1 format's 64 canonical mapping slots,
  so the project schema does not require a capacity increase.
- Deliberately deferred global post-processing, persistent feedback buffers,
  transition morphing, and true word-level project timing until their seek,
  compositor, and migration contracts can be addressed independently.
- Validation passed 170/170 C tests independently in debug, release, and
  ASan/UBSan profiles; the complete 69-test Python product suite; all four
  application profiles; and the portable distribution build and archive
  allowlist inspection. Nine six-second 640x360/24 fps scene renders each
  produced exactly 144 H.264 High/yuv420p BT.709 frames. Repeat Cadence and
  Loom exports had matching decoded-frame hashes, and representative frames
  from all nine scenes were visually inspected together.

### 2026-07-16 - Visual corrections and composition-root decomposition

- Corrected three review-driven scene problems without weakening deterministic
  playback/export parity: Spectrum's sliced additive glow, Orbital Lattice's
  over-damped motion, and Song Atlas's ineffective camera-speed control. The
  latter two now expose bounded renderer-backed motion and camera controls.
- Began the dependency-ordered `plug.c` split. Shared theme/widget primitives,
  notice and text helpers, internal `Track`/`Tracks` state, and the complete
  lyrics editor moved into explicit modules whose mutable state remains embedded
  in `Plug` for hot-reload handoff. `scene_stable_name` moved to the scene
  registry instead of remaining UI-owned. `plug.c` is now 6,479 lines, down
  from the 7,148-line baseline; project workflow, render control, Assist,
  transport, and scene-settings extraction remain.
- Pinned the supported Codex structured-output schema subset and bounded child
  diagnostics so lyric-review failures remain inspectable without allowing
  unbounded or misplaced subprocess output.
- Reorientation validation on 2026-07-17 passed 175/175 debug C tests and the
  complete 79/79 Python adapter/product suite, including the real FFmpeg
  full-versus-windowed render smoke.

### 2026-07-16 - Pentagram Orbits: the Lyness map as scene ten

- Added Pentagram Orbits, a phase-portrait scene built on the Lyness
  recurrence `y[k+1] = (1 + y[k])/y[k-1]` (Zamolodchikov periodicity of the
  A2 Y-system: every positive orbit closes after exactly five steps). Scene
  init traces level curves of the conserved quantity
  `K = (x+1)(y+1)(x+y+1)/(x*y)` in log coordinates, where ln K is convex
  with its unique minimum at the golden-ratio fixed point, so ray bisection
  from the center is guaranteed to find each curve. Each orbit seeds one
  phase point on a level curve and lets the recurrence place the remaining
  four stations.
- All motion phase is a pure function of seed and time (no integrated
  audio-dependent state), so seeking and offline export are deterministic;
  audio contributes only additive per-frame offsets, band-lit nest curves,
  and beat-phase pulse pops. Eight bounded parameters (speed, nest curves,
  orbit count, spark glow, chord brightness, hue, music coupling, field
  scale) follow the descriptor/preset/mapping conventions.
- A same-day musicality pass made the geometry itself spectral: every level
  curve and orbit station flexes radially by the smoothed band energy
  sampled at its angle (a seam-free mirrored angle-to-band map with linear
  interpolation), each beat launches a Gaussian brightness ripple that
  travels outward through the nest on beat phase alone, sparks lunge along
  their chords on the beat, and the whole nest breathes with RMS. All of it
  remains a pure function of the current frame; repeat renders stayed
  hash-identical and the mean inter-frame luma delta rose from 0.055 to
  0.138 on the beat-heavy validation clip.
- Registration required the tenth scene slot everywhere at once: Scene_Id
  and Analysis_Scene enums (appended, order-synced), scene registry,
  settings tables, `SCENE_SETTINGS_SCENE_COUNT` 9 -> 10 with derived
  mapping/preset capacities (project schema maxItems 108 -> 120 and
  72 -> 80), scene-plan schema enum, external-analysis scene list, CLI help
  and `pentagram` selector, key 0 shortcut, and platform source lists.

### 2026-07-17 - Windowed CLI export

- Added `--render-window START DURATION`: the export loop fast-forwards
  analysis, beat tracking, scene updates, and cue switching through every
  frame before the window (240 frames per UI tick, no drawing or encoding),
  then draws and encodes only the window. Windowed frames are therefore the
  same deterministic frames a full export produces for that span; FFmpeg
  receives exactly the frame-enclosing decoded-audio slice, and the `-t` cap
  uses the window frame count.
- The frame mapping lives in `render_export_window_frames`: the start floors
  to its containing frame and the absolute requested end rounds up to its
  containing boundary, so fractional starts cannot omit the requested tail.
  The end clamps to the timeline, and non-finite or out-of-range windows fail
  with a dedicated error before any staging file is created.
  Unit tests pin the boundary cases; a product smoke test renders a full and
  a windowed export of the same synthesized audio and requires matching
  frame counts, an exact audio duration, and a minimum PSNR of 35 dB against
  the full export's span (separate H.264 encodes cannot be compared
  byte-for-byte).
- Validated on a real track: windowed renders are hash-identical across
  runs, and a stateful scene (Constellation) shows flat ~44 dB PSNR against
  the full export with no drift across the window, confirming fast-forward
  state parity. UI-side export remains full-track; the window is a CLI
  surface for section previews and automation.

### 2026-07-17 - Review-driven correctness and diagnostics hardening

- Made a validated Assist run with zero authorized lanes a first-class empty
  outcome. It is no longer staged or applyable, preserves all editor content,
  explains the selected workflow's result, and keeps Copy result, Copy log, and
  Copy folder actions visible across terminal job states. Successful job logs
  now contain privacy-safe lane counts, with matching counts in the manifest.
- Replaced production `capture_output` child execution with continuously
  drained 16 KiB stdout/stderr tails and process-group cleanup after timeout or
  direct-child exit, bounding diagnostic memory, preventing leaked descendants,
  and preserving private failure detail only in the per-job artifact directory.
- Removed full-song hashing from metadata autosave by referencing only bundle
  objects already verified and published by the current process. Explicit Save
  and Save As retain full hash verification and collision checks.
- Reset the callback-fed sample ring while the preview stream is stopped during
  seeks, restoring the SPSC reset invariant before playback resumes.
- Changed fractional render windows to enclose both requested boundaries; the
  end is now `ceil((start + duration) * fps)` rather than a duration added to a
  floored start, preventing the requested tail frame from being omitted.
- Validation passed 176/176 C tests in debug, release, and ASan/UBSan; 82/82
  Python adapter/product tests; debug, release, sanitizer, and hot-reload
  application builds; and a real 24 fps fractional-window render whose 13
  frames matched frames 26-38 of the full stateful-scene export.

### 2026-07-18 - Reactivity-routing review hardening

- Replaced representative-only route tests with a descriptor-driven regression
  that applies a live audio route to every setting of every scene. This pins
  continuous and toggle behavior at the common scene-settings boundary, so a
  newly added setting automatically joins the same coverage.
- Quantized routed toggles at their descriptor midpoint before calling the
  strict settings setter, and made route application transactional: an invalid
  write cannot leak a partially modified effective-settings snapshot.
- Rejected equal route output endpoints in both the engine and editor. The
  canonical full-range RMS case is already the editor's slider-constant
  representation, and every flat mapping is non-reactive; the UI now explains
  why Apply is disabled.
- Deferred repeatable CLI routes until all project/audio arguments have been
  loaded, making `--route` placement relative to `--project` deterministic. A
  real save/reopen product test compares both argument orders and their exact
  persisted mappings.
- Made dirty route drafts participate in Saved status, explicit Save/Save As,
  project/track/scene changes, autosave, rendering, and quit guards. Automatic
  scene switching pauses while the active track's route editor is open.
- Validation passed 200/200 C tests in debug, release, and ASan/UBSan and 83/83
  Python adapter/product tests. Debug, release, sanitizer, and hot-reload
  application builds pass.

### 2026-07-19 - Anchor-based route editor and shared preset library

- Rebuilt the Tune route editor around two (source level -> output value)
  anchor rows with source-aware names (Quiet/Loud, Calm/Busy, Beat start/Beat
  end), a live in->out readout, and a transfer-curve graph whose every sample
  goes through the frame loop's own `scene_route_output_value`, extracted from
  `scene_routes_apply` with a bit-for-bit agreement test. The UI font atlas
  gained the arrow codepoint range routed-row summaries always needed.
- Added `--mute` for silent scripted launches; render smoke tests and the
  manual lyrics-assist E2E suite (`tests/e2e/`, deliberately outside all
  automated runners) pass it.
- Moved numbered tuning presets from per-track project data into a per-user
  shared library: strict-JSON `presets.json` under the platform data
  directory (`MUSIALIZER_PRESET_STORE` override), encoded by the project
  codec's own writer/parser, written atomically after every mutation, and
  held read-only when an existing file cannot be accepted. Old projects keep
  their track-local presets byte-stable and copy them into the shared
  library on open (identity: scene plus exact values). Track preset editing
  UI now binds to the shared library; `.musi` round-trip of legacy presets
  is unchanged.

### 2026-07-19 - Lyrics production workflow: sync known lyrics, fix transcription

- Diagnosed the trick-track failure end-to-end (job 8fbbd8ccda3bd2d1): the
  MP3's own `lyrics-eng` tag was ignored, whisper `medium.en` looped on a
  hallucination for the final 81 seconds, and the review contract could
  merge but never split, so 131 evidence lines became 27 paragraph cues
  ending 90 seconds early.
- Assist lyrics mode now discovers authored lyrics (explicit `--lyrics-file`,
  sibling `<stem>.lyrics.txt`, then embedded lyric tags via local ffprobe)
  and, when found, replaces the Codex review with `tools/lyric_align.py`: a
  dependency-free deterministic monotonic alignment of authored lines
  against Whisper word timing (`lyric_sync` lane, `lyrics.sync.json`,
  `schemas/lyric-sync-v1.schema.json`). Line classification separates
  lyric/backing/section/event/delivery; repetition loops are excluded by a
  repeats-plus-duration detector; unmatched lines interpolate only across
  short trusted gaps (flagged) or are reported. On the fixture: 104 authored
  cues timed from existing medium.en evidence, 19 reported unmatched, zero
  model requests.
- Transcription fallback contract v2: chronological citation reuse permits
  splitting, display bounds (200 chars/15 s hard, far tighter targets in
  prompt), mandatory whole-track coverage accounting persisted in the review
  document, hallucination intervals annotated in the request, and a
  deterministic post-splitter snapping to word gaps.
- Adapter hygiene: valid whisper.cpp `--dtw` preset names (large models
  previously failed outright), one thread per host CPU, and
  best-available-model discovery (`large-v3-turbo` first, `medium.en` last;
  `MUSIALIZER_WHISPER_MODEL` still wins). Measured on the fixture through
  the full sync pipeline: turbo timed 122/123 authored lines in 4.6 CPU
  minutes with no hallucination; full large-v3 timed 97 in 32.5 minutes
  (it suppressed loud ensemble passages); the old medium.en default timed
  104 but looped on a hallucination for the final 81 seconds.
- Explicitly deferred: Demucs stems, external forced aligners, online lyric
  lookup, word-level timing in the C model, CUDA whisper rebuild (the /tmp
  whisper.cpp build is CPU-only; a toolkit install is the user's call).

### 2026-07-25 / 2026-07-26 - headless UI review loop and the defects it found

- Built a non-disruptive UI review workflow: `--ui-probe` sets deterministic
  workspace state, `tools/ui_capture.sh` renders the `tools/ui_states.txt`
  catalogue (32 states) on a private Xvfb display with the operator's Wayland and
  PulseAudio handles removed, each state against a throwaway fixture copy.
  `tools/UI_REVIEW.md` documents the loop and its limits.
- Fixed, each with headless regression coverage: track identity showing a
  SHA-256 asset digest as a track name; per-button shrink-to-fit typography
  (eight rows now share one size, `ui_row_typography.c`); timeline tick labels at
  1.16:1 contrast; command-line flags rewriting the opened `.musi` through
  autosave; a zero-height tracks panel registering hit boxes over the scene grid
  (`workspace_layout.c`); the lyric editor drawing Apply/Discard/Delete past the
  bottom of the framebuffer at every window size (`lyrics_editor_layout.c`).
- Two findings were investigated and **refuted**, and are recorded so they are
  not re-reported: Loom filling part of the stage is an intended reveal
  proportional to elapsed track time; Cadence's final word is temporally
  invisible, not truncated.
- A 116-agent audit with adversarial verification produced the backlog above:
  34 findings examined, 11 killed, 23 survived. Full synthesis and completeness
  critique were session-scoped; everything durable is captured in the backlog
  section.
- Learned, the hard way: a capture that cannot reach a state cannot review it.
  The two worst defects were invisible until `assist=confirm` and `lyric=N`
  existed. And a test that terminates `xvfb-run` orphans the application beneath
  it, which then spins a core headlessly forever.

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
