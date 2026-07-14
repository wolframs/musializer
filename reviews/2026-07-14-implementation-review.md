# Implementation Review — 2026-07-14

Code-level review of commits `17460b8` ("make projects portable and harden
workflows") and `ee98957` ("polish authoring UX and expand scene system"),
which implement `2026-07-14-ux-audit.md` /
`2026-07-14-ux-audit-implementation.md` and
`2026-07-14-creative-scene-ideas.md` /
`2026-07-14-creative-scene-implementation.md`. 60 files, ~5,650 insertions.

**Method:** six parallel Sonnet 5 sub-agents did independent, read-only
review of `git diff cb111e9..HEAD`, each scoped to a distinct risk area
(new determinism-critical infra, persistence/schema, `plug.c` state
machines, `plug.c` layout/visual, the seven reimagined scenes, build/tooling
plumbing). Each agent was briefed with the specific CLAUDE.md invariants
relevant to its area and asked to verify implementation-tracker claims
against actual code, not against the trackers' own checkmarks. The three
findings below were then independently re-verified by direct code reading
(not just accepted from agent reports) before being recorded here. No code
was modified.

## Bugs

### 1. HIGH — Scene-settings "Undo reset" can silently overwrite the wrong track's settings

`src/plug.c:4688-4724` (guard), `src/plug.c:4423-4441` (track switch)

The undo-arming guard only clears when the *scene id* changes:

```c
if (p->scene_settings_reset_scene != p->scene.id) {
    p->scene_settings_reset_undo_available = false;
    ...
}
```

The track-switch handler resets `event_undo_available` on click but never
touches `scene_settings_reset_undo_available`:

```c
p->current_track = i;
start_preview_track(next_track);
p->event_undo_available = false;
p->clear_events_confirmation = false;
```

**Repro:** on Track A, hit Reset → Confirm (arms undo with Track A's
settings snapshot). Switch to Track B, which happens to share the same base
scene as Track A. Click "Undo reset" — it applies Track A's old settings
onto Track B via `track_effective_scene_settings(track)`, where `track` is
now B, with no confirmation shown.

This is exactly the class of silent cross-context mutation the two-step
Reset confirmation (ux-audit items 1.6/3.3) was built to prevent, and it
slipped through because the guard is keyed on scene id instead of track
identity.

### 2. MEDIUM — Assist failure detail goes stale on validation-rejection, contradicting the notice toast

`src/plug.c:3479-3484` (call site) vs. `src/plug.c:2988-3051`
(`load_analysis_candidate_for_track`)

When a completed Assist job's bridge fails validation (bad duration match,
oversized/empty file, unreadable file, etc.), `load_analysis_candidate_for_track`
pushes a specific `notice_push(...)` toast for each rejection reason. The
call site, however, only does:

```c
if (candidate == NULL) {
    p->assist_job_state = ASSIST_JOB_FAILED;
    return;
}
```

`p->assist_failure_detail` is cleared to `""` at job start (`plug.c:2967`)
and never repopulated on this path, so the in-panel status text falls back
to the generic default:

```c
p->assist_failure_detail[0] != '\0' ? p->assist_failure_detail :
"The helper exited before producing a validated result.";
```

The user sees one specific reason in the toast and a different, generic
reason in the Assist panel itself — violating the implementation tracker's
own claim for item 2.8 ("use one failure description in both Assist status
and its notice").

### 3. MEDIUM — Scene-preset capacity didn't scale with the new scene count

`src/scene_settings.h:11` vs. `src/project.h:17`

```c
SCENE_SETTINGS_PRESETS_PER_SCENE = 8   // scene_settings.h
SCENE_SETTINGS_SCENE_COUNT       = 9   // scene_settings_values.h (was 7)
MUSI_PROJECT_MAX_SCENE_PRESETS   = 56u // project.h — unchanged
```

`56` is exactly the old `7 × 8` ceiling. The UI now allows creating presets
across 9 scenes (`9 × 8 = 72` possible), but project persistence still caps
the total at 56. Past that point, `Save` fails with a generic notice —
"Project could not be built — A track path, authored lane, or output
setting is outside the project contract" — that gives no indication the
cause is preset count. Not data loss, but an undiagnosable dead end, and
the one place where "raise persistence capacities in lockstep" (claimed
done in `2026-07-14-ux-audit-implementation.md` P4) was missed for the
scene-count expansion introduced by the other tracker.

## Lower-severity (sub-agent reported, plausible, not independently re-verified)

- **`src/scene_cadence.c:145-166`** — `CADENCE_MAX_WORDS = 32` silently
  truncates word-swarm geometry for lyric lines with more than 32 tokens;
  the 32nd word absorbs the remaining line duration. Cosmetic edge case,
  not a safety issue — unlikely to matter for real lyrics.
- **`src/scene_settings.c:444-474`** (`scene_settings_ui_layout`) — the
  inspector-width clamp order (floor at 280 → shrink for a 620px workspace
  floor → re-floor the shrunk value at 240) could theoretically push
  `workspace_width` below its intended floor for window widths in roughly
  `[640, 860)`. Unreachable in practice: `musializer.c` enforces a 960×640
  minimum, and at 960 the math holds (`workspace_width=680`, preview
  `45.8%` of window ≥ the required 30%).
- **`tests/test_project_io.c`** — no explicit legacy-strip round-trip test
  for the three newly-optional fields (`ascii_image`, scene-switch
  `settings`, `scene_presets`). Hand-tracing `parse_project`'s and
  `parse_switch`'s field masks confirms old-format `.musi` files without
  these fields still parse correctly with empty/zeroed defaults that pass
  `musi_project_validate` — the logic is correct, just untested to the
  project's usual boundary-case convention.

## What held up clean

- **Determinism:** no unseeded `rand()` or wall-clock reads anywhere in the
  new scenes (Cadence, Loom) or the new `beat_tracker.c`. Beat-tracker state
  lives on `Plug` and is explicitly reset via `beat_tracker_reset` on
  analyzer reconfiguration and at the start of every export, so export
  behavior doesn't depend on prior preview scrubbing.
- **The `beat_phase` stub is fixed.** `2026-07-14-creative-scene-ideas.md`
  §5.4 found `Scene_Audio_Frame.beat_phase` was declared but never assigned.
  It's now a genuine onset-interval phase estimator with a weighted moving
  average, octave-error folding into a `[40, 240] BPM` range, deterministic
  reset on discontinuities/seeks, and meaningful test coverage (onset
  learning, phase progression, silence, invalid input, forward/backward
  discontinuity).
- **NaN/Inf hygiene:** every new division across the beat tracker and the
  seven reimagined scenes (flocking forces, terrain-lighting normals,
  kaleidoscope fold ratio, bar-taper exponent) is guarded against
  zero/degenerate input.
- **Fixed-capacity discipline:** all new per-scene state (Cadence/Loom word
  and tapestry state, Terrarium's fixed 10-creature flocking, Constellation's
  bounded node/flare arrays) stays within compile-time-sized arrays; no
  unbounded growth or stack-resident large objects were introduced.
- **`pixel_scale` respected** in the 2D scenes that draw fixed-pixel detail
  (Pulse Field trail thickness, Spectrum bars); the 3D scenes (Orbital
  Lattice, Song Atlas) are unaffected since their new geometry is
  world-space.
- **Build/registration completeness:** both new scenes and the new
  `beat_tracker`/`scene_constellation_motion` modules are correctly wired
  through `scene.h`/`scene.c`, `scene_settings.c/h`, the CLI name mapping,
  `analysis_bridge.c/h`, `musializer.c` help text, and both
  `src_build/nob_stage2.c` source lists (app and headless test builds),
  cross-checked by a dedicated Python registration test
  (`test_signature_scenes_are_registered_everywhere`).
- **POSIX/Windows parity:** the new `ffmpeg_available()` preflight probe was
  added to both `ffmpeg_posix.c` and `ffmpeg_windows.c` in the same pass, as
  a pure filesystem/`SearchPathA` check with no shell invocation.
- **No injection risk introduced** in `tools/external_analysis.py` or the
  ffmpeg host files — all subprocess calls remain argv-list based, no
  `shell=True`/`system()`/`popen()` with interpolated strings.
- **Transactional persistence strengthened, not weakened:** the new
  "portable project" asset-bundling feature
  (`musi_project_bundle_asset`) hash-verifies before publish, uses
  link()-based atomic content-addressed publishing, and is proven by a
  dedicated test to reject symlink escape, `..` traversal, and a hijacked
  `.assets` directory symlink. `musi_project_editor_support` was correctly
  extended for the new `MUSI_ASSET_IMPORTED` feature. fsync-failure
  propagation was also strengthened (new `MUSI_PROJECT_FILE_ERROR_DURABILITY`
  instead of silent swallowing).
- **Close-guard coverage intact:** `plug_confirm_close` still covers all
  five required conditions (dirty lyric draft, staged Assist candidate,
  running Assist job, running export, unsaved project) after the UI
  changes.
- **No unsafe string handling:** no `strcpy`/`sprintf` found in any of the
  new notice/tooltip/status code; all bounded via `snprintf`.

---

*Six parallel Sonnet 5 sub-agents did the initial scoped review; the three
recorded bugs were independently re-verified by direct source reading
before being written up here.*
