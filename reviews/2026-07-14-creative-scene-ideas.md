# Musializer Creative Scene Ideas — 2026-07-14

Pure ideation from four parallel Sonnet 5 sub-agents — no code was written or
modified. Scope: what's imaginable for this specific tool beyond correctness
and tunability (see `reviews/2026-07-14-ux-audit.md` for that pass). The
prompt: AI music visualization is a crowded space online, but no one tool
"does it all," and raylib's own possibilities here aren't close to exhausted.

**Grounding used by all four agents:** every scene receives, per frame
(`src/scene.h`), a `Scene_Audio_Frame` (per-band spectrum + trail history,
`rms`, `peak`, `spectral_flux`, a continuous `beat_phase` 0..1, and an
`onset` boolean), an optional `Semantic_Frame` (AI-derived `energy` /
`tension` / `valence` / `confidence`, not just spectral features), the
current line-level `Lyric_Cue`, and an `Event_Timeline_View`. Most ideas
below are "wire an existing signal into a new visual mechanism," not "add a
new sensor."

## Where this converges

Three findings independently pointed at the same thing: **this tool's
actual differentiator is having timed lyrics, AI-derived mood, and
deterministic offline 4K/8K export all on the same scene interface at
once** — something the fragmented visualizer landscape (browser toys with
no export, pro live-VJ tools with no determinism, generative-video services
that can't reproduce a frame twice) doesn't combine. The strongest concrete
concepts lean into exactly that: lyric text as physical geometry, and
semantic mood as structural shape rather than a color tint.

Cross-referencing the four reports, a rough map of effort vs. payoff:

- **Cheap, high-payoff, scene-local:** Pulse Field → kaleidoscope/rose-curve
  math swap; ASCII Field → procedural glyph-density mode (removes the
  "needs an imported image" limitation); additive blend mode on existing
  bright elements.
- **Cross-cutting infrastructure, benefits everything downstream:** a
  full-screen post-processing shader pass (bloom/vignette/grain/glitch)
  inserted once at the existing `BeginTextureMode(p->screen)` composite
  step in `plug.c`; a feedback/trail render-texture pattern reusable by
  Spectrum, Pulse Field, Constellation.
- **One substantial but contained rewrite:** Song Atlas's terrain is real
  3D geometry (`scene_song_atlas.c`, verified: `rlBegin(RL_TRIANGLES)` with
  per-vertex `rlColor4ub`, no normals) shaded with hand-computed vertex
  colors instead of a real `Mesh` + lighting — the single biggest "unlock"
  among the existing 7, independently flagged by two of the four agents.
- **New scenes worth prototyping:** a lyric-as-geometry kinetic-typography
  scene ("Cadence" below) and a semantic-mood-as-structure scene ("Loom" /
  "Verbarium" below) were named the strongest signature-feature candidates
  — the kind of scene nothing else in this space could replicate, because
  it needs data lanes this tool has and most visualizers don't.

---

## 1. Prior art: techniques worth stealing

Surveyed: Milkdrop/projectM/Butterchurn (feedback-buffer engines), VJ
tooling conventions (TouchDesigner/Resolume/Notch), shader-art/demoscene
technique, and emerging typography/mood-driven directions.

### Classic feedback-buffer engines

**Warp-mesh + per-pixel motion vectors.** The whole frame is the canvas — a
coarse displacement grid pushes/rotates/zooms every pixel of the previous
frame before compositing the new one, so motion accumulates into hypnotic
flow. Milkdrop keys zoom/push off bass energy, rotation off mid-band RMS.
Raylib translation: a render-to-texture pass with a displaced vertex grid
sampling last frame's texture with a UV offset from `bands`/`rms`. **Fit:
new scene** — none of the 7 currently treat the whole frame as a warp
field.

**Video-echo accumulation trails.** Old frames fade rather than clear, so
transients leave decaying streaks. Keys off `peak`/`onset` for trail
opacity. **Fit: upgrade to Pulse Field** — single pulses would layer into
decaying ripple histories instead of resetting each beat.

**Preset morphing on scene-switch.** Milkdrop's defining UX move: presets
dissolve into each other over seconds rather than hard-cutting, sometimes
gated to land on a downbeat. **Fit: upgrade to `scene_switch.c`/`.h`** —
already governs *which* scene is active; a blended-transition mode is a
direct extension of its existing job.

### VJ / live-visual tooling conventions

**Kaleidoscope / radial mirror symmetry.** Cheap in compute, expensive-
looking in result — turns noisy input into an instantly "designed" mandala.
**Fit: upgrade to Orbital Lattice** — its node/link graph is already
radially organized around a center; an N-fold mirror wrap converts a
"graph" read into a "mandala" read for the same underlying data.

**Luma-keyed feedback compositing.** VJs derive a layer's alpha from its
own brightness and blend it over a decaying background layer — depth
without an authored alpha channel. **Fit: alternative transition mode
alongside preset morphing in `scene_switch.c`** — a persistent underlay
scene bleeding through a keyed overlay.

**Bass-triggered datamosh / pixel-sort glitch.** A controlled corruption —
smeared rows/columns reading as a deliberate rhythm hit, decaying back to
clean within a few frames. Keys off `onset`/`spectral_flux` spikes. **Fit:
upgrade to Spectrum** — the most "flat graphic design" of the 7 scenes,
and it already exposes both signals directly; a glitch punctuation on hard
hits directly counters the bar-graph genericness.

### Shader-art, creative coding, demoscene

**Domain-warped raymarched SDF metaball tunnel.** Continuously melting
organic blobs defined by math, never geometrically identical frame to
frame. **Fit: upgrade to Spectral Terrarium** — replacing/supplementing
mesh creatures with a raymarched organic body fits the "living ecosystem"
premise with far less animation work than rigged meshes.

**Reaction-diffusion organic growth.** Turing-pattern textures (spots,
stripes, veining) that visibly *grow*, not just modulate brightness — feels
alive because the simulation has memory. Onset events seed injections,
sustained energy sets diffusion rate. **Fit: upgrade to ASCII Field** —
its glyph grid currently samples a frozen imported image; driving glyph
intensity/character from a live reaction-diffusion field makes the same
machinery audio-reactive at the simulation level.

**Curl-noise flow-field particle advection.** Particles swirl in
divergence-free eddies — reads as smoke/fluid, never clumps. Bass sets
"viscosity," treble sets turbulence detail; CPU-tractable at moderate
counts. **Fit: upgrade to Constellation** — slow curl-noise drift between
event-triggered highlights would add continuous motion without disturbing
the existing event-linking semantics.

**Chladni-plate standing waves / cymatics.** Genuinely underused in
visualizer software: a "sand on a vibrating plate" model where particles
settle into the antinode geometry of whichever frequencies dominate right
now — a physically-motivated, not arbitrary, spectrum-to-shape mapping.
Evaluate a small sum of sine-product basis functions over a grid each
frame as mode coefficients from the top `bands`. **Fit: new scene**, but
can reuse Song Atlas's heightmap/terrain rendering machinery rather than
starting from nothing.

**Demoscene "cheap alive" discipline.** Not one effect but a constraint:
64k intros layer 2–3 cheap secondary motions (camera drift on offset sine
phases, per-object jitter, slow color-temperature breathing off smoothed
RMS) so nothing looks frozen between hits. **Fit: cross-cutting** — cheap
insurance for Orbital Lattice/Constellation/Spectral Terrarium looking
static during quiet passages.

### Typography and mood-driven directions

**Kinetic typography as primary geometry.** Most "lyric video" tools treat
text as an overlay; the more distinctive move is letting the words *be*
the scene — extruded, exploding into particles on line changes,
weight/kerning breathing with amplitude. Strong fit specifically because
`Lyric_Cue` is already first-class timed data, not a bolt-on caption, and
`caption_layout.*` already provides shared preview/export text-layout
logic to build on. **Fit: new scene** — see "Cadence" below.

**Mood/valence-driven procedural grading.** The genuinely proceduralizable
slice of "ML-driven" visualization: use the already-computed
`Semantic_Frame` to continuously bias a scene's palette/framing/particle
aggression, layered under (not replacing) fast per-frame audio reactivity.
**Fit: cross-cutting upgrade to every scene not yet consuming
`Semantic_Frame`** — the data lane already exists and is currently
under-used as a visual driver versus a lyric/event annotation source.

---

## 2. Unused raylib 5.5 capability

Confirmed by reading `thirdparty/raylib-5.5` headers and grepping `src/`:
today the entire project uses exactly **one** custom fragment shader
(`resources/shaders/*/circle.fs`, a soft-circle glow), `RenderTexture2D`
only for final composition and offline export (never a mid-pipeline effect
buffer), `Camera3D`/`BeginMode3D` in 3 scenes with no `LoadModel`,
`GenMesh*`, `Material`, or lighting anywhere, and no `DrawMeshInstanced`,
`BeginBlendMode`, or `DrawBillboard` calls at all.

| Capability | What it enables | API surface | Caveat |
|---|---|---|---|
| Full-screen post shader pass | Bloom, vignette, chromatic aberration, film grain, scanlines applied uniformly to every scene | `LoadRenderTexture`, `BeginTextureMode`, `BeginShaderMode`, `SetShaderValue` | Cheap if uniforms are time/audio-seeded, not wall-clock; keep blur passes at half-res for 8K export |
| Feedback/trail buffer (ping-pong) | Motion trails/afterimages without CPU-side trail arrays | Two `LoadRenderTexture` targets, alpha-blended swap | Must reset deterministically on scene entry/seek, like `scene_song_atlas.c` already does for its own discontinuities |
| Real heightmap `Mesh` + lighting for Song Atlas | Actual specular/diffuse shading, rim light on ridges, audio-driven "sun" | `GenMeshHeightmap`/custom `Mesh` fill + `UploadMesh`, `GenMeshTangents`, lighting shader | Song Atlas is the only scene with real terrain geometry and zero lighting model today — highest-value single upgrade |
| GPU instancing | Thousands of audio-reactive particles/nodes in one draw call | `DrawMeshInstanced(Mesh, Material, Matrix*, count)` | Transforms must derive from `frame_index`/audio, not wall-clock RNG, to stay export-deterministic |
| Procedural noise textures | Domain-warped color fields, cloud/plasma backgrounds, terrain detail | `GenImagePerlinNoise`/`GenImageCellular`, seeded from project `seed` | Generate once at scene init, not per frame |
| Additive/custom blend modes | Overlapping bright elements accumulate light instead of muddying | `BeginBlendMode(BLEND_ADDITIVE)` | Tune at Master export scale — additive blending can blow out highlights under supersampling before downsample |
| Billboarded sprites in 3D | Rich glow/particle look in 3D scenes at near-zero extra draw cost | `DrawBillboard`/`DrawBillboardPro` | None significant |
| SDF font atlas | Crisp resolution-independent type under supersampling | `GenImageFontAtlas` w/ SDF path | Relevant if lyric captions or a title-card scene need geometry-quality type |

**Ranked top 3 — highest leverage for least implementation risk:**
1. Full-screen post shader pass — one new render texture + one `.fs` file,
   inserted once at the existing composite point in `plug.c`, benefits all
   7 scenes without a per-scene rewrite.
2. Additive blend mode on existing bright elements — a two-line wrap, no
   new assets, immediately reads as more premium on beat hits.
3. Song Atlas real `Mesh` + lighting — larger diff but contained to one
   file behind a clean `Scene_Descriptor`; the one scene with unused 3D
   structure sitting right below a flat-shaded surface.

**Scene most deserving a genuine tech upgrade: Song Atlas** — it already
commits to full 3D terrain and a moving camera, more than any other scene,
but throws that investment away by shading with hand-interpolated vertex
colors instead of a lit mesh. Every 2D scene would need a bigger
conceptual leap to "deserve" 3D lighting; the other 3D scenes (Orbital
Lattice, Spectral Terrarium, Constellation) use 3D mostly for points/lines
where lighting has less to show. Song Atlas is the one place a surface
already exists to be lit.

---

## 3. Reimagining the 7 existing scenes

Not more sliders (see the tunability section of the prior UX audit) — a
change to each scene's core visual mechanism.

**Spectrum** (`scene_spectrum.c:30-86`) — currently approximates trails via
same-frame alpha blending between `bands[i]` and a decayed `trails[i]`
value, redrawn as soft-circle quads each frame; no accumulated image
exists. *Reimagined:* a real ping-pong render-texture feedback loop —
sample a scaled/dimmed previous frame before drawing current bars on top,
so fast-rising bars leave genuine comet-tail afterimages that curl and
color-shift as they decay. `spectral_flux` sets decay rate, `onset`
injects a one-frame bloom flash. Compositing-based; moderate effort (new
render-target plumbing).

**Pulse Field** (`scene_pulse_field.c:53-68`) — literal nested circles,
audio only perturbs radius/thickness/sweep. *Reimagined:* swap the radius
function for a parametric rose/kaleidoscope curve —
`radius = extent*(i/rings)*(1 + k*cos(fold*theta))` — where `fold` (petal
count) tracks the bass/treble energy ratio; occasionally cross-fades
toward a spiral on sustained builds. Pure math change to the existing
polar loop, no new render targets or shaders. **Lowest risk, highest
payoff of the seven.**

**Orbital Lattice** (`scene_orbital_lattice.c:207-227`) — flat
`ColorFromHSV` cubes joined by straight tube links, no lighting model at
all. *Reimagined:* real directional shading (Lambertian + rim/fresnel glow
via a small custom shader) so nodes read as lit crystalline gems, plus
quadratic-Bezier links that sag/sway with `flux` instead of rigid straight
struts. Mesh/lighting-based; moderate effort.

**ASCII Field** (`scene_ascii_field.c:22-31, 305-387`) — glyph identity
comes from the imported static image; audio only modulates
activity/wobble/split, never glyph choice, and the scene needs an image to
show anything at all. *Reimagined:* a procedural "live spectrogram" mode —
each column's glyph chosen from a density ramp by rolling per-band energy
history, blended with any imported image's luminance when present. Removes
the "needs an image" dependency. Logic-only change, low risk.

**Song Atlas** (`scene_song_atlas.c:225-294`) — terrain triangles carry
per-vertex color only, no computed normals, so it reads as a flat/wireframe
map. *Reimagined:* compute real per-vertex normals from the heightfield
and add a directional "sun" term — even baked into the existing
vertex-color path without a custom shader — so ridges catch light and
valleys shadow as the terrain scrolls. Height already comes from `bands`,
so slope-lighting reacts to the music implicitly. Mesh/lighting-based;
moderate, and can ship without a new shader.

**Spectral Terrarium** (`scene_spectral_terrarium.c:177-233`) — creatures
move on independent fixed circular orbits with zero interaction between
them. *Reimagined:* real boids flocking (separation/alignment/cohesion
among the ~10 creatures), flock heading steered by dominant spectral band,
altitude/tightness by spectral centroid — trebly passages pull the flock
up and tight like a starling murmuration, bassy passages spread it low and
slow. Particle/instancing-based; O(N²) at N=10 is negligible, but may need
a centroid/pan signal not currently in `Scene_Audio_Frame`.

**Constellation** (`scene_constellation.c:85-240`) — camera orbits a
static sphere field with flat wireframe glow halos, fixed neighbor edges,
straight to the framebuffer with no post-processing. *Reimagined:* a real
bloom pass (threshold-extract bright/event-lit stars, blur at low-res,
additive composite back) so lit stars genuinely flare, plus optional cheap
depth-of-field (a second blurred pass weighted by camera distance) so it
reads as a photographed night sky rather than a flat orbiting shot.
Compositing-based; moderate, no new scene-geometry logic.

**Ranking — biggest visual leap for effort:**
1. Pulse Field (math-only, near-zero risk)
2. ASCII Field (logic-only, also fixes the image dependency)
3. Constellation (bloom/DOF, most "camera-realism" transformative)
4. Spectrum (feedback trails, strong signature, needs new plumbing)
5. Song Atlas (normal-based lighting, can skip a custom shader initially)
6. Orbital Lattice (lighting + bezier links, more shader/tessellation work)
7. Spectral Terrarium (boids are cheap, but visual delta is subtler; may need new upstream data)

---

## 4. Net-new scene concepts

Deliberately not upgrades to the 7 — new scenes leaning into the
underused lanes: lyrics as primary subject, mood as structure, beat-phase
choreography, and whole-track shape as something other than a terrain.

Note on data: `Lyric_Cue` is **line-level only** (`text[512]`,
`start_seconds`, `end_seconds`) — no per-word timestamps exist yet. Any
word-level choreography below estimates word timing by splitting a line's
span proportionally by character count. Legitimate and cheap, but an
approximation worth knowing about up front.

**Cadence** — lyric-as-geometry, beat-choreographed typography. The
current line doesn't caption the screen, it *is* the screen: each word is
a cluster of glyph-shaped particles that swarms in and snaps into legible
formation as its estimated word-window begins, holds in a taut trembling
formation, then disperses back to particles before the next word.
`beat_phase` drives the coil — particles tighten toward the glyph outline
approaching phase 1.0, relax just after wrap — so text visibly breathes
with the beat rather than just appearing; per-character jitter amplitude
maps to a spectrum band hashed by glyph position; `onset` triggers a
sharper snap-into-focus. Particle/instancing-heavy (glyph atlas + GPU
instancing toward target UV positions per letter) — solid fit for raylib,
cheap texture-atlas lookups.

**Verbarium** — whole-track lyric sediment. A vertical core sample of the
entire song: every sung line deposits as a stratified band of colored
text-texture into a growing sediment column, like rock strata made of
words — by the end of a track, a readable geological cross-section of the
transcript. Stratum color/warmth from `semantic.valence`/`.tension`
sampled at that cue's time (cold blue-grey for low-tension passages, hot
amber/red for tense peaks), band thickness from `rms`/`spectral_flux`
captured during that line, blank rock for instrumental gaps. 2D
mesh/texture-heavy (stamped text-texture quads appended to a growing
target — no per-frame regeneration of old strata); main risk is legibility
at scale once many lines accumulate, a layout problem not a rendering one.

**Loom** — semantic arc as a growing woven tapestry. A textile grows
across the screen as the song plays; a bleak low-energy verse weaves
loose, thread-sparse, and cool-toned, and as the semantic arc climbs
toward a peak the weave visibly tightens — thread count, saturation, and
interlace complexity all increase — ending dense and vivid compared to its
threadbare opening. `semantic.energy` sets thread density, `.valence` sets
color temperature, `.tension` sets weave complexity; `beat_phase` drives a
subtle per-column thread lift/settle animation; `spectral_flux`/onset add
glints and snags. The clearest "structure that requires a real story arc"
concept — cloth, not landscape. Best as a shader-driven procedural weave
texture over a growing quad rather than a fully simulated per-thread cloth
mesh (which would be overkill).

**Cartograph** — whole-song route map, not terrain. A branching
path/river-delta grows across the frame as the song advances — a
generative subway map or river system, not a spectrogram. The main
channel is the timeline; it curves, widens, narrows continuously, and
forks at every recorded `CUE` event (section markers), visually marking
verse/chorus/bridge structure the way a river map marks confluences.
Channel width from `rms`, curvature from `spectral_flux`/`tension`, color
from `valence`, fork points sourced directly from `Event_Timeline_View`.
Puts the currently under-used event-timeline lane front and center. 2D
vector/mesh-heavy (extruded ribbon/SDF river shader); needs the whole-track
path capped/decimated in memory for long tracks.

**Tidal** — pure beat-phase choreography, not onset-pulse. A single
luminous ring doesn't just flash on the beat — as `beat_phase` climbs
toward 1 it contracts and sharpens with a faint pre-echo halo gathering
outside it like a held breath, then releases outward in a decaying
shockwave at phase wrap, overshooting slightly before settling. A genuine
attack/sustain/release envelope keyed to the continuous phase, not a
binary trigger — deliberately rejecting "pulse on beat," the most common
cliché in this genre. Spectrum bands distort the ring's silhouette
radially; `semantic.energy` scales the whole cycle's amplitude across the
track. Shader-heavy (SDF ring in a fragment shader, phase-driven
radius/blur uniforms) — cheap and reliable even at 8K supersampling.

**Climate** — semantic mood as cellular-automaton topology. A grid-based
CA (Life-family rules) fills the frame, but its *governing rules*, not
just its palette, retune continuously with the mood arc: sparse
negative-valence passages run a stable, glacial rule (muted, few live
cells); as `energy`/`tension` climb toward a peak the rule shifts toward
chaotic, fast-reproducing dynamics (dense, warm, flickering). Spectrum
bands seed live cells at radial positions per band; `onset` triggers seed
bursts. The one concept here that isn't lyric- or terrain-shaped at all —
mood literally changes a system's behavior, not its coloring. Shader/
compute-heavy (ping-pong render targets running the CA step as a fragment
shader at ~512–1024² before export upsampling); main risk is tuning rule
presets so sparse→chaotic reads as a smooth arc, not an abrupt switch.

**Signature-feature recommendation:** **Cadence** and **Loom** (or its
sibling Verbarium) are the strongest candidates. Cadence is the one
concept a Milkdrop-style or generic WebGL visualizer genuinely cannot
replicate — it requires timed lyric text as a first-class input, and it
uses `beat_phase` correctly (anticipation/release, not pulse-on-onset).
Loom is the strongest demonstration of the other unique lane — AI-derived
mood shaping whole-track structure rather than a tint — and produces an
immediately screenshot/export-worthy artifact (a woven "portrait of the
song"), playing directly to this tool's deterministic 4K/8K export
strength. Together, a lyric-driven kinetic-typography scene plus a
semantic-arc structural scene is the pairing most likely to become "the
thing people associate with this tool," because both need data lanes
(timed lyrics + genuine LLM mood interpretation) that generic
audio-reactive engines structurally don't have.

---

## 5. Implementation guidance — where things go

Orientation pointers for whoever picks one of the above up, not a build
plan. Grouped by the kind of change, since several ideas share an entry
point. All line numbers verified against `feature/scene-engine-foundation`
at time of writing.

### Adding a brand-new scene
(Cadence, Loom, Verbarium, Cartograph, Tidal, Climate, Chladni terrain,
warp-mesh, etc.)

A new scene touches the same bounded set of files every time:

- `src/scene.h` — add the id to the `Scene_Id` enum, before `COUNT_SCENES`.
- `src/scene.c:6-22` — declare the new `extern const Scene_Descriptor` and
  add it to `scene_registry[]`.
- `src/scene_<name>.c` (new file) — implement `init`/`update`/`draw`/
  `unload` matching `Scene_Descriptor` in `scene.h`; any existing
  `scene_*.c` is the reference shape.
- `src/scene_settings_values.h:8` — `SCENE_SETTINGS_SCENE_COUNT` must grow
  by one; every array sized by scene count depends on it.
- `src/scene_settings.c` — add a descriptor table (can start empty) and an
  entry in `tables[]`, even before deciding tunable parameters.
- `src/plug.c:809-821` and `:863-869` — CLI name↔id string mapping
  (`--scene <name>`).
- `src/musializer.c:98` — CLI help text listing scene names.
- `src/analysis_bridge.c:17` — scene-name array consumed by the assisted
  scene-change bridge.
- `src/plug.c:1007-1025` (keyboard shortcuts) and the scene rail
  (`scene_browser`, `plug.c:~4501` onward) — an 8th scene has no spare
  number key (1–7 already used) and needs a slot in the rail's layout.
- `src_build/nob_stage2.c:112-122` (and the sanitizer/test source lists
  nearby) — register the new `.c` file in the centralized build source
  list per CLAUDE.md.
- `tests/test_scene_<name>.c` — headless test, following any existing
  `tests/test_scene_*.c`.
- `README.md`'s "seven built-in scenes" list and CLI scene-selector table
  — becomes eight.

This checklist is identical regardless of which section-4 concept gets
built first — it's the scaffolding, not the idea.

### Shared rendering infrastructure
(post-processing, feedback trails, additive blending — needed by
Spectrum's trails, Constellation's bloom, any warp-mesh/reaction-diffusion
concept from section 1)

- **The one shared draw chokepoint is `scene_render()` at `plug.c:1083`.**
  It's called identically from live preview (`preview_screen()`,
  `plug.c:6187` and `:6235`) and from both export paths
  (`rendering_screen()`'s CLI route at `:6365`, and the frame-capture
  route at `:6514`). A full-screen effect that must apply identically to
  preview and export — CLAUDE.md's preview/export-parity invariant —
  belongs wrapped around this function's output, not duplicated into each
  caller.
- **Shader load/unload pattern to copy:**
  `p->circle = LoadShaderFromMemory(NULL, data)` at `plug.c:6601-6603`,
  paired with `UnloadShader(p->circle)` at `:6621`. New `.fs` files go in
  `resources/shaders/glsl120/` and `resources/shaders/glsl330/` next to
  `circle.fs`, and need adding to `distribution_support_files` in
  `src_build/nob_stage2.c` — an easy packaging step to forget.
- **Existing offscreen render target to reuse or mirror:** `p->screen`
  (`RenderTexture2D`, declared `plug.c:227`, allocated `plug.c:369-379`,
  torn down at `:5525`/`:6400`/`:6724`). A feedback/trail buffer needs a
  *second* persistent `RenderTexture2D` on the same alloc/free lifecycle,
  ping-ponged frame to frame.
- **Per-scene-only effects** (e.g. only Constellation gets bloom) don't
  need the shared chokepoint at all — a scene can own its own
  `RenderTexture2D` inside its private `void *state`, which
  `Scene_Descriptor`'s `init`/`unload` already allocate and free.

### Reimagining an existing scene

Each is contained to its own file — none require touching `scene.h`, the
registry, or the build list:

| Scene → idea | Start here |
|---|---|
| Pulse Field → kaleidoscope | `scene_pulse_field.c` draw loop, ring-radius calc (~L53-68) — pure math swap |
| ASCII Field → procedural glyph mode | glyph selection in `scene_ascii_field.c` (~L22-31 band mapping, ~L305-387 draw); optional toggle in `ascii_settings[]`, `scene_settings.c` |
| Song Atlas → real lighting | `scene_song_atlas.c`, `atlas_rl_vertex`/`atlas_color` vertex emission (~L225-294) — swapping `rlBegin(RL_TRIANGLES)` for a real `Mesh`+`UploadMesh` means adding mesh/material fields to the scene's own state struct, not `Scene_Renderer` |
| Orbital Lattice → lighting + bezier links | node/link draw in `scene_orbital_lattice.c` (~L207-227) |
| Constellation → bloom/DOF | star/halo draw in `scene_constellation.c` (~L212-240), plus a private render-texture pair per the infrastructure note above |
| Spectral Terrarium → boids | `terrarium_simulate` in `scene_spectral_terrarium.c` (~L159-224), replacing the fixed-orbit update; needs a centroid/pan signal — see below |
| Spectrum → feedback trails | `scene_spectrum.c` draw (~L30-86); needs a private ping-pong render texture per the infrastructure note |

### Beat-phase tracking — currently a stub, needed before Tidal (and Cadence's beat-synced breathing)

Worth flagging plainly: **`Scene_Audio_Frame.beat_phase` (`scene.h:35`) is
declared but never assigned anywhere in the live codebase.** The one
per-frame audio-construction site (`plug.c:~991-1016`, the function that
builds `rms`/`peak`/`spectral_flux`/`onset` from the FFT spectrum each
frame) never sets `.beat_phase` — it defaults to `0.0`. What exists
instead is a per-scene *decaying pulse envelope* triggered by the `onset`
boolean (`scene_constellation_motion.c:19,58-59`,
`scene_orbital_lattice_motion.c:161,219-220`,
`scene_spectral_terrarium.c:218-219`) — a one-shot decay, not a continuous
tempo-locked phase. There is no beat-grid/tempo estimator anywhere in
`src/`. Tidal's entire premise (anticipation before the beat, release
after, driven by a continuous 0..1 phase) needs real beat tracking built
first — the natural home is a new function in `audio_analyzer.c` or a
sibling module (`beat_tracker.c`/`.h`, mirroring `audio_analyzer.h`'s
header-owns-its-state pattern), populated at the same
`plug.c:~1010-1016` site where `rms`/`peak`/`flux` are already computed.
Treat this as prerequisite infrastructure, not part of Tidal's own file.

### Spectral centroid / stereo pan — needed for Terrarium's boids concept

`Scene_Audio_Frame` has no centroid or pan field today. Cheapest path:
compute a spectral centroid locally inside Spectral Terrarium's own
`update()` from the `bands` array it already receives (energy-weighted
average of band index) — no upstream change at all. Only worth promoting
to a real shared `Scene_Audio_Frame` field (populated at the same
`plug.c:~991-1016` site that already loops over `spectrum.smooth` for
`rms`/`peak`) if a second scene wants the same signal later.

### Word-level lyric timing — needed for true per-word choreography in Cadence

`Lyric_Cue` (`lyrics.h:17-22`) is line-level only (`text[512]`,
`start_seconds`, `end_seconds`) — confirmed, no per-word timestamps exist.
Two tiers:
- **Cheap, works today:** estimate word windows inside the new scene's own
  `update()` by splitting `text` on whitespace and allocating each word a
  proportional slice of `[start_seconds, end_seconds]` by character count.
  No format change, no risk — good enough to prototype Cadence fully.
- **Real per-word timing:** touches `lyrics.h`'s struct, `lyrics.c`'s
  codec/validation, `schemas/project-v1.schema.json`'s lyric-cue schema,
  `project_io.c`'s round-trip, and the Whisper/Codex assisted-timing
  pipeline that produces cues in the first place
  (`tools/ANALYSIS_ADAPTERS.md`) — a project-format migration, not a scene
  change. Only worth it once the estimated version proves the concept.

### Event-driven concepts (Cartograph's fork points) — no plumbing needed

`EVENT_TYPE_CUE` already exists in `event_timeline.h:13-19`, and every
scene already receives the full `Event_Timeline_View` via
`Scene_Frame.events` (`scene.h`). Cartograph can filter
`type == EVENT_TYPE_CUE` directly inside its own `update()` from day one —
the one concept in the whole set needing zero new data plumbing, only new
scene-local consumption logic.

### Scene-transition dissolve/morph (preset-morphing, Loom's growth continuity)

The hard cut happens at `scene_switch_update()` (`src/scene_switch.c`,
called from `plug.c:879`) — it currently reports a single new
`scene_index` on transition. A blended/dissolving transition means this
call (or its caller) needs to report *two* indices plus a blend factor
during the transition window, which then means `scene_render()`
(`plug.c:1083`) drawing both scene instances into separate render
textures and cross-fading — the same render-texture technique as the
shared-infrastructure section above, applied at the scene-switch boundary
instead of continuously.

---

*Produced by four parallel Sonnet 5 sub-agents doing read-only source
review and web research — no code was written. Section 5 was added in a
follow-up pass via direct source inspection (not sub-agents). Line numbers
reflect `feature/scene-engine-foundation` at review time.*
