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
- Switch among seven built-in scenes: Spectrum, Pulse Field, Orbital Lattice,
  ASCII Field, Song Atlas, Spectral Terrarium, and Constellation.
- Import an image as ASCII art, author timeline events, and edit timed lyrics in
  the application.
- Navigate with a whole-track waveform, draggable hairline playhead, exact
  tenth/one/ten-second seek controls, and matching keyboard steps.
- Generate local measured section suggestions, transcribe lyrics with Whisper,
  review those timings with headless Codex, and optionally ask Xiaomi MiMo V2.5
  through OpenRouter for a semantic description of how the music feels.
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

1. Choose a scene from the left rail. Keys <kbd>1</kbd> through <kbd>7</kbd>
   switch scenes directly.
2. Open **Lyrics** to write or import lyric cues and adjust their start/end
   times against the playhead.
3. Open **Assist** for timed-lyric help, measured scene planning, semantic music
   interpretation, or the complete pipeline. Results are staged with an impact
   summary and require an explicit Apply action.
4. Inspect the generated section markers and enable **Auto scenes** if they
   should drive both preview and export.
5. Add manual Constellation events with **+ Feel**, **+ Cue**, and **+ Custom**,
   or import an image into ASCII Field.
6. Open **Export**, choose resolution, frame rate, and quality, then select the
   destination. Progress reports the exact frame count and ETA; cancellation
   does not replace an existing video.
7. Save with <kbd>Ctrl</kbd>+<kbd>S</kbd>, or use
   <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> for **Save As**.

Canonical edits autosave after a short idle period. Unapplied lyric drafts,
staged assistance, running jobs, active exports, and unsaved projects are
guarded before context changes or quit, so partial work is not silently lost.

Timed lyrics use the same caption layer in preview and export. Long cues wrap
to three centered lines with a visible ellipsis. The bundled font atlas covers
accented Latin, Greek, Cyrillic, punctuation, currency, and common symbols;
CJK fallback, bidirectional text, and complex-script shaping are not yet
implemented.

### Assisted analysis and privacy

Assistance is optional and capability-based:

- **Measured section planning** is local and derives timing/structure from PCM.
- **Timed lyrics** uses a configured local Whisper installation, then an
  evidence-preserving headless Codex review.
- **MiMo feelings** uses the local measured analysis plus MiMo V2.5 through
  OpenRouter to produce semantic energy/tension/valence cues.
- **Full assist** combines those stages while retaining separate provenance.

Whisper evidence, Codex review, measured audio features, MiMo interpretation,
and user-authored events remain distinct data lanes. MiMo/OpenRouter modes are
the explicit authorization boundary for sending derived music information to a
remote service; the UI asks before launch. Model output is validated, staged,
and never mutates the project automatically.

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
Audio below the saved project directory can be stored as a verified relative
reference; unrelated assets retain a canonical absolute path.

The v1 schema deliberately describes future composition features that this
editor cannot yet preserve. This build opens only its lossless editor subset:
referenced full-track audio, integer-frame-rate H.264 MP4 output, one enabled
opaque full-track Normal scene, and no parameter cues or mappings. Other
schema-valid documents are rejected with an explicit unsupported-feature error
instead of being silently rewritten.

Imported ASCII grids are currently per-track editor state but are not stored in
`.musi` v1. Export works, but project save is blocked while a populated grid
exists. Use **Clear image** to discard it explicitly; an empty ASCII scene then
saves and reopens normally.

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
```

Built-in scene selectors are `spectrum`, `pulse`, `orbital`, `ascii`, `atlas`,
`terrarium`, and `constellation`. Repeatable `--event type:seconds:id:value`
arguments accept `lyric`, `semantic`, `cue`, or `custom`. A positional `.musi`
file is equivalent to `--project`. Command-line renders exit after FFmpeg
finishes, making them suitable for scripts and smoke tests.

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
