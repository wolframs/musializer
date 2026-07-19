# Musializer

<p align="center">
  <img src="./resources/logo/logo-256.png" alt="Musializer logo" width="192">
</p>

Musializer is a desktop studio for turning music into reactive scenes, timed
lyrics, and deterministic high-quality video. It is written in C with raylib,
opens ordinary audio files directly, and keeps each song as a portable `.musi`
project.

This is an **AI-agent-developed fork**, built under human direction and review,
of [tsoding's original playful C repository](https://github.com/tsoding/musializer).
The fork keeps the original project's immediacy and custom C build system while
growing it into a scene-based visualizer, editor, and offline rendering tool.
The upstream demo below remains a lovely snapshot of where it began.

> [!WARNING]
> Musializer is under active development and the `.musi` format is currently
> version 1. Keep backups of irreplaceable projects when moving between builds.
> Signed binaries and a native system installer are not published yet.

## What it can do

- Play WAV, OGG, MP3, QOA, and FLAC files with live audio-reactive visuals.
- Switch among ten built-in scenes: Spectrum, Pulse Field, Orbital Lattice,
  ASCII Field, Song Atlas, Spectral Terrarium, Constellation, Cadence, Loom,
  and Pentagram Orbits.
- Tune every scene from a live parameter inspector. Spectrum exposes separate
  taper, semantic hue, and glow shaping; Spectral Terrarium exposes ecosystem
  speed, creature speed, population, and habitat glass; Constellation exposes
  star density plus event reach, duration, and hue response. Song Atlas includes broad
  terrain/camera ranges, 1x-3x sampling detail, manual or music-reactive hue,
  camera orbit, distance, and drift controls, and Filled/Wireframe surface
  modes; exact values and per-scene resets are saved with the track and
  reused by offline export, while numbered per-scene tuning presets live in a
  per-user library shared across every track and project.
- Turn timed lyric words into beat-choreographed kinetic geometry with Cadence,
  or accepted semantic energy/tension/valence into a growing whole-track textile
  with Loom.
- Use ASCII Field as a procedural rolling spectrogram, or import an image as a
  color-aware glyph canvas with live spectral density, animated glyph waves,
  and compression-resilient CRT scanlines; author timeline events and edit
  timed lyrics in the application.
- Navigate with a depth-shaded whole-track waveform, draggable capped hairline
  playhead, one-second buttons, and exact tenth/one/ten-second keyboard steps.
- Generate local measured section suggestions and timed lyrics: lyrics already
  present in the track's metadata or a sibling text file are synchronized
  deterministically against Whisper word timing, and only tracks without
  known lyrics fall back to transcription plus a headless Codex review.
  Optionally ask Xiaomi MiMo V2.5 through OpenRouter for a semantic
  description of how the music feels.
- Stage every assisted result for review instead of silently changing a project.
- Auto-switch scenes from reviewed section markers.
- Render deterministic H.264/AAC MP4 video up to 7680x4320 with anti-aliased
  scenes, shared lyric captions, exact decoded-audio frame scheduling, and
  transactional output publication.
- Save lyrics, semantic/manual events, scene suggestions, seed, provenance, and
  output intent in strict `.musi` v1 projects.
- Work as a normal Linux desktop application, a CLI renderer, or a hot-reloaded
  C development build.

Tracker modules (`xm` and `mod`) work for interactive playback and preview, but
not offline MP4 export because raylib does not expose their decoded Wave data.

## Quick start on Linux

The nicest way to use Musializer on Linux, including KDE Plasma, is to install
the per-user launcher from a source checkout:

```console
$ ./tools/install-linux-launcher.sh
```

The installer bootstraps and builds the release executable when needed, then
adds **Musializer** to the application menu and registers `.musi` projects with
the desktop MIME database. It writes only below `~/.local` and needs no `sudo`.
After installation, ordinary launches do not invoke the compiler.

You can now open audio or project files from the application menu/file manager,
or run:

```console
$ musializer path/to/song.mp3
$ musializer path/to/show.musi
```

The source checkout must remain where it was installed from. Re-run the
installer after moving it or when you want the launcher to use a new release
build. Diagnostics go to `~/.local/state/musializer/launcher.log`.

Remove the integration with:

```console
$ ./tools/install-linux-launcher.sh --uninstall
```

An unpacked Linux distribution can run the same installer without a compiler;
it uses the executable included in the archive.

## Build from source

Musializer vendors raylib and uses [`nob.c`](nob.c), a custom build system
written in C. You need a C compiler and the X11 development headers on Linux.
FFmpeg is optional for preview but required for MP4 export.

On Debian, Ubuntu, or Kubuntu:

```console
$ sudo apt install build-essential libx11-dev libxcursor-dev libxrandr-dev \
    libxinerama-dev libxi-dev
$ sudo apt install ffmpeg  # needed for video export and analysis workflows
```

Bootstrap `nob` once, build, and run:

```console
$ cc -o nob nob.c
$ ./nob build release
$ ./build/musializer
```

`./nob` with no arguments remains equivalent to `./nob build release`. The
build driver rebuilds itself after its sources change; if that self-rebuild is
interrupted, bootstrap it again with `cc -o nob nob.c`.

Useful profiles and checks:

```console
$ ./nob build debug
$ ./nob build sanitize
$ ./nob build hotreload
$ ./nob test debug
$ ./nob test release
$ ./nob test sanitize
$ python3 -m unittest discover -s tests/adapters -v
$ ./nob dist
```

Release, debug, and hot-reload recipes exist for Linux, macOS, OpenBSD,
MinGW-w64, and MSVC. Full ASan+UBSan application builds are currently supported
on Linux and macOS. See [product readiness](packaging/PRODUCT_READINESS.md) for
the tested packaging boundary and cross-platform caveats.

## Using the application

Start with **Open audio**, **Open project**, or drag a file into the first-run
workspace. Each entry under **Track projects** is an independent one-song
document.

The normal workflow is:

1. Choose a scene from the left rail. Keys <kbd>1</kbd> through <kbd>9</kbd>
   switch scenes directly.
2. Select **Tune** beside the scene list to open its parameter inspector. The
   inspector initially fits inside the existing window; choose **Expand** when
   you explicitly want the application to use available monitor space. The
   track rail compacts before the preview falls below 30% of the window. Drag a
   slider for live feedback. **Reset** requires confirmation and exposes a
   one-shot **Undo reset** action.
   **Save new** captures a per-scene preset; **Load**, **Update**, and
   **Delete** manage the selected preset. Presets are shared across all
   tracks and projects: they persist in a strict-JSON per-user store
   (`$XDG_DATA_HOME/musializer/presets.json` or
   `~/.local/share/musializer/presets.json` on Linux,
   `%APPDATA%\Musializer\presets.json` on Windows,
   `~/Library/Application Support/Musializer/presets.json` on macOS;
   `MUSIALIZER_PRESET_STORE` overrides the path), written atomically after
   every change. Projects saved by older builds keep their track-local
   presets byte-for-byte, and opening one copies any presets you do not
   already have into the shared library. A store file this build cannot
   accept is left untouched and read-only rather than overwritten.
3. Open **Lyrics** to write or import lyric cues and adjust their start/end
   times against the playhead.
4. Open **Assist** for timed-lyric help, measured scene planning, semantic music
   interpretation, or the complete pipeline. Selecting a workflow first shows
   its local/remote data boundary. Results are validated and staged with a
   lane-specific impact summary, then require an explicit Apply confirmation.
   A valid run with no suggestions is reported as **No editor changes found**
   and cannot be applied as a misleading no-op. **Copy result**, **Copy log**,
   and **Copy folder** keep the immutable job evidence reachable from the panel.
5. Inspect generated section markers and enable **Auto scenes** if they should
   drive both preview and export. To author a scene change manually, position
   the playhead, select and tune the scene, then choose **+ Scene**. The cue
   captures the scene's tuning at that moment; seeking and export reload the
   same snapshot. Cue boundaries currently use deterministic cuts.
6. Add manual visualization events with **+ Feel** and **+ Custom**, or import
   an image into ASCII Field.
7. Open **Export**, choose resolution, frame rate, and quality, then select the
   destination. The action is disabled with an explanation when FFmpeg is not
   discoverable, before a destination picker opens. Progress reports the exact
   frame count and ETA; cancellation does not replace an existing video.
8. Save with <kbd>Ctrl</kbd>+<kbd>S</kbd>, or use
   <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> for **Save As**.

Canonical edits autosave after a short idle period. Unapplied lyric drafts,
staged assistance, running jobs, active exports, and unsaved projects are
guarded before context changes or quit, so partial work is not silently lost.
Metadata-only autosaves reuse assets that this process already hash-verified
and published into the project's content-addressed bundle; explicit Save and
Save As continue to reverify the full asset content.
Notices use distinct info/success/warning/error colors, wrap their detail text,
show hidden queue depth, and let actionable Assist failures reopen the review
step or copy the immutable artifact/log path.

Timed lyrics use the same caption layer in preview and export. Long cues wrap
to three centered lines with a visible ellipsis. The bundled font atlas covers
accented Latin, Greek, Cyrillic, punctuation, currency, and common symbols;
CJK fallback, bidirectional text, and complex-script shaping are not yet
implemented.

### Assisted analysis and privacy

Assistance is optional and capability-based:

- **Measured section planning** is local and derives timing/structure from PCM.
- **Timed lyrics** first looks for the lyrics you already have: an explicit
  lyrics text file, a sibling `<track>.lyrics.txt`, or unsynchronized lyrics
  embedded in the audio file's metadata (ID3 `USLT`-style tags). When one is
  found, a fully local deterministic aligner synchronizes those authored lines
  against Whisper's word timing — the authored text is displayed verbatim,
  section headings and stage directions are filtered out, backing lines are
  kept, hallucinated evidence stretches are ignored, and any line that found
  no timing is reported instead of guessed. Only tracks without known lyrics
  fall back to Whisper transcription plus an evidence-preserving headless
  Codex wording review, which is now bounded to short readable cues, must
  account for the whole track, and is followed by a deterministic splitter.
  Whisper discovery prefers the best installed model
  (`large-v3-turbo` > `large-v3` > `large-v3-q5_0` > `medium.en`, an order
  measured on sung material where turbo recovered the most lyric lines at a
  fraction of the cost; `MUSIALIZER_WHISPER_MODEL` overrides).
- **MiMo feelings** uses the local measured analysis plus MiMo V2.5 through
  OpenRouter to produce semantic energy/tension/valence cues.
- **Full assist** combines those stages while retaining separate provenance.

Whisper evidence, deterministic lyric sync, Codex review, measured audio
features, MiMo interpretation,
and user-authored events remain distinct data lanes. MiMo/OpenRouter modes are
the explicit authorization boundary for sending track audio to a remote
service; every workflow asks before launch and names what leaves the
computer. While a helper runs, the UI reports real elapsed time and the 40-minute
job timeout rather than inventing a completion percentage. Cancellation is
polled without blocking playback, and a staged lyric lane cannot replace an
active authored lyric draft. Model output is validated, staged, and never
mutates the project automatically.

The more experimental scenes have deliberately different jobs. **ASCII Field**
is a rolling spectral glyph field that can blend an imported image's aspect,
tonal structure, and source color into that live canvas. **Orbital Lattice**
turns band energy into damped crystalline motion and flexible links. **Song
Atlas** analyzes the complete decoded track into a slope-lit time-by-frequency
terrain. **Cadence** estimates word windows inside accepted line-level lyric
cues and assembles each word from beat-responsive character swarms. **Loom**
samples accepted semantic events across the full track and weaves energy,
tension, and valence into thread density, interlace, and color temperature;
without a semantic lane it freezes the measured spectrum into each column as
the working edge passes, so the finished cloth is a spectrogram-as-textile
record of the song, with live band vibration, onset ripples, and
centroid-driven color temperature at the fell.
Without lyrics or semantic analysis, Cadence and Loom retain deterministic
audio-reactive ambient/fallback behavior rather than requiring a model service.
**Pentagram Orbits** draws the phase portrait of the Lyness recurrence
`y[k+1] = (1 + y[k])/y[k-1]`, whose every orbit provably closes after exactly
five steps: nested invariant-curve ovals surround the golden-ratio fixed
point while five-station orbits hop their pentagram chords in time with the
music. The nest is shaped by the sound itself: every invariant curve flexes
radially with the smoothed spectrum sampled around its circumference, each
beat sends a brightness ripple outward from the golden center, and sparks
lunge along their chords on the beat.

Set the optional credential in the process environment or in an ignored `.env`
at the repository root:

```dotenv
OPENROUTER_API_KEY=your-key-here
```

The helper parses only that named value and never sources `.env` as shell code.
It does not write the key into arguments, logs, manifests, or cache keys. Local
children receive credential-like environment variables stripped out.

Run the non-invasive product doctor to see which workflows are ready:

```console
$ python3 tools/musializer_doctor.py
$ python3 tools/musializer_doctor.py --json \
    --require export --require local_lyrics --require remote_mimo
```

The doctor checks discovery and writable locations only. It does not invoke
FFmpeg/models, make a network request, or reveal credential values. Setup and
lower-level adapter commands are documented in
[Analysis adapters](tools/ANALYSIS_ADAPTERS.md) and
[Measured analysis](tools/MEASURED_ANALYSIS.md).

### Projects and imported images

Projects embed the evaluated lyrics, semantic events, manual events, scene
suggestions, deterministic seed, export settings, metadata, and analysis
provenance. They do not require a mutable analysis cache to replay accepted
semantic cues. Saves use a durable temporary sibling and atomic replacement.
Every save imports the track audio and, when present, the ASCII source image
into a content-addressed sibling bundle:

```text
show.musi
show.assets/audio/<sha256>.<ext>
show.assets/images/<sha256>.<ext>
```

The project stores strict relative references plus SHA-256 identities. Existing
matching objects are reused, conflicting objects are rejected, and the project
file is published only after every required asset is present. As a result,
moving `show.musi` together with `show.assets/` keeps the project portable and
offline-replayable; source files elsewhere on the workstation are no longer a
runtime dependency after a successful save.

The v1 schema deliberately describes future composition features that this
editor cannot yet preserve. This build opens only its lossless editor subset:
full-track referenced or imported audio, integer-frame-rate H.264 MP4 output,
one enabled opaque full-track Normal scene, and Musializer's canonical slider
constants, built-in audio-driven routes, and scene-cue snapshots. Other
schema-valid composition features are rejected with an explicit
unsupported-feature error instead of being silently rewritten.

An imported ASCII image is stored as a verified image asset with its derived
grid dimensions. Reopening the project verifies the image identity and rebuilds
the deterministic grid before exposing the track; a missing, modified, or
escaped bundle path fails closed instead of silently clearing the scene.

## Rendering

Preview requests 4x MSAA. High and Master exports render at 2x spatial
resolution and downsample to the requested output; Balanced renders at native
resolution. Set `MUSIALIZER_RENDER_SUPERSAMPLE=0` to force 1x on a GPU that
cannot allocate the larger target.

The default export is 1920x1080 at 30 fps, High quality. Balanced, High, and
Master map to H.264 High-profile CRF 20/16/12 with `yuv420p`, BT.709 metadata,
256 or 320 kbit/s AAC, and fast-start metadata. The UI offers 720p, 1080p,
1440p, and 2160p at 24, 30, or 60 fps; validated CLI sizes can reach 7680x4320.

The decoded raylib Wave is the canonical input for both frame analysis and
FFmpeg audio. Musializer renders
`ceil(decoded_sample_frames * FPS / sample_rate)` frames and pads only the final
sub-frame audio tail to align stream and container endings within the MP4 time
base. FFmpeg writes a unique sibling file; only a clean encoder exit publishes
it over the selected destination.

For very long mixes, export can currently use gigabytes because the complete
decoded Wave and a float analysis copy are held in memory. GPU readback, FFmpeg
pipe writes, and finalization also still occur on the UI thread, so a slow
encoder can temporarily delay repaint/cancel input. These are known boundaries,
not release-quality claims; see [product readiness](packaging/PRODUCT_READINESS.md).

## CLI automation

The complete UI workflow does not require a terminal, but the CLI is useful for
repeatable renders and tests:

```console
$ ./build/musializer --help
$ ./build/musializer --scene orbital path/to/song.mp3
$ ./build/musializer --ascii-image path/to/image.png path/to/song.mp3
$ ./build/musializer --scene constellation \
    --event lyric:1.25:42:0.9 --event cue:2.0:43:1.0 path/to/song.mp3
$ ./build/musializer path/to/song.mp3 \
    --analysis-bridge build/analysis/song/analysis.bridge.tsv \
    --auto-scenes --render output.mp4
$ ./build/musializer path/to/song.wav --scene atlas \
    --resolution 2560x1440 --fps 60 --quality master --render output.mp4
$ ./build/musializer path/to/song.mp3 --scene atlas \
    --quality high --save-project path/to/show.musi
$ ./build/musializer --project path/to/show.musi --render output.mp4
$ ./build/musializer path/to/song.mp3 --scene pentagram \
    --render-window 35 20 --render preview-section.mp4
$ ./build/musializer path/to/song.mp3 --scene loom \
    --route loom.weight:band:2:0:0.8:0.4:2.5:smoothstep \
    --route loom.glints:spectral_flux:0:0:0.15:0:2 --render routed.mp4
```

Built-in scene selectors are `spectrum`, `pulse`, `orbital`, `ascii`, `atlas`,
`terrarium`, `constellation`, `cadence`, `loom`, and `pentagram`. Repeatable
`--event type:seconds:id:value`
arguments accept `lyric`, `semantic`, `cue`, or `custom`. A positional `.musi`
file is equivalent to `--project`. Command-line renders exit after FFmpeg
finishes, making them suitable for scripts and smoke tests. `--mute` starts
the session with the output volume at zero — playback, analysis, and export
behave identically, the speakers just stay quiet — which keeps scripted and
test launches from being audible.

Repeatable `--route` arguments connect live audio measurements to any scene
setting for this session, identically in preview and export:
`parameter:source:band:in_min:in_max:out_min:out_max[:curve][:noclamp]`. The
parameter is a scene-settings key (the `settings.` prefix is optional, e.g.
`loom.weight`); sources are `rms`, `peak`, `spectral_flux`, `beat_phase`, and
`band` with a spectrum band index; curves are `step`, `linear`, `smoothstep`,
`ease_in`, and `ease_out`. The mapped value replaces the slider value each
frame, clamped to the setting's range; toggle settings switch at the midpoint
of their mapped range. Output endpoints must differ—flat values are ordinary
slider constants, not audio routes. Route arguments are applied after the
input audio/project is loaded, so their placement relative to `--project` does
not change the result. Routes are saved into `.musi` projects
using the format's parameter-mapping representation — a setting persists as
either its slider constant or its route — and reopened projects render
byte-identically to the session that authored them. Projects containing
routes are rejected by older builds' editors by design rather than being
silently stripped.

Routes can also be authored visually: every row in the Tune inspector has a
`~` button that opens an inline route editor with source buttons, a spectrum
band stepper, a response curve, and a clamp toggle. The mapping itself is
edited as two anchors, each pairing one source level with one output value —
`Quiet 0.03 → 0.00` and `Loud 0.78 → 1.40` for loudness sources, `Calm/Busy`
for spectral flux, `Beat start/Beat end` for beat phase. A transfer graph
plots the full response (source level across, setting value up, the span
between the anchors shaded) with a dot riding the curve at the live audio
value, and a live readout shows the exact value the route is producing right
now — computed by the same function the frame loop uses. A routed setting's
row shows the live driven value plus a compact `source · curve · range`
summary in place of its slider (the underlying slider value is kept but
inactive until the route is removed). Equal output endpoints disable Apply
because `.musi` v1 represents that value as a slider. Route edits are drafts: they take effect on Apply,
Discard throws them away, and unapplied route changes block saving,
project/track/scene changes, rendering, and quitting the same way an unapplied
lyric draft does. Autosave also leaves the draft's track untouched. Automatic
scene switching pauses while the inline route editor is open so its host scene
cannot disappear mid-edit.

`--render-window START DURATION` (seconds) exports only that span of the
track. The engine first fast-forwards analysis, beat tracking, and scene
state through the preceding frames exactly as a full export would, so the
windowed frames match the same span of a full render; only the drawing and
encoding are skipped. Fractional boundaries expand to the containing video
frames so the requested interval is completely covered rather than losing its
tail; the decoded audio slice uses those same frame boundaries. The window
clamps to the end of the track, and a start at or past the track end fails
before any file is touched.

`--analysis-bridge` checks the bridge audio SHA-256 before importing lyric,
semantic, or scene lanes. `--auto-scenes` opts into its section recommendations
for both preview and export. Analysis artifacts and bounded logs live below
ignored `build/analysis/`.

## Development

Hot reload keeps most application logic in `libplug`:

```console
$ ./nob build hotreload
$ ./build/musializer
```

Rebuild while the application is running, focus its window, and press
<kbd>h</kbd> to load the new plug. `--reload-once` with a track or scripted
render performs one automated handoff smoke.

Coding agents should follow the checkout's local `AGENTS.md` when present. It
records the project-format, rendering, privacy, UI-state, cross-platform, test,
and packaging invariants that a local change must preserve. The durable roadmap
and implementation log live in [EXTENSION_PLAN.md](EXTENSION_PLAN.md).

Distribution recipes deliberately include an explicit allowlist of runtime
support files and exclude `.env`, Python bytecode, analysis caches, and user
media. Linux/OpenBSD produce `.tar.gz`, MinGW produces `.zip`, and macOS
produces `build/Musializer.app`; not all artifacts currently have equal helper
coverage. `./nob dist` always rebuilds first.

## Original demo

Music by [@nu11](https://soundcloud.com/nu11-chiptune), from the 20:38 mark of
[nu11 WIP works 2016-2022](https://soundcloud.com/nu11-chiptune/nu11-wip-works-2016-2022):

https://github.com/tsoding/musializer/assets/165283/8b9f9653-9b3d-4c04-9569-338fa19af071

## License and lineage

Musializer is released under the [MIT License](LICENSE). The original project
and copyright belong to Alexey "tsoding" Kutepov and Musializer contributors;
this fork retains that license and attribution. The upstream source remains at
[github.com/tsoding/musializer](https://github.com/tsoding/musializer).

The bundled Space Grotesk interface face and Alegreya caption face are released
under the SIL Open Font License 1.1. Their copyright and license notices are in
[`resources/fonts`](resources/fonts).
