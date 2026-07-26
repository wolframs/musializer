# Cadence overhaul notes (2026-07-26)

Captured during the caption-typography session, at the operator's request, so
the thinking is not lost. **Nothing here is implemented.** This is a scratchpad
for a future session, not a description of the scene as it stands.

The operator's summary was: Cadence is "undertuned", has "flaky animation
characteristics", and would benefit from face and colour customization, proper
anti-aliasing, and proper particle fade-in and fade-out.

## Why Cadence sits outside the caption work

`plug.c` skips the shared lyric overlay entirely when the active scene is
Cadence, because Cadence typesets the lyric itself at `height*0.20*scale` with
its own per-word animation. That is deliberate: the word choreography *is* the
scene. It also means the caption style added in this session -- face, backing,
placement, size, width, inset, ink -- reaches the other nine scenes and not this
one. Anyone reading the Style controls with Cadence selected will reasonably
expect them to apply, and today they do not. That gap is the first thing an
overhaul should close or explicitly label.

## What is known good

`scene_cadence_timing.c` was extracted in the previous session and has five
headless tests. It owns `smoothstep`, the line dissolve envelope, the per-word
hold, and window assignment across a cue's glyphs. The fix it carries is real
and photographed: a word whose window has not closed is exempt from the line's
dissolve, so the final word of a line reads as type through its own moment
instead of dispersing into particles before it is ever legible.

Whatever is done next, keep that module as the seam. Timing belongs there;
drawing belongs in `scene_cadence.c`. The extraction is what made the last
defect describable.

## Candidate work, roughly in order of value

1. **Face and colour.** Cadence should at minimum honour the caption style's
   face and ink colour, so a project reads as one piece of design across scene
   changes. Size, placement and backing are a different matter -- the scene's
   composition assumes a centred, full-width line, and honouring an arbitrary
   anchor means re-laying-out the whole animation around it. Face and colour
   first; treat the rest as a separate decision.
2. **Anti-aliasing.** Glyphs and particles are drawn at scene scale without the
   supersampling accommodation the rest of the renderer makes. High and Master
   export use spatial supersampling and fixed-pixel detail is meant to respect
   `Scene_Renderer.pixel_scale`; audit every constant in `scene_cadence.c`
   against that rule before adding effects on top.
3. **Particle fade-in and fade-out.** The ambient particle field appears and
   disappears rather than arriving and leaving. This is also a review hazard:
   the field between two cues looks enough like the dissolve bug that it made
   two before/after render pairs come out identical during the last session. A
   proper envelope would make the two states visually distinct, which is worth
   something on its own.
4. **"Undertuned."** Cadence exposes fewer controls than its neighbours. Worth
   an inventory against `scene_settings_values.h` before inventing new ones:
   the useful question is which of its magic numbers a user would actually want
   to move, not how many sliders can be added.

## Traps to carry forward

- **Read the cue bounds before choosing a render window.** The demo fixture's
  first cue ends at 5.2 s; 5.4 s is the *next* cue's start. Rendering 5.1-5.4
  photographs the gap, not the cue. A null result from the wrong sample is the
  most expensive kind of wrong.
- **Cadence's last word ends at exactly 1.0 by construction**, which is why the
  per-word hold has to treat "window not yet closed" as the exempt case rather
  than comparing against a threshold.
- Any change to what Cadence draws changes exported pixels. Use the
  byte-identical canary technique: render a short 720p window before and after,
  and expect the hash to change only where you intended it to.
