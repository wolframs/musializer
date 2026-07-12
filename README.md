# Musializer

<p align=center>
  <img src="./resources/logo/logo-256.png">
</p>

> [!WARNING]
> This software is unfinished. Keep your expectations low.

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
- xm
- mod
- flac

## Download Binaries

- Windows: [musializer-alpha2-win64.zip](https://github.com/tsoding/musializer/releases/download/alpha2/musializer-alpha2-win64.zip)
- Linux: *in progress*

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

Tracks may also be loaded from the command line. The optional scene selector is
useful for repeatable smoke tests:

```console
$ ./build/musializer --scene orbital path/to/track.mp3
$ ./build/musializer --scene atlas path/to/track.mp3
$ ./build/musializer --scene terrarium path/to/track.mp3
$ ./build/musializer --scene constellation \
    --event lyric:1.25:42:0.9 --event cue:2.0:43:1.0 path/to/track.mp3
$ ./build/musializer --ascii-image path/to/image.png path/to/track.mp3
$ ./build/musializer --scene orbital path/to/track.wav --render output.mp4
$ ./build/musializer path/to/track.mp3 \
    --analysis-bridge build/analysis/track/analysis.bridge.tsv \
    --auto-scenes --render output.mp4
```

Built-in scene names are `spectrum`, `pulse`, `orbital`, `ascii`, `atlas`,
`terrarium`, and `constellation`. While the app is running, <kbd>1</kbd> through
<kbd>7</kbd> switch between them. Repeatable `--event`
`type:seconds:id:value` arguments accept `lyric`, `semantic`, `cue`, or `custom`
events for Constellation replay. A command-line render exits automatically
after FFmpeg finishes, making it useful for smoke tests and scripted renders.
`--analysis-bridge` verifies the bridge's audio SHA-256 before importing lyric,
semantic, or scene lanes; `--auto-scenes` opts into its section recommendations
for both preview and offline rendering.

Preview rendering requests 4x MSAA. Offline rendering uses a 2x spatial render
followed by a fixed-resolution downsample; set
`MUSIALIZER_RENDER_SUPERSAMPLE=0` to exercise the 1600x900 fallback directly on
GPUs that cannot allocate the larger render target.

The same workflow is available in the application UI:

- click the empty screen or drop an audio file to load a track;
- choose any built-in scene from the **Scenes** rail on the left;
- click **Import image -> ASCII**, or drop an image, to populate and select
  ASCII Field;
- open **Lyrics** to add/select lyric cue blocks, edit their UTF-8 content,
  nudge or set start/end times at the playhead, and import/export a bounded
  `.lyrics.tsv` editing file;
- open **Assist** to run local timed-lyrics assistance (Whisper plus an
  evidence-only headless Codex review), measured scene-change planning,
  MiMo feeling analysis, or the complete pipeline without blocking playback;
- enable **Auto scenes** after inspecting generated section markers when scene
  recommendations should drive preview and deterministic export;
- use **+ Feel**, **+ Cue**, and **+ Custom** on the timeline to record
  color-coded Constellation events at the current playhead;
- use **Clear** to remove recorded events, and the film icon to render the
  currently selected scene.

Analysis artifacts and logs are cached under `build/analysis/`. Canonical JSON
keeps measured audio, Whisper evidence, Codex review, MiMo interpretation, and
scene recommendations separate; only a validated derived bridge enters the C
editor. **MiMo feelings** and **Full assist** are the explicit authorization
boundary for an OpenRouter request. They read `OPENROUTER_API_KEY` from the
environment or the ignored repository `.env` without sourcing that file.

CLI arguments remain useful for repeatable automation, but are not required for
normal scene, image, event, or render workflows.

### Linux application launcher

On Linux desktops, including KDE Plasma, install a per-user application-menu
launcher once:

```console
$ ./tools/install-linux-launcher.sh
```

After that, open **Musializer** from the application menu like any other app.
Audio files can also be passed through the desktop launcher or dropped onto its
icon. The installer builds the release executable once; ordinary launches run
that executable directly and do not invoke the compiler. Re-run the installer
after pulling code changes when you want a fresh release build.

The launcher is installed entirely under `~/.local`, requires no `sudo`, and
writes diagnostic output to
`~/.local/state/musializer/launcher.log`. To remove it:

```console
$ ./tools/install-linux-launcher.sh --uninstall
```

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
`MUSIALIZER_RENDER_SUPERSAMPLE=0`; `ffprobe` should still report 1600x900 at
30 fps.

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
