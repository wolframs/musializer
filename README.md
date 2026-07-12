# Musializer

<p align=center>
  <img src="./resources/logo/logo-256.png">
</p>

> [!WARNING]
> Musializer is under active development and the `.musi` format is currently
> version 1. Keep backups of irreplaceable projects when moving between builds.

The project aims to make a tool for creating beautiful music visualizations and rendering high quality videos of them.

*Please, read [CONTRIBUTING.md](CONTRIBUTING.md) before making a PR.*

## Demo

*Music by [@nu11](https://soundcloud.com/nu11-chiptune) from [https://soundcloud.com/nu11-chiptune/nu11-wip-works-2016-2022](https://soundcloud.com/nu11-chiptune/nu11-wip-works-2016-2022) at 20:38*

https://github.com/tsoding/musializer/assets/165283/8b9f9653-9b3d-4c04-9569-338fa19af071

## Supported Audio Formats

- wav
- ogg
- mp3
- qoa
- flac

Tracker modules (`xm` and `mod`) are supported for interactive playback and
preview, but not offline MP4 export yet: raylib can stream them as music but
cannot expose the decoded Wave required by the deterministic export analyzer.

## Download Binaries

This product branch does not publish signed binaries yet. Build it from source,
or use the Linux launcher installer below. The older upstream alpha downloads
predate projects, assisted analysis, the scene engine, and the current export
pipeline.

## Build from Source

External Dependencies:
- [ffmpeg](https://ffmpeg.org/) executable available in `PATH` environment variable. It is called as a child process during the rendering of the videos. So if you don't plan to render any videos it's completely **optional**.

We are using Custom Build System written entirely in C called `nob`. [nob.c](./nob.c) is the program that builds Musializer. For more info on this Build System see the [nob.h repo](https://github.com/tsoding/nob.h).

Before using `nob` you need to bootstrap it. Just compile it with the available C compiler. On Linux it's usually `$ cc -o nob nob.c` on Windows with MSVC from within `vcvarsall.bat` it's `$ cl.exe nob.c`. You only need to boostrap it once. After the bootstrap you can just keep running the same executable over and over again. It even tries to rebuild itself if you modify [nob.c](./nob.c) (which may fail sometimes, so in that case be ready to reboostrap it).

I really recommend to read [nob.c](./nob.c) and [nob.h](https://github.com/tsoding/nob.h) to get an idea of how it all actually works. The Build System is a work in progress, so if something breaks be ready to dive into it.

### Linux and OpenBSD

```console
$ cc -o nob nob.c # ONLY ONCE!!!
$ ./nob
$ ./build/musializer
```

Development profiles and the headless test suite are available after the same
bootstrap:

```console
$ ./nob build debug
$ ./nob build sanitize
$ ./nob build hotreload
$ ./nob test
$ ./nob test sanitize
```

The `release`, `debug`, and `hotreload` application profiles are implemented by
the Linux, macOS, OpenBSD, MinGW-w64, and MSVC recipes. The full
AddressSanitizer + UndefinedBehaviorSanitizer `sanitize` application profile is
available on Linux and macOS; OpenBSD, MinGW-w64, and MSVC reject it explicitly
instead of silently emitting a non-sanitized build. The test sanitizer likewise
requires a host `cc` with compatible ASan and UBSan runtimes.

`./nob dist` always rebuilds before packaging. On Linux it uses a separate
portable raylib object directory and omits the workstation-only
`-march=native` flag used by `build release`. Linux and OpenBSD produce a
`.tar.gz`; MinGW produces a `.zip`; macOS produces `build/Musializer.app`.
Distribution archives include the license, product documentation, `.env.example`,
Linux desktop integration where applicable, and the Python analysis adapters
with their prompt and schemas. FFmpeg, Python, NumPy, Whisper, and Codex remain
external runtime dependencies where their features are used. See
[`packaging/PRODUCT_READINESS.md`](packaging/PRODUCT_READINESS.md) for the
current platform matrix and known packaging limits.

Run the dependency-free product doctor before a demo, export, or assisted
analysis session:

```console
$ python3 tools/musializer_doctor.py
$ python3 tools/musializer_doctor.py --json --require export --require local_lyrics
```

It reports preview, MP4 export, local Whisper/Codex lyrics, and remote MiMo as
separate capabilities. The check does not invoke those programs or make network
requests, and reports only whether an OpenRouter credential is configured—not
its value.

Tracks may also be loaded from the command line. The optional scene selector is
useful for repeatable smoke tests:

```console
$ ./build/musializer --help
$ ./build/musializer --scene orbital path/to/track.mp3
$ ./build/musializer --scene atlas path/to/track.mp3
$ ./build/musializer --scene terrarium path/to/track.mp3
$ ./build/musializer --scene constellation \
    --event lyric:1.25:42:0.9 --event cue:2.0:43:1.0 path/to/track.mp3
$ ./build/musializer --ascii-image path/to/image.png path/to/track.mp3
$ ./build/musializer --scene orbital --resolution 2560x1440 --fps 60 \
    --quality master path/to/track.wav --render output.mp4
$ ./build/musializer path/to/track.mp3 \
    --analysis-bridge build/analysis/track/analysis.bridge.tsv \
    --auto-scenes --render output.mp4
$ ./build/musializer path/to/track.mp3 --scene atlas \
    --quality high --save-project path/to/show.musi
$ ./build/musializer --project path/to/show.musi --render output.mp4
```

Built-in scene names are `spectrum`, `pulse`, `orbital`, `ascii`, `atlas`,
`terrarium`, and `constellation`. While the app is running, <kbd>1</kbd> through
<kbd>7</kbd> switch between them. Repeatable `--event`
`type:seconds:id:value` arguments accept `lyric`, `semantic`, `cue`, or `custom`
events for Constellation replay. A command-line render exits automatically
after FFmpeg finishes, making it useful for smoke tests and scripted renders.
Resolution accepts any validated even size up to 7680x4320; the UI presents
720p, 1080p, 1440p, and 2160p presets at 24, 30, or 60 fps. Quality is
`balanced`, `high`, or `master`. A positional `.musi` file is equivalent to
`--project`; `--save-project` writes the active track project and exits when no
render is requested.
`--analysis-bridge` verifies the bridge's audio SHA-256 before importing lyric,
semantic, or scene lanes; `--auto-scenes` opts into its section recommendations
for both preview and offline rendering.

Preview rendering requests 4x MSAA. Offline rendering uses a 2x spatial render
followed by an output-resolution downsample for High and Master; Balanced uses
native output resolution. Set `MUSIALIZER_RENDER_SUPERSAMPLE=0` to force a 1x
render on GPUs that cannot allocate the larger target. The default is
1920x1080, 30 fps, High. Balanced/High/Master map to H.264 High-profile CRF
20/16/12, with BT.709 color metadata, `yuv420p`, 256 or 320 kbit/s AAC, and
fast-start metadata. Frame scheduling is derived from the exact decoded sample
count.
The renderer analyzes one decoded PCM stream and stages that same stream for
FFmpeg, avoiding compressed-audio decoder disagreement. It renders
`ceil(decoded duration * FPS)` frames and pads only the final sub-frame audio
tail so audio, video, and container end on the same deterministic frame
boundary, within the MP4 muxer's time-base rounding. FFmpeg writes a temporary
sibling and only replaces the chosen output
after a successful, bounded finalize, so cancellation or encoder failure
preserves an existing video.

The same workflow is available in the application UI:

- use **Open audio**, **Open project**, or drag-and-drop from the first-run
  workspace; the app also exposes **Add audio**, **Save**, and **Save As**;
- choose any built-in scene from the **Scenes** rail on the left;
- click **Import image -> ASCII**, or drop an image, to populate and select
  ASCII Field;
- open **Lyrics** to add/select lyric cue blocks, edit their UTF-8 content,
  nudge or set start/end times at the playhead, and import/export a bounded
  `.lyrics.tsv` editing file;
- open **Assist** to run local timed-lyrics assistance (Whisper plus an
  evidence-only headless Codex review), measured scene-change planning,
  MiMo feeling analysis, or the complete pipeline without blocking playback.
  Remote modes show their privacy boundary before launch; results are staged
  with counts, replacement impact, and a first-lyric preview, then require a
  second confirmation. Accepted MiMo cues steer palette/tension across every
  built-in scene without being mixed into measured audio;
- enable **Auto scenes** after inspecting generated section markers when scene
  recommendations should drive preview and deterministic export;
- use **+ Feel**, **+ Cue**, and **+ Custom** on the timeline to record
  color-coded Constellation events at the current playhead;
- use **Clear manual** with confirmation and one-level undo to remove only
  authored events; lyrics, semantics, and scene suggestions remain intact;
- open **Export** to choose size, frame rate, and quality, review the estimated
  frame count and scene plan, choose an output, and follow the exact
  decoded frame total/elapsed/ETA once export starts. Cancellation is
  transactional;
- press <kbd>Ctrl</kbd>+<kbd>S</kbd> to save an existing project. Canonical
  edits autosave after a short idle period; an unapplied lyric draft blocks
  context changes and autosave until it is applied or discarded. Use
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> for **Save As**;
- treat each entry in **Track projects** as an independent one-song `.musi`
  document. Named inactive projects autosave too, and quitting confirms before
  discarding unnamed/unsaved work;

Analysis artifacts and logs are cached under `build/analysis/`. Canonical JSON
keeps measured audio, Whisper evidence, Codex review, MiMo interpretation, and
scene recommendations separate; only a validated derived bridge enters the C
editor. **MiMo feelings** and **Full assist** are the explicit authorization
boundary for an OpenRouter request. They read `OPENROUTER_API_KEY` from the
environment or the ignored repository `.env` without sourcing that file.

Projects embed lyrics, semantic events, manual events, scene suggestions,
per-track deterministic seed, and export intent. Analysis references retain
provenance, but evaluated semantic cues do not depend on a mutable cache file
when a project is reopened. Project writes use an exclusively created sibling,
flush, and atomic replacement. Audio beside or below the destination project is
stored as a verified project-relative reference; unrelated assets safely fall
back to canonical absolute paths.

The v1 schema deliberately describes more future-facing composition features
than this editor can preserve. This build opens only the lossless editor subset:
referenced audio, full-track integer-FPS H.264 MP4 output, one enabled opaque
full-track Normal scene, and no parameter cues or mappings. Other schema-valid
v1 documents are rejected with an explicit unsupported-feature error instead of
being silently normalized on save.

Timed lyric cues render as a shared caption layer in preview and MP4 export, so
Whisper/Codex timing work has an immediate visual result in every scene. Long
cues wrap to three centered lines with visible ellipsis when necessary, and the
bundled caption atlas deliberately covers accented Latin, Greek, Cyrillic,
punctuation, currency, and common interface symbols.

Imported ASCII grids are isolated per open track, but the source-image asset is
not in `.musi` v1 yet. Video export works; project save is blocked while any
imported grid is present, preventing hidden image loss even after switching
scenes. Use **Clear image** to discard the grid explicitly; an empty ASCII scene
then saves and reopens deterministically.

CLI arguments remain useful for repeatable automation, but are not required for
normal project, scene, image, event, assistance, or render workflows.

### Linux application launcher

On Linux desktops, including KDE Plasma, install a per-user application-menu
launcher once:

```console
$ ./tools/install-linux-launcher.sh
```

After that, open **Musializer** from the application menu like any other app.
Audio files can also be passed through the desktop launcher or dropped onto its
icon. The installer registers `.musi` projects with the desktop MIME database,
so they can be opened through the file manager as well. From a source checkout
it builds the release executable once; from an unpacked Linux distribution it
uses the packaged executable without requiring build tooling. Ordinary launches
never invoke the compiler. Re-run the installer after pulling source changes
when you want a fresh release build.

The launcher is installed entirely under `~/.local`, requires no `sudo`, and
writes diagnostic output to
`~/.local/state/musializer/launcher.log`. To remove it:

```console
$ ./tools/install-linux-launcher.sh --uninstall
```

Use `--no-refresh` to skip desktop-menu cache refresh commands in containers or
automated tests.

For a repeatable legacy-scene export smoke, use any short WAV fixture and check
the resulting streams:

```console
$ ./build/musializer --scene spectrum fixture.wav --render /tmp/musializer-smoke.mp4
$ ffprobe -v error -show_entries stream=codec_type,width,height,r_frame_rate \
    -of compact /tmp/musializer-smoke.mp4
```

For an anti-aliasing visual smoke, repeat the render with `spectrum`, `ascii`,
and `constellation`, extract a frame with `ffmpeg -ss 1 -i output.mp4 -frames:v
1 frame.png`, and inspect it at 100% scale. These cover shader edges, font
glyphs, and fine 3D geometry respectively. Repeat one render with
`MUSIALIZER_RENDER_SUPERSAMPLE=0`; `ffprobe` should still report the selected
output size and frame rate (1920x1080 at 30 fps by default).

Optional offline analysis helpers for measured audio, Whisper timings, and
cached MiMo interpretations are documented in
[`tools/ANALYSIS_ADAPTERS.md`](tools/ANALYSIS_ADAPTERS.md) and
[`tools/MEASURED_ANALYSIS.md`](tools/MEASURED_ANALYSIS.md). They are separate
from the C renderer and never make network calls unless the MiMo helper is
explicitly invoked without `--dry-run`.

If the build fails because of missing header files, you may need to install the X11 dev packages.

On Debian, Ubuntu, etc, do this:

```console
$ sudo apt install libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev
```

On other distro's, use the appropriate package manager.

### Windows MSVC

From within `vcvarsall.bat` do

```console
> cl.exe nob.c # ONLY ONCE!!!
> nob.exe
> build\musializer.exe
```

### Cross Compilation from Linux to Windows using MinGW-w64

Install [MinGW-w64](https://www.mingw-w64.org/) from your distro repository.

Edit `./build/config.h` and set `MUSIALIZER_TARGET_WIN64_MINGW` instead of `MUSIALIZER_TARGET_LINUX`.

```console
$ ./nob
$ wine ./build/musializer.exe
```

## Hot Reloading

```console
$ ./nob build hotreload
$ ./build/musializer
```

Enabling `MUSIALIZER_HOTRELOAD` in `./build/config.h` remains supported for
existing local configurations, but the named profile is the recommended path.

Keep the app running. Rebuild with `./nob`. Hot reload by focusing on the window of the app and pressing <kbd>h</kbd>.
For an automated one-cycle handoff smoke, pass `--reload-once` together with a
track or scripted render.

The way it works is by putting the majority of the logic of the application into a `libplug` dynamic library and just reloading it when requested. The [rpath](https://en.wikipedia.org/wiki/Rpath) (aka hard-coded run-time search path) for that library is set to `.` and `./build/`. See [src_build/nob_linux.c](src_build/nob_linux.c) for more information on how everything is configured.
