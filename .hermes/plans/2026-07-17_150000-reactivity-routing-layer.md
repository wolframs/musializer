# Reactivity Routing Layer Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan
> task-by-task.

**Goal:** A shared modulation layer under all ten scenes that connects audio
features (rms, peak, spectral flux, beat phase, individual bands) to scene
parameters, editable in the Tune inspector — the "Winamp AVS routing"
capability, done the Musializer way: deterministic, strictly persisted,
preview/export identical.

**The pivotal discovery (read this first):** the `.musi` v1 format already
specifies the entire persistence model. `Musi_Parameter_Mapping`
(`src/project.h:123`) carries `source ∈ {rms, peak, spectral_flux,
beat_phase, band}`, `band_index`, input/output ranges, an interpolation curve
(`step|linear|smoothstep|ease_in|ease_out`), and a `clamp` flag; and
`musi_mapping_evaluate()` (`src/project.c:426`) is implemented and tested.
Today the editor persists sliders as *degenerate constant mappings*
(source=rms, `output_min == output_max`) and
`scene_settings_mapping_supported()` (`src/scene_settings.c:401`) plus the
editor-support gate (`src/project.c:360,378`) deliberately reject everything
else. **This plan is therefore not a format extension — it is widening the
editor and runtime to the format's existing capability.** No schema change,
no migration.

**Architecture in one paragraph:** `scene_settings_get()` is the single choke
point every scene already reads its tunables through, per frame, in both
preview and export (`make_scene_frame()` at `src/plug.c:879` and the export
fast-forward loop both build frames the same way). A new headless
`scene_routes` module evaluates a per-scene table of dynamic mappings against
the current `Scene_Audio_Frame` and produces an *effective*
`Scene_Settings` snapshot each frame; scenes stay untouched and
preview/export parity is inherited, not re-proven. Routes use **replace
semantics** (the mapping output *is* the parameter value), clamped to the
setting descriptor's min/max. Persisted slider constants remain degenerate
mappings, but the runtime route table requires distinct output endpoints so a
route cannot silently round-trip as a slider in the v1 format.

**Tech stack:** C11, raylib, `nob`. No new dependencies in phases 1–3.

**Branch:** continue on `worktree-loom-reactivity` (contains the Loom weave
work this plan generalizes).

---

## Progress log (updated as tasks land)

- **Phase 1 done (2 commits, 2026-07-17, now on master).** `scene_routes`
  module (validate, source binding, replace-semantics apply, spec parser),
  frame-loop wiring through `make_scene_frame`, and the repeatable `--route`
  CLI with first-track adoption. Routed segment exports are
  framemd5-deterministic.
- **Phase 2 done (2026-07-18).** `scene_routes_export/import_mappings`
  persist routes beside constants in `scenes[0].mappings` (a parameter is
  exactly one of the two; all-or-nothing import), editor gate widened via
  `scene_routes_mappings_supported`, save/open call sites wired in plug.c.
  Verified end to end: the route survives save as schema-correct JSON, a
  reopened project renders byte-identically to the authoring session,
  no-route mp3-vs-project renders are identical, CLI argument order is
  irrelevant, and a sanitize-build routed render is clean. 191/191 C tests
  in debug/release/sanitize.
- **Phase 3 done (2026-07-18).** 3.1: `route_editor_state` headless draft
  state machine (open/edit/apply/remove lifecycle, clamped mutations, dirty
  predicate, summary/meter display policy; nine tests, precedent
  `assist_ui_state`). 3.2: Tune-inspector integration in `plug.c` — per-row
  `~` route affordance, routed rows show the live driven value plus a
  `source · curve · range` summary and live-source meter in place of the
  slider, inline editor row (source buttons, band stepper, input/output
  mini-sliders, curve cycler, clamp/invert, Apply/Remove/Discard), dirty
  draft joins `plug_confirm_close`, editor slot follows hot-reload track
  compaction, `PLUG_STATE_VERSION` 27. 3.3: 200/200 C tests in
  debug/release/sanitize, 82/82 Python, hotreload build clean, routed
  project self-determinism re-verified (framemd5), routed-vs-plain differ,
  `beat_phase` route exercised on a second scene (pulse) via CLI. The
  sanitize app binary fails the two render smoke tests only because
  LeakSanitizer flags one-time GL/driver init leaks — same reason the C
  runner disables LSan; run those against a debug/release binary.
  Interactive mouse-driven behavior was NOT synthetically injected; the
  desktop interaction needs a human pass.
- **Review hardening done (2026-07-18).** Route application now has
  descriptor-driven coverage across every setting of every scene, including
  canonical midpoint quantization for toggles and transactional failure.
  Equal output endpoints are rejected as non-reactive; the canonical
  full-range RMS case is also the editor's slider-constant representation.
  CLI routes are deferred until project/audio loading completes, with an
  end-to-end order-independence test. Dirty route drafts now guard
  Saved status, Save/Save As, project/track/scene changes, autosave, rendering,
  and quit; automatic scene switching pauses while the active route editor is
  open. 200/200 C tests pass in debug/release/sanitize, 83/83 Python tests
  pass, and all four application build profiles are clean.
- **Next:** user feedback on the editor UX; Phase 4 items remain gated on
  explicit decisions.

---

## Critical constraints (read before any task)

1. **No schema change.** Everything in phases 1–3 must round-trip through the
   existing `schemas/project-v1.schema.json` `mapping` definition. If a task
   seems to need a new field (e.g. attack/release smoothing), it belongs in
   the deferred phase 4 and needs an explicit format decision first.
2. **Determinism.** Route evaluation may depend only on the frame's audio
   values and the persisted route table. Sources bind exactly to the
   `Scene_Audio_Frame` values (`rms`, `peak`, `spectral_flux`, `beat_phase`,
   `bands[band_index]`) — no wall clock, no hidden smoothing in v1. Export
   determinism must be re-verified with a two-render framemd5 comparison.
3. **Strict validation.** A route's `parameter` must resolve through the
   scene-settings descriptor tables; `band` requires `band_index` within
   `AUDIO_ANALYZER_MAX_BANDS`, other sources require `band_index == 0`
   (schema `allOf` rule); ranges finite, `input_max > input_min`; duplicate
   parameters within a scene rejected. Reject NaN/Inf everywhere.
4. **Provenance.** Routes are user-authored configuration, not analysis
   output. Never auto-create routes from model output; Assist/MiMo may only
   *stage* route suggestions behind Apply/Discard (out of scope here).
5. **Hot reload.** Runtime route state lives in `Track`/`Plug` state (plain
   memory), never module-static (see `.hermes/plans/2026-07-16_143000-plug-c-split.md`
   constraint 1).
6. **Editor honesty.** Widening `musi_project_editor_support` means newer
   projects with routes will be *rejected by older builds* (by design — the
   gate exists exactly for this). Update the gate's error message and the
   compatibility notes; never silently drop routes on open/save.
7. **Style/build.** Four-space indent, module prefixes, no `-ffast-math`.
   New engine `.c` files go into `append_engine_sources`, headless ones also
   into `append_tested_core_sources` (`src_build/nob_stage2.c:~100,~143`).

## Validation rhythm (after every task)

```sh
./nob build debug 2>&1 | tail -3
./nob test release 2>&1 | tail -2       # 180/180 at plan time; grows per task
```

Before merge: `./nob test debug|release|sanitize`, Python suite
(`python3 -m unittest discover -s tests/adapters -v`), `./nob build
hotreload`, a real-media render via `--render-window` + `ffprobe`, and the
two-render framemd5 determinism check.

---

## Phase 1 — Runtime routing core (no UI, no persistence yet)

### Task 1.1: `scene_routes` module

**Create:** `src/scene_routes.c/.h`, `tests/test_scene_routes.c`; register in
both build lists.

Runtime model (fixed capacity, plain memory):

```c
typedef struct Scene_Routes {           // one per scene id
    size_t count;
    Musi_Parameter_Mapping items[SCENE_ROUTES_PER_SCENE_CAP]; // reuse the musi type
} Scene_Routes;
typedef struct Scene_Route_Table {
    Scene_Routes scenes[SCENE_SETTINGS_SCENE_COUNT];
} Scene_Route_Table;
```

API sketch:

- `scene_routes_validate(scene_index, const Musi_Parameter_Mapping *)` —
  constraint 3 rules, plus "parameter resolves to a descriptor of this
  scene". Reuse `descriptor_for_key` logic (export it from scene_settings or
  pass a lookup callback; prefer a small exported
  `scene_settings_descriptor_by_key(scene_index, key, size_t *index)`).
- `scene_routes_source_value(const Scene_Audio_Frame *, source, band_index,
  double *out)` — the feature-bus binding; false on missing bands/OOB.
- `scene_routes_apply(const Scene_Route_Table *, size_t scene_index,
  const Scene_Audio_Frame *, const Scene_Settings *base,
  Scene_Settings *effective)` — copy base, then for each valid route:
  evaluate via `musi_mapping_evaluate`, clamp to descriptor min/max, write.
  A route that fails to evaluate leaves the base value (never NaN, never a
  half-applied table).

**Tests:** replace semantics; descriptor-bound clamping; band routing incl.
`band_index >= bands_count`; every registered descriptor and setting kind;
flat output routes rejected as slider constants; hostile input (NaN ranges,
unknown keys, duplicate parameters); byte-determinism of `effective` for
identical inputs.

### Task 1.2: wire the effective snapshot into the frame loop

**Modify:** `src/plug.c` (`make_scene_frame`, and the `Scene_Renderer`
construction sites so `renderer->settings` sees the same effective snapshot),
`src/track.h` (a `Scene_Route_Table routes` member + dirty flag).

One evaluation per frame, stored in Plug-owned memory (hot-reload safe),
consumed by both `scene_instance_update` and `scene_instance_draw`, in the
live loop *and* the export loop (the export fast-forward path already calls
`make_scene_frame` — verify the renderer settings pointer there too).

**Tests:** unit-level parity — feed identical frame inputs through the
preview-path and export-path snapshot construction and require identical
effective settings. Manual: temporary hardcoded route (e.g. band 2 →
`loom.weight`), run app, confirm; remove before commit.

### Task 1.3: `--route` CLI injection (the iteration lever)

**Modify:** `src/musializer.c` (repeatable `--route SPEC` argument, precedent:
`--event type:seconds:id:value`), a `plug_add_route(...)` entry point, help
text, README CLI section.

Colon-separated spec mirroring the mapping fields, e.g.
`--route loom.weight:band:2:0:1:0.4:2.2:smoothstep:clamp` (curve and clamp
segments optional with defaults). Parsed strictly through
`scene_routes_validate`; invalid specs fail startup with a clear message
rather than being skipped. Routes injected this way land in the track route
table and mark the project dirty, so *saving persists them* — this makes the
CLI a full authoring path while the inspector UI does not exist yet, and the
parser is headless-testable. This task is the agile pivot of the plan: after
it lands, real experimentation drives what Phase 3's UI needs to be.

---

## Phase 2 — Persistence and editor support

### Task 2.1: split/merge on open and save

**Modify:** save path (`src/plug.c:2983` area) to append the track's dynamic
routes after the exported constant mappings (shared capacity
`MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE`; reject over-capacity saves with a
notice, never truncate). Open path (`src/plug.c:3246` area) to partition
`scenes[0].mappings` into constants (→ `scene_settings_import_mappings`) and
dynamic routes (→ `scene_routes_validate` + track table). A mapping that is
neither a supported constant nor a valid route fails open (strictness), it
is not skipped.

### Task 2.2: widen the gates

**Modify:** `scene_settings_mappings_supported` call site in
`musi_project_editor_support` (`src/project.c:360`) to accept
constant-or-valid-route per mapping; update the error string at
`src/project.c:378`. Keep rejecting what the UI still cannot round-trip.

**Tests:** fixture `.musi` with mixed constant + dynamic mappings
round-trips byte-faithfully (open → save → identical mapping set); legacy
constant-only projects unchanged; a project using only-schema-valid but
editor-unsupported constructs still rejects. Update
`tests/test_project*.c` / `tests/test_scene_settings.c` expectations.

### Task 2.3: docs for the format boundary

README (routing workflow paragraph), `EXTENSION_PLAN.md` status entry
(**coordinate at merge time — another agent has pending edits there**), and
compatibility note: routes are v1-schema-valid, older editors reject them by
design.

---

## Phase 3 — Tune inspector UI

### Task 3.1: route editor state machine (headless)

**Create:** `src/route_editor_state.c/.h` + tests (precedent:
`assist_ui_state`). States: pick source / band, edit ranges, pick curve,
toggle clamp, remove route; dirty-marking rules; guard interactions (an
in-progress route edit participates in close/context-change guards like
other drafts).

### Task 3.2: inspector integration

**Modify:** the Tune inspector rendering in `plug.c` (or the widgets module):
each slider row gains a route affordance — when routed, the slider shows the
live effective value (read-only) plus a compact `source → curve → range`
summary; a meter strip shows the live source value so the user finally *sees*
the waveform connect to the parameter. Keep all logic that can be headless in
3.1's module; do not pretend synthetic input tests prove the interaction.

### Task 3.3: product pass

Full validation battery; short real-media renders on at least two scenes with
routes active (one `band` route, one `beat_phase` route); framemd5
determinism; README screenshots/wording; `packaging/PRODUCT_READINESS.md`
only if a platform boundary changed (not expected).

---

## Phase 4 — Deferred (explicit decisions required, do not start silently)

- **Temporal shaping** (attack/release per route): needs new schema fields →
  format migration decision.
- **Smoothed source variants** (e.g. `rms_env`): schema enum extension; would
  also let `constellation_motion` / `orbital_lattice_motion` /
  `scene_loom_weave` collapse onto one shared feature bus.
- **Parameter cues UI:** `Musi_Parameter_Cue` + `musi_project_parameter_at`
  (`src/project.c:452`) are implemented but unwired — timeline automation is
  a natural sibling feature.
- **Expression routes:** vendor MIT-licensed
  [projectm-eval](https://github.com/projectM-visualizer/projectm-eval)
  (portable ns-eel2 replacement — the engine behind Winamp AVS/MilkDrop
  scripting) as an additional route source. Dependency decision per
  CLAUDE.md; the AVS design reference is the 2005 BSD release
  ([visbot/vis_avs](https://github.com/visbot/vis_avs), maintained port
  [grandchild/vis_avs](https://github.com/grandchild/vis_avs)). Never borrow
  from the withdrawn 2024 "Winamp Legacy" source (restrictive license).
