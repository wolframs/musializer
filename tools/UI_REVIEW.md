# Headless UI review

A loop for finding and fixing display and interaction defects in the desktop
workspace without occupying the operator's session and without pretending that
synthetic input proves a real interaction works.

## Why it exists

Scene rendering, project persistence, and analysis all have headless tests. The
workspace UI had none, because it is immediate-mode code whose rectangles are
computed inline while drawing. The defects it accumulates are consequently the
ones no existing suite can see: labels at inconsistent sizes within one row,
panels that overflow the window at supported sizes, text drawn at unreadable
contrast, and controls that report the wrong selected state.

## Isolation guarantees

Reviews must be runnable while the operator is working in the same checkout.

- Captures run on a **private Xvfb display** (`:77` by default,
  `MUSIALIZER_CAPTURE_DISPLAY` overrides). Nothing is drawn on the real
  session, no window takes focus, and the real cursor is never moved.
- `WAYLAND_DISPLAY` is unset and `PULSE_SERVER` is pointed at a path that
  cannot resolve, so a capture never opens a client stream on the audio server
  the operator is using.
- Every artifact is written under `build/`, which `.gitignore` excludes.
  Fixture audio, bridges, projects, and screenshots are never committed.
- Each state runs against a throwaway copy of the fixture project, and the run
  fails if the master fixture changed. This is not paranoia: capture states that
  dirtied the workspace used to trigger the autosave poll and rewrite the shared
  fixture, so a `--scene` state left the fixture on that scene and the *next*
  run's panel captures silently photographed different project state. Two
  capture directories are only comparable if both ran against the same fixture.
- One Xvfb per state. A state that hangs or rejects its probe spec cannot
  corrupt its neighbours, and each failure is reported by name.

The one thing not isolated is the shared `build/` output directory: a capture
run needs `./nob build` output, so do not start one while a different build is
in flight.

That shared binary has a trap worth knowing. `build/musializer` is whichever
profile was built last, and both this harness and the Python product smoke
tests in `tests/adapters/` run it. Leaving a `sanitize` build in place makes
those smoke tests fail on AddressSanitizer leak reports rather than on anything
they were testing — the same LSan false-failure `tests/e2e/README.md` warns
about. Rebuild `debug` or `release` before running the Python suite.

## The loop

```sh
./nob build debug
tools/ui_fixture.sh                       # once; writes build/ui-review/
tools/ui_capture.sh build/ui-review/shots # renders every state in ui_states.txt
```

Keep one set in `build/ui-review/reference/` as the picture of current behaviour
and capture changes into a scratch directory beside it. Refresh the reference
only from a run that reported no drift, and regenerate the fixture first if it
ever does — a reference taken against a mutated fixture is worse than none,
because it looks authoritative.

Then read the PNGs, write findings, fix, and re-capture into a second directory
to compare. A contact sheet is usually the fastest way to review a family of
related states:

```sh
ffmpeg -i 'build/ui-review/shots/scene-%d.png' \
    -filter_complex tile=3x3:padding=4 -frames:v 1 contact.png
```

Two review habits matter more than the tooling:

- **Verify a suspected bug before reporting it.** Capture the same surface at
  several playhead positions or window sizes first. Loom filling only part of
  the stage looks like a framing bug and is not one: the weave is revealed in
  proportion to elapsed track time, so a single capture at 15% of the track is
  working as designed.
- **Crop to native resolution before judging typography.** A downscaled full
  window hides the font-size and padding inconsistencies that are the point.

## What `--ui-probe` does

The catalogue drives the application through `--ui-probe`, a diagnostics flag
that sets deterministic workspace state and then holds it so the frame can be
grabbed. See `Plug_Ui_Probe` in `src/plug.h`.

```
--ui-probe panel=lyrics,time=6,size=1280x720
--ui-probe panel=none,fullscreen=1,time=6,play=1,size=1920x1080
```

| Key | Values | Notes |
| --- | --- | --- |
| `panel` | `none`, `tune`, `export`, `lyrics`, `assist` | Anything but `none` requires a loaded track |
| `fullscreen` | `0`, `1` | |
| `time` | seconds | Requires a loaded, seekable track |
| `play` | `0`, `1` | Default `0` |
| `size` | `WIDTHxHEIGHT` | Clamped by `SetWindowMinSize` |

It applies the same state transition the corresponding button performs, rather
than injecting synthetic mouse or keyboard events, and it never touches project
data or marks a project dirty. An unknown key, a repeated key, an unparsable
value, or a panel requested without a track is an error, so a typo in a capture
script cannot quietly photograph the wrong state.

`play=0` parks the transport so repeated runs of a chrome or panel state are
comparable. The spectrum analyzer is fed by the audio callback, so a parked
transport decays every audio-reactive scene toward its idle state: judging
scene visuals requires `play=1`, and that capture is deliberately not
frame-reproducible. With `play=1` the captured playhead is roughly
`time + MUSIALIZER_CAPTURE_SETTLE` (6 s default), which will run past the end of
a short fixture if `time` is close to its duration.

## Second opinions

Screenshots and code sections can be handed to an external reviewer prompted to
act as a critical experienced user rather than a polite assistant. Keep such a
reviewer strictly read-only:

```sh
codex exec -s read-only --ephemeral -C . \
    -i build/ui-review/shots/panel-lyrics-720p.png \
    -o review.md - < prompt.md
```

Treat the result as advice to verify, not as findings. In practice these
reviews have been most valuable for diagnosing *mechanism* — why a defect
happens, in which function — and for ranking work, and least reliable when
reasoning about pixels alone.

## What this does not cover

Screenshot review needs a human or a model to look at the result, so it cannot
gate a build. The complementary half belongs in the C test suite as pure layout
functions, following the precedent of `scene_settings_ui_layout` and
`caption_layout`.

One piece of that exists: `ui_row_typography.c` decides the single font size a
row of buttons shares and how a label that still does not fit is ellipsized, and
`tests/test_ui_row_typography.c` covers it headlessly. Still missing are
assertions that children stay inside their parents, that interactive rectangles
do not overlap, that hit targets meet a minimum size, and that every
`ui_theme.h` colour pair clears a contrast ratio.

Neither half catches a panel that simply has less room than its content needs.
The tracks panel action row fit four labels only by shrinking them all to the
minimum readable size; the fix was to stack them two by two, and the capture
loop is what made that obvious. Read the pictures, not just the tests.
