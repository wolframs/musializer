# UX audit implementation tracker

This checklist turns `2026-07-14-ux-audit.md` into an ordered delivery plan.
The audit remains the evidence and rationale; this file records implementation
status and verification. Items are grouped by dependency and user impact rather
than by the audit's source-code sections.

Status legend: `[ ]` pending, `[~]` in progress, `[x]` implemented and tested,
`[-]` deliberately not adopted (with rationale).

## P0 — Core controls and recoverable failures

- [x] **1.1** Keep all scene choices available at the supported minimum height.
- [x] **2.1** Wrap or deliberately truncate notice detail and path text.
- [x] **2.2** Give actionable failures Retry and Copy path actions.
- [x] **2.3** Report the specific Assist launch failure and mention logs only
  when one exists.
- [x] **2.4** Check FFmpeg availability before opening the export workflow.
- [x] **2.5** Route capture-device failures through the notice system.
- [x] **3.3** Introduce danger-only styling for destructive confirmations.

## P1 — Responsive layout and safe state changes

- [x] **1.2** Replace Tune's silent window resize with an explicit affordance.
- [x] **1.6** Give scene Reset a two-step confirmation and one-shot undo.
- [x] **1.7** Protect at least 30% of the window for the preview when Tune opens.
- [x] **2.6** Give notice severities distinct semantic colors.
- [x] **2.7** Style Assist completion/failure by outcome rather than activity.
- [x] **2.8** Use one failure description in both Assist status and its notice.
- [x] **2.9** Distinguish an armed Assist mode from a running job.
- [x] **2.10** Show how many notices are hidden beyond the visible tray.
- [x] **2.13** Keep notices for pending two-step actions until resolution.

## P2 — Interaction clarity and timeline hierarchy

- [x] **1.3** Surface scene shortcuts 1–7 in tooltips.
- [x] **1.4** Confirm visible base-scene changes with a lightweight notice.
- [x] **1.5** Explain what + Feel, + Scene, and + Custom record.
- [x] **1.8** Explain preset Load, Update, and Delete actions on hover.
- [x] **1.9** Simplify explicit seek controls while retaining precise shortcuts.
- [x] **2.11** Make the close guard list concrete unresolved work in priority order.
- [x] **2.12** Explain disabled Assist modes on hover.
- [x] **3.2** Distinguish button press, hover, selection, and slider drag states.
- [x] **3.7** Give empty preset state dedicated guidance instead of disabled chrome.
- [x] **3.8** Strengthen the timeline action-category treatment.
- [x] **3.9** Add amplitude depth and a distinct cap to the waveform playhead.
- [x] **3.10** Give Export appropriate primary-action weight.

## P3 — Visual-system consistency

- [-] **3.1** Keep icon-only treatment for compact transport/capture controls
  and explicit text labels for editing actions. Adding decorative glyphs to
  scene and project buttons would reduce clarity rather than unify the roles.
- [x] **3.4** Establish and apply a named UI type scale.
- [x] **3.5** Establish shared panel-padding and control-gap tokens.
- [x] **3.6** Establish shared standalone and compact control heights.
- [x] **3.11** Size the scene-settings panel to its actual control count.
- [x] **3.12** Separate informational-muted and disabled text colors.

## P4 — Scene tuning depth and compatibility

- [x] Add the proposed Spectrum controls for glow softness, hue swing, core
  glow, and bar taper.
- [x] Add Terrarium ecosystem speed, creature speed, habitat opacity, and
  density; make motion affect the simulation clock.
- [x] Add Constellation event duration, event reach, hue swing, and density.
- [x] Raise persistence capacities in lockstep if required, preserve legacy
  snapshots, update the schema, and test preset/cue/project round trips.

## Verification gates

- [x] Targeted layout, notice, Assist-state, timeline, and scene-setting tests.
- [x] Debug, release, and sanitizer C test suites (165/165 each).
- [x] Python adapter/product tests.
- [x] Debug, release, sanitizer, hot-reload, and microphone-enabled builds.
- [x] Six-second real-media Spectrum, Terrarium, and Constellation renders;
  each produced 144 H.264 High/yuv420p/BT.709 frames at 640x360 and 24 fps.
- [x] Linux distribution build and private-artifact allowlist inspection.
- [x] README and product documentation describe the final interaction model.
