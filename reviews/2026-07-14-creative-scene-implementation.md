# Creative scene implementation tracker — 2026-07-14

This turns `2026-07-14-creative-scene-ideas.md` from broad ideation into a
bounded product plan. The ordering favors Musializer's unique inputs (timed
lyrics and semantic mood), deterministic export, and visible improvement per
unit of implementation risk.

Status: `[ ]` pending, `[-]` deliberately deferred, `[x]` complete.

## P0 — prerequisite signal quality

- [x] Populate `Scene_Audio_Frame.beat_phase` with a deterministic continuous
  phase instead of the current permanent zero.
- [x] Reset safely after seeks/discontinuities and keep preview/export behavior
  finite and bounded.
- [x] Add headless tests for onset learning, phase progression, discontinuity,
  silence, and invalid input.

## P1 — high-payoff upgrades to the existing seven

- [x] Pulse Field: replace literal circles with an audio-reactive rose/
  kaleidoscope silhouette while retaining its readable concentric rhythm.
- [x] ASCII Field: provide a procedural live-spectrogram glyph field when no
  image is loaded and blend rolling spectral density into imported images.
- [x] Spectrum: use additive light accumulation and onset/flux punctuation so
  trails read as light rather than translucent paint.
- [x] Orbital Lattice: replace rigid struts with swaying segmented curves and
  give nodes layered directional/rim shading.
- [x] Song Atlas: compute slope-derived terrain lighting so ridges catch a
  moving semantic/audio sun and valleys remain legible.
- [x] Spectral Terrarium: replace independent creature orbits with bounded
  separation/alignment/cohesion flocking driven by spectral centroid.
- [x] Constellation: add additive flare layers and depth-weighted halos without
  changing deterministic geometry.

## P2 — signature scenes

- [x] Cadence: add lyric words as primary kinetic geometry, with estimated
  per-word timing, character swarms, onset focus, and beat anticipation.
- [x] Loom: add a growing semantic tapestry whose density, color temperature,
  and interlace are structural consequences of energy, valence, and tension.
- [x] Give both scenes bounded tunable controls, preset/cue persistence, CLI
  names, shortcuts, analysis mappings, and preview/export parity.
- [x] Keep ordinary caption overlay out of Cadence so lyrics are not duplicated.

## P3 — integration and evidence

- [x] Update project limits/schema and preserve compatibility with seven-scene
  projects and older setting snapshots.
- [x] Update build source lists, CLI help, README, product-readiness notes, and
  architecture roadmap.
- [x] Add structural/product tests for every registry and mapping surface.
- [x] Run debug/release/sanitizer C tests and the complete Python suite.
- [x] Build debug/release/sanitize/hotreload and distribution profiles.
- [x] Render short real-media previews for every materially changed scene and
  inspect frame count, codec/pixel format/color metadata, and representative
  frames.

## Deliberate follow-ups, not bundled into this pass

- [-] A global bloom/vignette/grain shader. It needs a two-target compositor
  lifecycle across live preview, CLI export, hot reload, resizing, and 8K
  supersampling; scene-local additive treatments provide safer evidence first.
- [-] Persistent GPU feedback buffers. They require deterministic reconstruction
  after arbitrary seeks before they can satisfy project replay guarantees.
- [-] Scene-transition morphing/luma keys. This is a separate scene-switch state
  model and should not be coupled to introducing two new scene identities.
- [-] True word-level lyric timestamps. Cadence deliberately validates the
  proportional line-timing prototype before requiring a project-format
  migration and Whisper/Codex contract change.
- [-] Verbarium, Cartograph, Tidal, Climate, Chladni, and warp-field scenes.
  Cadence and Loom are the two signature prototypes selected by the review;
  the remaining concepts stay as an evidence-backed backlog.
